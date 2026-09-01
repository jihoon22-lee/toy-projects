#!/usr/bin/env python3
"""Audit BuildScope release archives and provenance without extracting them."""

from __future__ import annotations

import argparse
import ast
import base64
import csv
import hashlib
import io
import json
import math
import os
import re
import stat
import struct
import tarfile
import zipfile
from collections.abc import Iterator, Sequence
from contextlib import contextmanager
from email import policy
from email.parser import BytesParser
from pathlib import Path, PurePosixPath
from typing import Any, BinaryIO

from check_buildscope_b5_report import BuildScopeB5ReportError, validate_report
from check_published_html import PublishedHtmlError
from check_published_html import check_report as check_html_report

MAX_LOCAL_ASSET_BYTES = 256 * 1024 * 1024
MAX_JSON_BYTES = 20 * 1024 * 1024
MAX_METADATA_BYTES = 2 * 1024 * 1024
MAX_ARCHIVE_MEMBERS = 10_000
MAX_ARCHIVE_MEMBER_BYTES = 256 * 1024 * 1024
MAX_ARCHIVE_TOTAL_BYTES = 512 * 1024 * 1024
MAX_ZIP_CENTRAL_DIRECTORY_BYTES = 16 * 1024 * 1024
HASH_CHUNK_BYTES = 1024 * 1024
SHA_PATTERN = re.compile(r"^[0-9a-f]{40}$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
RUN_ID_PATTERN = re.compile(r"^[1-9][0-9]{0,19}$")
VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
ICI_VERSION_PATTERN = re.compile(r"^v[0-9]+\.[0-9]+\.[0-9]+$")
REPOSITORY_PATTERN = re.compile(
    r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,99}/[A-Za-z0-9][A-Za-z0-9_.-]{0,99}$"
)
NATIVE_SUFFIXES = (".so", ".pyd", ".dylib", ".dll")
SCHEMA_NAMES = (
    "buildscope-diff-v1.schema.json",
    "buildscope-snapshot-v1.schema.json",
    "buildscope-snapshot-v2.schema.json",
    "buildscope-snapshot-v3.schema.json",
)
PACKAGE_MODULES = (
    "__init__.py",
    "__main__.py",
    "_command.py",
    "_io.py",
    "_metadata.py",
    "_paths.py",
    "_replay_policy.py",
    "compiler_replay.py",
    "diff.py",
    "diff_cli.py",
    "diff_glob.py",
    "diff_policy.py",
    "include_analysis.py",
    "normalize.py",
    "snapshot.py",
)
PYZ_SHEBANG = b"#!/usr/bin/env python3\n"


class BuildScopeReleasePayloadError(ValueError):
    """A downloaded BuildScope payload violates the release contract."""


def _open_flags() -> int:
    flags = os.O_RDONLY
    flags |= getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    flags |= getattr(os, "O_NONBLOCK", 0)
    return flags


def _same_file(left: os.stat_result, right: os.stat_result) -> bool:
    return left.st_dev == right.st_dev and left.st_ino == right.st_ino


def _stable_file(left: os.stat_result, right: os.stat_result) -> bool:
    return (
        _same_file(left, right)
        and left.st_size == right.st_size
        and left.st_mtime_ns == right.st_mtime_ns
        and left.st_ctime_ns == right.st_ctime_ns
    )


@contextmanager
def _open_regular(
    path: Path,
    label: str,
    *,
    maximum_bytes: int = MAX_LOCAL_ASSET_BYTES,
) -> Iterator[tuple[BinaryIO, os.stat_result]]:
    """Open a bounded regular file without following its final path component."""

    try:
        descriptor = os.open(path, _open_flags())
    except OSError as exc:
        raise BuildScopeReleasePayloadError(
            f"{label} cannot be opened safely: {exc}"
        ) from exc
    stream: BinaryIO | None = None
    try:
        opened = os.fstat(descriptor)
        if not stat.S_ISREG(opened.st_mode):
            raise BuildScopeReleasePayloadError(f"{label} must be a regular file")
        if opened.st_size <= 0 or opened.st_size > maximum_bytes:
            raise BuildScopeReleasePayloadError(
                f"{label} size is outside the accepted range: {opened.st_size} bytes"
            )
        try:
            named = path.stat(follow_symlinks=False)
        except OSError as exc:
            raise BuildScopeReleasePayloadError(
                f"{label} path cannot be rechecked: {exc}"
            ) from exc
        if not stat.S_ISREG(named.st_mode) or not _same_file(opened, named):
            raise BuildScopeReleasePayloadError(f"{label} changed while it was opened")

        stream = os.fdopen(descriptor, "rb", closefd=True)
        descriptor = -1
        yield stream, opened

        after = os.fstat(stream.fileno())
        try:
            named_after = path.stat(follow_symlinks=False)
        except OSError as exc:
            raise BuildScopeReleasePayloadError(
                f"{label} path cannot be checked after reading: {exc}"
            ) from exc
        if not _stable_file(opened, after) or not _stable_file(opened, named_after):
            raise BuildScopeReleasePayloadError(f"{label} changed while it was read")
    finally:
        if stream is not None:
            stream.close()
        elif descriptor >= 0:
            os.close(descriptor)


def _stream_digest(path: Path, label: str) -> str:
    digest = hashlib.sha256()
    with _open_regular(path, label) as (stream, _):
        while True:
            chunk = stream.read(HASH_CHUNK_BYTES)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def _read_json(path: Path, label: str) -> dict[str, Any]:
    with _open_regular(path, label, maximum_bytes=MAX_JSON_BYTES) as (stream, _):
        payload = stream.read(MAX_JSON_BYTES + 1)
    if len(payload) > MAX_JSON_BYTES:
        raise BuildScopeReleasePayloadError(f"{label} exceeds its read bound")
    try:
        decoded = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise BuildScopeReleasePayloadError(
            f"{label} is not valid UTF-8: {exc}"
        ) from exc

    def bounded_int(raw: str) -> int:
        if len(raw) > 20:
            raise ValueError("JSON integer exceeds 20 decimal digits")
        return int(raw)

    def bounded_float(raw: str) -> float:
        if len(raw) > 100:
            raise ValueError("JSON float exceeds 100 characters")
        value = float(raw)
        if not math.isfinite(value):
            raise ValueError("JSON float must be finite")
        return value

    def reject_constant(raw: str) -> None:
        raise ValueError(f"non-standard JSON constant: {raw}")

    def unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate JSON key: {key}")
            result[key] = value
        return result

    try:
        value = json.loads(
            decoded,
            parse_int=bounded_int,
            parse_float=bounded_float,
            parse_constant=reject_constant,
            object_pairs_hook=unique_object,
        )
    except (json.JSONDecodeError, ValueError) as exc:
        raise BuildScopeReleasePayloadError(
            f"{label} is not valid JSON: {exc}"
        ) from exc
    if not isinstance(value, dict):
        raise BuildScopeReleasePayloadError(f"{label} must contain one JSON object")
    return value


def _safe_member_name(name: str, label: str) -> PurePosixPath:
    if (
        not name
        or len(name) > 4096
        or "\x00" in name
        or "\\" in name
        or name.startswith("/")
    ):
        raise BuildScopeReleasePayloadError(f"unsafe {label} member name: {name!r}")
    raw_parts = name.split("/")
    path = PurePosixPath(name)
    if (
        not path.parts
        or any(part in {"", ".", ".."} for part in raw_parts)
        or any(":" in part for part in raw_parts)
    ):
        raise BuildScopeReleasePayloadError(f"unsafe {label} member path: {name!r}")
    return path


def _check_member_budget(count: int, total: int, size: int, label: str) -> int:
    if count > MAX_ARCHIVE_MEMBERS:
        raise BuildScopeReleasePayloadError(
            f"{label} contains more than {MAX_ARCHIVE_MEMBERS} members"
        )
    if size < 0 or size > MAX_ARCHIVE_MEMBER_BYTES:
        raise BuildScopeReleasePayloadError(f"{label} member size is invalid: {size}")
    updated = total + size
    if updated > MAX_ARCHIVE_TOTAL_BYTES:
        raise BuildScopeReleasePayloadError(
            f"{label} expands beyond {MAX_ARCHIVE_TOTAL_BYTES} bytes"
        )
    return updated


def _zip_preflight(
    stream: BinaryIO,
    label: str,
    *,
    prefix: bytes = b"",
) -> None:
    """Validate a bounded, single-volume, non-ZIP64 archive before ZipFile.

    ``zipfile.ZipFile`` constructs one ``ZipInfo`` object per central-directory
    record in its constructor.  A hostile archive can therefore consume a
    large amount of memory before the later inventory limits run.  This raw
    preflight only reads the bounded EOCD tail and central directory bytes,
    counts/scans records without creating ``ZipInfo`` objects, and then resets
    the stream for the normal parser.
    """

    eocd_size = 22
    eocd_signature = b"PK\x05\x06"
    central_signature = b"PK\x01\x02"
    zip64_locator_signature = b"PK\x06\x07"
    central_header_size = 46
    max_comment_bytes = 65_535
    try:
        stream.seek(0, os.SEEK_END)
        file_size = stream.tell()
        if file_size <= len(prefix) + eocd_size:
            raise BuildScopeReleasePayloadError(
                f"{label} is too small for a ZIP archive"
            )
        tail_size = min(file_size, eocd_size + max_comment_bytes)
        stream.seek(file_size - tail_size, os.SEEK_SET)
        tail = stream.read(tail_size)
        if len(tail) != tail_size:
            raise BuildScopeReleasePayloadError(f"{label} ZIP trailer is truncated")

        eocd_offset = tail.rfind(eocd_signature)
        if eocd_offset < 0 or eocd_offset + eocd_size > len(tail):
            raise BuildScopeReleasePayloadError(f"{label} has no valid ZIP end record")
        eocd_position = file_size - tail_size + eocd_offset
        (
            signature,
            disk_number,
            central_disk,
            entries_on_disk,
            total_entries,
            central_size,
            central_offset,
            comment_size,
        ) = struct.unpack_from("<4s4H2LH", tail, eocd_offset)
        if signature != eocd_signature:
            raise BuildScopeReleasePayloadError(
                f"{label} has an invalid ZIP end record"
            )
        if eocd_position + eocd_size + comment_size != file_size:
            raise BuildScopeReleasePayloadError(
                f"{label} ZIP end record has an inconsistent comment length"
            )
        if disk_number != 0 or central_disk != 0 or entries_on_disk != total_entries:
            raise BuildScopeReleasePayloadError(
                f"{label} must be a single-volume ZIP with consistent member counts"
            )
        if (
            entries_on_disk == 0xFFFF
            or total_entries == 0xFFFF
            or central_size == 0xFFFFFFFF
            or central_offset == 0xFFFFFFFF
        ):
            raise BuildScopeReleasePayloadError(
                f"{label} ZIP64 archives are not supported"
            )
        if (
            eocd_offset >= 20
            and tail[eocd_offset - 20 : eocd_offset - 16] == zip64_locator_signature
        ):
            raise BuildScopeReleasePayloadError(
                f"{label} ZIP64 archives are not supported"
            )
        if total_entries > MAX_ARCHIVE_MEMBERS:
            raise BuildScopeReleasePayloadError(
                f"{label} contains more than {MAX_ARCHIVE_MEMBERS} members"
            )
        if central_size > MAX_ZIP_CENTRAL_DIRECTORY_BYTES:
            raise BuildScopeReleasePayloadError(
                f"{label} central directory exceeds "
                f"{MAX_ZIP_CENTRAL_DIRECTORY_BYTES} bytes"
            )

        central_position = len(prefix) + central_offset
        central_end = central_position + central_size
        if (
            central_position < len(prefix)
            or central_position > eocd_position
            or central_end != eocd_position
        ):
            raise BuildScopeReleasePayloadError(
                f"{label} central directory bounds are inconsistent"
            )
        stream.seek(central_position, os.SEEK_SET)
        central = stream.read(central_size)
        if len(central) != central_size:
            raise BuildScopeReleasePayloadError(
                f"{label} central directory is truncated"
            )

        position = 0
        count = 0
        total_uncompressed = 0
        names: set[str] = set()
        while position < central_size:
            remaining = central_size - position
            if remaining < central_header_size:
                raise BuildScopeReleasePayloadError(
                    f"{label} central directory has a truncated header"
                )
            fields = struct.unpack_from("<4s6H3L5H2L", central, position)
            (
                signature,
                _made_by,
                _needed,
                flags,
                _compression,
                _modified_time,
                _modified_date,
                _crc32,
                compressed_size,
                uncompressed_size,
                name_size,
                extra_size,
                comment_size,
                disk_start,
                _internal_attributes,
                _external_attributes,
                local_offset,
            ) = fields
            if signature != central_signature:
                raise BuildScopeReleasePayloadError(
                    f"{label} central directory has an invalid header at member {count + 1}"
                )
            if (
                compressed_size == 0xFFFFFFFF
                or uncompressed_size == 0xFFFFFFFF
                or disk_start == 0xFFFF
                or local_offset == 0xFFFFFFFF
            ):
                raise BuildScopeReleasePayloadError(
                    f"{label} ZIP64 member fields are not supported"
                )
            if name_size > 4096:
                raise BuildScopeReleasePayloadError(f"{label} member name is too long")
            record_size = central_header_size + name_size + extra_size + comment_size
            if record_size > remaining:
                raise BuildScopeReleasePayloadError(
                    f"{label} central directory member is truncated"
                )
            record_end = position + record_size
            name_start = position + central_header_size
            name_end = name_start + name_size
            extra_end = name_end + extra_size
            name_bytes = central[name_start:name_end]
            extra = central[name_end:extra_end]
            extra_position = 0
            while extra_position < len(extra):
                if len(extra) - extra_position < 4:
                    raise BuildScopeReleasePayloadError(
                        f"{label} member extra data is malformed"
                    )
                extra_id, extra_length = struct.unpack_from(
                    "<HH", extra, extra_position
                )
                extra_position += 4
                if extra_length > len(extra) - extra_position:
                    raise BuildScopeReleasePayloadError(
                        f"{label} member extra data is truncated"
                    )
                if extra_id == 0x0001:
                    raise BuildScopeReleasePayloadError(
                        f"{label} ZIP64 member fields are not supported"
                    )
                extra_position += extra_length
            encoding = "utf-8" if flags & 0x800 else "cp437"
            try:
                member_name = name_bytes.decode(encoding)
            except UnicodeDecodeError as exc:
                raise BuildScopeReleasePayloadError(
                    f"{label} member name is not valid {encoding}: {exc}"
                ) from exc
            canonical_name = member_name.rstrip("/")
            _safe_member_name(canonical_name, label)
            if canonical_name in names:
                raise BuildScopeReleasePayloadError(
                    f"{label} contains duplicate member {member_name!r}"
                )
            names.add(canonical_name)
            count += 1
            total_uncompressed = _check_member_budget(
                count,
                total_uncompressed,
                uncompressed_size,
                label,
            )
            local_position = len(prefix) + local_offset
            if local_position < len(prefix) or local_position + 30 > central_position:
                raise BuildScopeReleasePayloadError(
                    f"{label} member local-header offset is inconsistent"
                )
            position = record_end
        if count != total_entries:
            raise BuildScopeReleasePayloadError(
                f"{label} central directory member count mismatch: "
                f"EOCD={total_entries}, scanned={count}"
            )
        if not names:
            raise BuildScopeReleasePayloadError(f"{label} is empty")
    except BuildScopeReleasePayloadError:
        raise
    except (OSError, struct.error, ValueError) as exc:
        raise BuildScopeReleasePayloadError(
            f"invalid {label} ZIP structure: {exc}"
        ) from exc
    finally:
        try:
            stream.seek(0, os.SEEK_SET)
        except (OSError, ValueError):
            pass


def _zip_inventory(
    archive: zipfile.ZipFile,
    label: str,
) -> dict[str, zipfile.ZipInfo]:
    inventory: dict[str, zipfile.ZipInfo] = {}
    total = 0
    for count, info in enumerate(archive.infolist(), start=1):
        canonical_name = info.filename.rstrip("/")
        _safe_member_name(canonical_name, label)
        if canonical_name in inventory:
            raise BuildScopeReleasePayloadError(
                f"{label} contains duplicate member {info.filename!r}"
            )
        inventory[canonical_name] = info
        total = _check_member_budget(count, total, info.file_size, label)
        mode = info.external_attr >> 16
        kind = stat.S_IFMT(mode)
        if mode and kind not in {0, stat.S_IFREG, stat.S_IFDIR}:
            raise BuildScopeReleasePayloadError(
                f"{label} contains a non-file member: {info.filename}"
            )
    if not inventory:
        raise BuildScopeReleasePayloadError(f"{label} is empty")
    return inventory


def _zip_read(
    archive: zipfile.ZipFile,
    inventory: dict[str, zipfile.ZipInfo],
    name: str,
    label: str,
) -> bytes:
    info = inventory.get(name)
    if info is None or info.is_dir():
        raise BuildScopeReleasePayloadError(f"{label} is missing {name}")
    if info.file_size <= 0 or info.file_size > MAX_METADATA_BYTES:
        raise BuildScopeReleasePayloadError(f"{label} metadata size is invalid: {name}")
    payload = archive.read(info)
    if len(payload) != info.file_size:
        raise BuildScopeReleasePayloadError(f"{label} metadata size changed: {name}")
    return payload


def _zip_digest(archive: zipfile.ZipFile, info: zipfile.ZipInfo, label: str) -> str:
    digest = hashlib.sha256()
    total = 0
    with archive.open(info, "r") as stream:
        while True:
            chunk = stream.read(HASH_CHUNK_BYTES)
            if not chunk:
                break
            total += len(chunk)
            if total > info.file_size:
                raise BuildScopeReleasePayloadError(
                    f"{label} member exceeds its declared size: {info.filename}"
                )
            digest.update(chunk)
    if total != info.file_size:
        raise BuildScopeReleasePayloadError(
            f"{label} member is truncated: {info.filename}"
        )
    return digest.hexdigest()


def _is_native_name(name: str) -> bool:
    lowered = name.lower()
    return lowered.endswith(NATIVE_SUFFIXES) or ".so." in PurePosixPath(lowered).name


def _single_header(message: Any, name: str, expected: str, label: str) -> None:
    values = message.get_all(name, [])
    if values != [expected]:
        raise BuildScopeReleasePayloadError(
            f"{label} {name} mismatch: expected {expected!r}, got {values!r}"
        )


def _check_package_metadata(payload: bytes, version: str, label: str) -> None:
    message = BytesParser(policy=policy.default).parsebytes(payload, headersonly=True)
    _single_header(message, "Name", "buildscope", label)
    _single_header(message, "Version", version, label)
    _single_header(
        message,
        "Summary",
        "Offline explorer for C and C++ compilation databases",
        label,
    )
    _single_header(message, "Requires-Python", ">=3.10", label)
    if message.get_all("Requires-Dist", []):
        raise BuildScopeReleasePayloadError(f"{label} must not declare dependencies")


def _package_digests_from_zip(
    archive: zipfile.ZipFile,
    inventory: dict[str, zipfile.ZipInfo],
) -> dict[str, str]:
    digests = {
        f"package/{name}": _zip_digest(
            archive,
            inventory[f"buildscope/{name}"],
            "BuildScope package",
        )
        for name in PACKAGE_MODULES
    }
    digests.update(
        {
            f"schema/{name}": _zip_digest(
                archive,
                inventory[f"buildscope/schemas/{name}"],
                "BuildScope schema",
            )
            for name in SCHEMA_NAMES
        }
    )
    return digests


def _schema_digests_from_tar(
    archive: tarfile.TarFile,
    inventory: dict[str, tarfile.TarInfo],
    prefix: str,
    label: str,
) -> dict[str, str]:
    return {
        name: _tar_digest(
            archive,
            inventory,
            f"{prefix}{name}",
            label,
        )
        for name in SCHEMA_NAMES
    }


def _check_wheel_record(
    archive: zipfile.ZipFile,
    inventory: dict[str, zipfile.ZipInfo],
    record_name: str,
) -> None:
    record = _zip_read(archive, inventory, record_name, "BuildScope wheel")
    try:
        rows = list(csv.reader(io.StringIO(record.decode("utf-8"), newline="")))
    except (csv.Error, UnicodeError) as exc:
        raise BuildScopeReleasePayloadError(f"wheel RECORD is invalid: {exc}") from exc
    by_name: dict[str, tuple[str, str]] = {}
    for row in rows:
        if len(row) != 3:
            raise BuildScopeReleasePayloadError(f"wheel RECORD row is invalid: {row!r}")
        name, digest, size = row
        if name in by_name:
            raise BuildScopeReleasePayloadError(f"wheel RECORD duplicates {name!r}")
        by_name[name] = (digest, size)
    if set(by_name) != set(inventory):
        raise BuildScopeReleasePayloadError(
            "wheel RECORD member inventory is incomplete"
        )
    for name, info in inventory.items():
        digest, size = by_name[name]
        if name == record_name:
            if digest or size:
                raise BuildScopeReleasePayloadError(
                    "wheel RECORD self-entry must have empty digest and size"
                )
            continue
        expected_digest = (
            base64.urlsafe_b64encode(
                bytes.fromhex(_zip_digest(archive, info, "BuildScope wheel"))
            )
            .rstrip(b"=")
            .decode("ascii")
        )
        if digest != f"sha256={expected_digest}" or size != str(info.file_size):
            raise BuildScopeReleasePayloadError(f"wheel RECORD mismatch: {name}")


def check_wheel(path: Path, version: str) -> dict[str, str]:
    label = "BuildScope wheel"
    expected_dist = f"buildscope-{version}.dist-info/"
    with _open_regular(path, label) as (stream, _):
        try:
            _zip_preflight(stream, label)
            with zipfile.ZipFile(stream) as archive:
                inventory = _zip_inventory(archive, label)
                names = set(inventory)
                expected_names = {
                    *(f"buildscope/{name}" for name in PACKAGE_MODULES),
                    *(f"buildscope/schemas/{name}" for name in SCHEMA_NAMES),
                    expected_dist + "METADATA",
                    expected_dist + "WHEEL",
                    expected_dist + "entry_points.txt",
                    expected_dist + "RECORD",
                }
                if names != expected_names:
                    raise BuildScopeReleasePayloadError(
                        "wheel member inventory mismatch: "
                        f"missing={sorted(expected_names - names)!r} "
                        f"extra={sorted(names - expected_names)!r}"
                    )
                native = sorted(name for name in names if _is_native_name(name))
                if native:
                    raise BuildScopeReleasePayloadError(
                        f"wheel contains native extensions: {native!r}"
                    )
                dist_prefixes = {
                    name[: -len("WHEEL")]
                    for name in names
                    if name.endswith(".dist-info/WHEEL")
                }
                if dist_prefixes != {expected_dist}:
                    raise BuildScopeReleasePayloadError(
                        f"wheel dist-info mismatch: {sorted(dist_prefixes)!r}"
                    )
                wheel_text = _zip_read(
                    archive, inventory, expected_dist + "WHEEL", label
                ).decode("utf-8")
                if (
                    "Wheel-Version: 1.0" not in wheel_text.splitlines()
                    or "Root-Is-Purelib: true" not in wheel_text.splitlines()
                    or "Tag: py3-none-any" not in wheel_text.splitlines()
                ):
                    raise BuildScopeReleasePayloadError(
                        "wheel must be pure and tagged py3-none-any"
                    )
                metadata = _zip_read(
                    archive, inventory, expected_dist + "METADATA", label
                )
                _check_package_metadata(metadata, version, "wheel metadata")
                entry_points = _zip_read(
                    archive, inventory, expected_dist + "entry_points.txt", label
                ).decode("utf-8")
                required_entries = [
                    "[console_scripts]",
                    "buildscope = buildscope.__main__:main",
                    "buildscope-diff = buildscope.diff_cli:main",
                ]
                if [
                    line for line in entry_points.splitlines() if line
                ] != required_entries:
                    raise BuildScopeReleasePayloadError(
                        "wheel console entry points are not exact"
                    )
                schemas = sorted(
                    name.removeprefix("buildscope/schemas/")
                    for name in names
                    if name.startswith("buildscope/schemas/")
                    and name.endswith(".schema.json")
                )
                if schemas != list(SCHEMA_NAMES):
                    raise BuildScopeReleasePayloadError(
                        f"wheel schema inventory mismatch: {schemas!r}"
                    )
                _check_wheel_record(
                    archive,
                    inventory,
                    expected_dist + "RECORD",
                )
                return _package_digests_from_zip(archive, inventory)
        except (UnicodeError, zipfile.BadZipFile, RuntimeError) as exc:
            raise BuildScopeReleasePayloadError(
                f"invalid BuildScope wheel: {exc}"
            ) from exc


def check_pyz(path: Path, version: str) -> dict[str, str]:
    label = "BuildScope standalone pyz"
    with _open_regular(path, label) as (stream, _):
        try:
            if stream.read(len(PYZ_SHEBANG)) != PYZ_SHEBANG:
                raise BuildScopeReleasePayloadError(
                    "standalone pyz must contain the exact Python shebang"
                )
            stream.seek(0)
            _zip_preflight(stream, label, prefix=PYZ_SHEBANG)
            with zipfile.ZipFile(stream) as archive:
                inventory = _zip_inventory(archive, label)
                names = set(inventory)
                expected_names = {
                    "__main__.py",
                    *(f"buildscope/{name}" for name in PACKAGE_MODULES),
                    *(f"buildscope/schemas/{name}" for name in SCHEMA_NAMES),
                }
                if names != expected_names:
                    raise BuildScopeReleasePayloadError(
                        "standalone member inventory mismatch: "
                        f"missing={sorted(expected_names - names)!r} "
                        f"extra={sorted(names - expected_names)!r}"
                    )
                schemas = sorted(
                    name.removeprefix("buildscope/schemas/")
                    for name in names
                    if name.startswith("buildscope/schemas/")
                    and name.endswith(".schema.json")
                )
                if schemas != list(SCHEMA_NAMES):
                    raise BuildScopeReleasePayloadError(
                        f"standalone schema inventory mismatch: {schemas!r}"
                    )
                if any(".dist-info/" in name for name in names):
                    raise BuildScopeReleasePayloadError(
                        "standalone pyz must not contain distribution metadata"
                    )
                init_source = _zip_read(
                    archive,
                    inventory,
                    "buildscope/__init__.py",
                    label,
                ).decode("utf-8")
                try:
                    tree = ast.parse(init_source)
                except SyntaxError as exc:
                    raise BuildScopeReleasePayloadError(
                        f"standalone package version source is invalid: {exc}"
                    ) from exc
                try:
                    values = [
                        ast.literal_eval(node.value)
                        for node in tree.body
                        if isinstance(node, ast.Assign)
                        and any(
                            isinstance(target, ast.Name) and target.id == "__version__"
                            for target in node.targets
                        )
                    ]
                except (TypeError, ValueError) as exc:
                    raise BuildScopeReleasePayloadError(
                        f"standalone package version is not a literal: {exc}"
                    ) from exc
                if values != [version]:
                    raise BuildScopeReleasePayloadError(
                        f"standalone package version mismatch: {values!r}"
                    )
                return _package_digests_from_zip(archive, inventory)
        except (zipfile.BadZipFile, RuntimeError) as exc:
            raise BuildScopeReleasePayloadError(
                f"invalid BuildScope standalone pyz: {exc}"
            ) from exc


def _tar_inventory(
    archive: tarfile.TarFile,
    label: str,
) -> dict[str, tarfile.TarInfo]:
    inventory: dict[str, tarfile.TarInfo] = {}
    total = 0
    for count, member in enumerate(archive, start=1):
        canonical_name = member.name.rstrip("/")
        _safe_member_name(canonical_name, label)
        if canonical_name in inventory:
            raise BuildScopeReleasePayloadError(
                f"{label} contains duplicate member {member.name!r}"
            )
        if not (member.isfile() or member.isdir()):
            raise BuildScopeReleasePayloadError(
                f"{label} contains a link or special member: {member.name}"
            )
        inventory[canonical_name] = member
        total = _check_member_budget(count, total, member.size, label)
    if not inventory:
        raise BuildScopeReleasePayloadError(f"{label} is empty")
    return inventory


def _tar_read(
    archive: tarfile.TarFile,
    inventory: dict[str, tarfile.TarInfo],
    name: str,
    label: str,
    *,
    maximum_bytes: int = MAX_METADATA_BYTES,
) -> bytes:
    member = inventory.get(name)
    if member is None or not member.isfile():
        raise BuildScopeReleasePayloadError(f"{label} is missing {name}")
    if member.size <= 0 or member.size > maximum_bytes:
        raise BuildScopeReleasePayloadError(f"{label} member size is invalid: {name}")
    extracted = archive.extractfile(member)
    if extracted is None:
        raise BuildScopeReleasePayloadError(f"{label} member cannot be read: {name}")
    with extracted:
        payload = extracted.read(maximum_bytes + 1)
    if len(payload) != member.size or len(payload) > maximum_bytes:
        raise BuildScopeReleasePayloadError(f"{label} member size changed: {name}")
    return payload


def _tar_digest(
    archive: tarfile.TarFile,
    inventory: dict[str, tarfile.TarInfo],
    name: str,
    label: str,
) -> str:
    member = inventory.get(name)
    if member is None or not member.isfile() or member.size <= 0:
        raise BuildScopeReleasePayloadError(f"{label} is missing {name}")
    extracted = archive.extractfile(member)
    if extracted is None:
        raise BuildScopeReleasePayloadError(f"{label} member cannot be read: {name}")
    digest = hashlib.sha256()
    total = 0
    with extracted:
        while True:
            chunk = extracted.read(HASH_CHUNK_BYTES)
            if not chunk:
                break
            total += len(chunk)
            if total > member.size:
                raise BuildScopeReleasePayloadError(
                    f"{label} member exceeds declared size: {name}"
                )
            digest.update(chunk)
    if total != member.size:
        raise BuildScopeReleasePayloadError(f"{label} member is truncated: {name}")
    return digest.hexdigest()


def _tar_prefix(
    archive: tarfile.TarFile,
    inventory: dict[str, tarfile.TarInfo],
    name: str,
    label: str,
    length: int,
) -> bytes:
    member = inventory.get(name)
    if member is None or not member.isfile() or member.size < length:
        raise BuildScopeReleasePayloadError(f"{label} is missing valid file {name}")
    extracted = archive.extractfile(member)
    if extracted is None:
        raise BuildScopeReleasePayloadError(f"{label} member cannot be read: {name}")
    with extracted:
        payload = extracted.read(length)
    if len(payload) != length:
        raise BuildScopeReleasePayloadError(f"{label} member is truncated: {name}")
    return payload


def check_sdist(path: Path, version: str) -> dict[str, str]:
    label = "BuildScope sdist"
    root = f"buildscope-{version}"
    with _open_regular(path, label) as (stream, _):
        try:
            with tarfile.open(fileobj=stream, mode="r:gz") as archive:
                inventory = _tar_inventory(archive, label)
                names = set(inventory)
                roots = {PurePosixPath(name).parts[0] for name in names}
                if roots != {root}:
                    raise BuildScopeReleasePayloadError(
                        f"sdist root mismatch: {sorted(roots)!r}"
                    )
                native = sorted(name for name in names if _is_native_name(name))
                if native:
                    raise BuildScopeReleasePayloadError(
                        f"sdist contains native extensions: {native!r}"
                    )
                for required in (
                    f"{root}/README.md",
                    f"{root}/docs/quickstart.md",
                    f"{root}/pyproject.toml",
                    f"{root}/python/buildscope/__init__.py",
                    f"{root}/PKG-INFO",
                    f"{root}/examples/cmake/compile_commands.json",
                    f"{root}/examples/cmake/include/buildscope-example/message.hpp",
                    f"{root}/examples/cmake/src/main.cpp",
                    f"{root}/examples/cmake/src/message.cpp",
                    f"{root}/examples/qmake/compile_commands.json",
                    f"{root}/examples/qmake/include/buildscope-example/message.hpp",
                    f"{root}/examples/qmake/src/main.cpp",
                    f"{root}/examples/qmake/src/message.cpp",
                    f"{root}/include/buildscope/contract.hpp",
                    f"{root}/src/core/contract.cpp",
                    f"{root}/src/gui/main_window.cpp",
                    f"{root}/ui/main_window.ui",
                ):
                    if required not in names:
                        raise BuildScopeReleasePayloadError(
                            f"sdist is missing required member: {required}"
                        )
                schemas = sorted(
                    name.removeprefix(f"{root}/schemas/")
                    for name in names
                    if name.startswith(f"{root}/schemas/")
                    and name.endswith(".schema.json")
                )
                if schemas != list(SCHEMA_NAMES):
                    raise BuildScopeReleasePayloadError(
                        f"sdist schema inventory mismatch: {schemas!r}"
                    )
                package_prefix = f"{root}/python/buildscope/"
                package_files = sorted(
                    name.removeprefix(package_prefix)
                    for name in names
                    if name.startswith(package_prefix)
                    and "/" not in name.removeprefix(package_prefix)
                )
                if package_files != list(PACKAGE_MODULES):
                    raise BuildScopeReleasePayloadError(
                        f"sdist package inventory mismatch: {package_files!r}"
                    )
                for forbidden in (f"{root}/setup.py", f"{root}/setup.cfg"):
                    if forbidden in names:
                        raise BuildScopeReleasePayloadError(
                            f"sdist contains a legacy build entry point: {forbidden}"
                        )
                metadata = _tar_read(archive, inventory, f"{root}/PKG-INFO", label)
                _check_package_metadata(metadata, version, "sdist metadata")
                pyproject = _tar_read(
                    archive, inventory, f"{root}/pyproject.toml", label
                ).decode("utf-8")
                for expected_line in (
                    'requires = ["hatchling>=1.27"]',
                    'build-backend = "hatchling.build"',
                    'name = "buildscope"',
                    f'version = "{version}"',
                    'requires-python = ">=3.10"',
                    "dependencies = []",
                    'buildscope = "buildscope.__main__:main"',
                    'buildscope-diff = "buildscope.diff_cli:main"',
                ):
                    if pyproject.splitlines().count(expected_line) != 1:
                        raise BuildScopeReleasePayloadError(
                            f"sdist pyproject contract is missing {expected_line!r}"
                        )
                if "backend-path" in pyproject:
                    raise BuildScopeReleasePayloadError(
                        "sdist pyproject must not redirect its build backend"
                    )
                digests = {
                    f"package/{name}": _tar_digest(
                        archive,
                        inventory,
                        f"{package_prefix}{name}",
                        label,
                    )
                    for name in PACKAGE_MODULES
                }
                digests.update(
                    {
                        f"schema/{name}": _tar_digest(
                            archive,
                            inventory,
                            f"{root}/schemas/{name}",
                            label,
                        )
                        for name in SCHEMA_NAMES
                    }
                )
                return digests
        except (tarfile.TarError, OSError) as exc:
            raise BuildScopeReleasePayloadError(
                f"invalid BuildScope sdist: {exc}"
            ) from exc


def check_bundle(path: Path, dist: Path, version: str) -> dict[str, str]:
    label = "BuildScope Linux bundle"
    root = f"buildscope-{version}-linux-x86_64"
    embedded = (
        "buildscope.pyz",
        f"buildscope-{version}-py3-none-any.whl",
        f"buildscope-{version}.tar.gz",
    )
    with _open_regular(path, label) as (stream, _):
        try:
            with tarfile.open(fileobj=stream, mode="r:gz") as archive:
                inventory = _tar_inventory(archive, label)
                names = set(inventory)
                roots = {PurePosixPath(name).parts[0] for name in names}
                if roots != {root}:
                    raise BuildScopeReleasePayloadError(
                        f"bundle root mismatch: {sorted(roots)!r}"
                    )
                required = (
                    f"{root}/README.runtime.md",
                    f"{root}/bin/buildscope-cli",
                    f"{root}/bin/buildscope-gui",
                    f"{root}/share/doc/buildscope/README.md",
                    f"{root}/share/doc/buildscope/quickstart.md",
                    f"{root}/share/buildscope/examples/cmake/CMakeLists.txt",
                    f"{root}/share/buildscope/examples/cmake/compile_commands.json",
                    f"{root}/share/buildscope/examples/cmake/include/buildscope-example/message.hpp",
                    f"{root}/share/buildscope/examples/cmake/src/main.cpp",
                    f"{root}/share/buildscope/examples/cmake/src/message.cpp",
                    f"{root}/share/buildscope/examples/qmake/example.pro",
                    f"{root}/share/buildscope/examples/qmake/compile_commands.json",
                    f"{root}/share/buildscope/examples/qmake/include/buildscope-example/message.hpp",
                    f"{root}/share/buildscope/examples/qmake/src/main.cpp",
                    f"{root}/share/buildscope/examples/qmake/src/message.cpp",
                )
                for name in required:
                    if name not in names:
                        raise BuildScopeReleasePayloadError(
                            f"bundle is missing required member: {name}"
                        )
                expected_files = {
                    *required,
                    *(
                        f"{root}/share/buildscope/schemas/{name}"
                        for name in SCHEMA_NAMES
                    ),
                    *(f"{root}/{name}" for name in embedded),
                }
                actual_files = {
                    name for name, member in inventory.items() if member.isfile()
                }
                if actual_files != expected_files:
                    raise BuildScopeReleasePayloadError(
                        "bundle file inventory mismatch: "
                        f"missing={sorted(expected_files - actual_files)!r} "
                        f"extra={sorted(actual_files - expected_files)!r}"
                    )
                schemas = sorted(
                    name.removeprefix(f"{root}/share/buildscope/schemas/")
                    for name in names
                    if name.startswith(f"{root}/share/buildscope/schemas/")
                    and name.endswith(".schema.json")
                )
                if schemas != list(SCHEMA_NAMES):
                    raise BuildScopeReleasePayloadError(
                        f"bundle schema inventory mismatch: {schemas!r}"
                    )
                unexpected_native = sorted(
                    name for name in names if _is_native_name(name)
                )
                if unexpected_native:
                    raise BuildScopeReleasePayloadError(
                        f"bundle contains unexpected native libraries: {unexpected_native!r}"
                    )
                for binary in ("buildscope-cli", "buildscope-gui"):
                    name = f"{root}/bin/{binary}"
                    member = inventory[name]
                    if not member.isfile() or member.mode & 0o111 == 0:
                        raise BuildScopeReleasePayloadError(
                            f"bundle binary is not executable: {name}"
                        )
                    header = _tar_prefix(archive, inventory, name, label, 20)
                    if (
                        len(header) < 20
                        or header[:4] != b"\x7fELF"
                        or header[4:6] != b"\x02\x01"
                        or int.from_bytes(header[18:20], "little") != 62
                    ):
                        raise BuildScopeReleasePayloadError(
                            f"bundle binary is not ELF64 x86-64: {name}"
                        )
                runtime = _tar_read(
                    archive, inventory, f"{root}/README.runtime.md", label
                ).decode("utf-8")
                if f"BuildScope {version}" not in runtime or "Qt" not in runtime:
                    raise BuildScopeReleasePayloadError(
                        "bundle runtime documentation has the wrong identity"
                    )
                for name in embedded:
                    bundled_name = f"{root}/{name}"
                    if _tar_digest(
                        archive, inventory, bundled_name, label
                    ) != _stream_digest(dist / name, f"top-level {name}"):
                        raise BuildScopeReleasePayloadError(
                            f"bundle embedded artifact differs from top-level asset: {name}"
                        )
                return _schema_digests_from_tar(
                    archive,
                    inventory,
                    f"{root}/share/buildscope/schemas/",
                    label,
                )
        except (UnicodeError, tarfile.TarError, OSError) as exc:
            raise BuildScopeReleasePayloadError(
                f"invalid BuildScope bundle: {exc}"
            ) from exc


def _require_exact_keys(value: dict[str, Any], expected: set[str], label: str) -> None:
    actual = set(value)
    if actual != expected:
        raise BuildScopeReleasePayloadError(
            f"{label} keys mismatch: missing={sorted(expected - actual)!r} "
            f"extra={sorted(actual - expected)!r}"
        )


def check_provenance(
    path: Path,
    *,
    version: str,
    tag: str,
    target_sha: str,
    main_sha: str,
    merge_gate_url: str,
    repository: str,
    ici_version: str,
    ici_sha256: str,
) -> None:
    payload = _read_json(path, "BuildScope provenance")
    _require_exact_keys(
        payload,
        {
            "product",
            "version",
            "tag",
            "target_commit",
            "exact_main_commit",
            "merge_gate_check",
            "workflow",
            "runner",
            "ici",
        },
        "provenance",
    )
    expected_scalars = {
        "product": "buildscope",
        "version": version,
        "tag": tag,
        "target_commit": target_sha,
        "exact_main_commit": main_sha,
        "merge_gate_check": merge_gate_url,
    }
    for name, expected in expected_scalars.items():
        if payload.get(name) != expected:
            raise BuildScopeReleasePayloadError(
                f"provenance {name} mismatch: {payload.get(name)!r} != {expected!r}"
            )

    workflow = payload.get("workflow")
    runner = payload.get("runner")
    ici = payload.get("ici")
    if (
        not isinstance(workflow, dict)
        or not isinstance(runner, dict)
        or not isinstance(ici, dict)
    ):
        raise BuildScopeReleasePayloadError(
            "provenance workflow, runner, and ici fields must be objects"
        )
    _require_exact_keys(
        workflow,
        {"name", "run_id", "server", "repository"},
        "provenance workflow",
    )
    _require_exact_keys(runner, {"os", "architecture"}, "provenance runner")
    _require_exact_keys(ici, {"version", "asset", "sha256"}, "provenance ici")
    if workflow.get("name") != "BuildScope Release":
        raise BuildScopeReleasePayloadError("provenance workflow name is invalid")
    run_id = workflow.get("run_id")
    if not isinstance(run_id, str) or RUN_ID_PATTERN.fullmatch(run_id) is None:
        raise BuildScopeReleasePayloadError(
            f"provenance workflow run_id is invalid: {run_id!r}"
        )
    if (
        workflow.get("server") != "https://github.com"
        or workflow.get("repository") != repository
    ):
        raise BuildScopeReleasePayloadError("provenance workflow origin is invalid")
    if runner != {"os": "linux", "architecture": "x86_64"}:
        raise BuildScopeReleasePayloadError(f"provenance runner is invalid: {runner!r}")
    if ici != {
        "version": ici_version,
        "asset": "ici.pyz",
        "sha256": ici_sha256,
    }:
        raise BuildScopeReleasePayloadError(
            f"provenance ici identity is invalid: {ici!r}"
        )


def check_release_payload(
    dist: Path,
    version: str,
    tag: str,
    target_sha: str,
    main_sha: str,
    merge_gate_url: str,
    repository: str,
    ici_version: str,
    ici_sha256: str,
) -> None:
    if VERSION_PATTERN.fullmatch(version) is None or tag != f"buildscope-v{version}":
        raise BuildScopeReleasePayloadError("BuildScope version and tag do not agree")
    if (
        SHA_PATTERN.fullmatch(target_sha) is None
        or SHA_PATTERN.fullmatch(main_sha) is None
    ):
        raise BuildScopeReleasePayloadError(
            "target and main commits must be full lowercase SHAs"
        )
    if target_sha != main_sha:
        raise BuildScopeReleasePayloadError("release target must equal exact main")
    if REPOSITORY_PATTERN.fullmatch(repository) is None:
        raise BuildScopeReleasePayloadError(f"invalid repository: {repository!r}")
    merge_gate_pattern = re.compile(
        rf"^https://github\.com/{re.escape(repository)}/actions/runs/"
        r"[1-9][0-9]{0,19}(?:/job/[1-9][0-9]{0,19})?$"
    )
    if merge_gate_pattern.fullmatch(merge_gate_url) is None:
        raise BuildScopeReleasePayloadError(
            f"invalid Merge Gate URL: {merge_gate_url!r}"
        )
    if (
        ICI_VERSION_PATTERN.fullmatch(ici_version) is None
        or SHA256_PATTERN.fullmatch(ici_sha256) is None
    ):
        raise BuildScopeReleasePayloadError("invalid ici release identity")
    try:
        directory_info = dist.stat(follow_symlinks=False)
    except OSError as exc:
        raise BuildScopeReleasePayloadError(
            f"release directory cannot be inspected: {exc}"
        ) from exc
    if not stat.S_ISDIR(directory_info.st_mode):
        raise BuildScopeReleasePayloadError(
            "release directory must be a real directory"
        )

    expected_files = {
        "buildscope.pyz",
        "buildscope.pyz.sha256",
        f"buildscope-{version}-py3-none-any.whl",
        f"buildscope-{version}.tar.gz",
        "buildscope-ici-deep.json",
        "buildscope-ici-deep.html",
        "buildscope-provenance.json",
        f"buildscope-{version}-linux-x86_64.tar.gz",
        "SHA256SUMS",
    }
    try:
        entries = list(os.scandir(dist))
    except OSError as exc:
        raise BuildScopeReleasePayloadError(
            f"release directory cannot be listed: {exc}"
        ) from exc
    actual_files = {entry.name for entry in entries}
    if actual_files != expected_files or any(
        not entry.is_file(follow_symlinks=False) for entry in entries
    ):
        raise BuildScopeReleasePayloadError(
            "release directory inventory mismatch: "
            f"missing={sorted(expected_files - actual_files)!r} "
            f"extra={sorted(actual_files - expected_files)!r}"
        )

    pyz_payload = check_pyz(dist / "buildscope.pyz", version)
    wheel_payload = check_wheel(
        dist / f"buildscope-{version}-py3-none-any.whl", version
    )
    sdist_payload = check_sdist(dist / f"buildscope-{version}.tar.gz", version)
    bundle_schemas = check_bundle(
        dist / f"buildscope-{version}-linux-x86_64.tar.gz",
        dist,
        version,
    )
    if not (pyz_payload == wheel_payload == sdist_payload):
        raise BuildScopeReleasePayloadError(
            "published package or schema bytes disagree across pyz, wheel, and sdist"
        )
    package_schema_subset = {
        key.removeprefix("schema/"): value
        for key, value in pyz_payload.items()
        if key.startswith("schema/")
    }
    if package_schema_subset != bundle_schemas:
        raise BuildScopeReleasePayloadError(
            "published schema bytes disagree with the Linux bundle"
        )

    deep_report = _read_json(
        dist / "buildscope-ici-deep.json", "BuildScope deep report"
    )
    try:
        validate_report(deep_report, expected_qt_major=6)
        check_html_report(dist / "buildscope-ici-deep.html", "buildscope")
    except (BuildScopeB5ReportError, PublishedHtmlError) as exc:
        raise BuildScopeReleasePayloadError(
            f"BuildScope deep report contract failed: {exc}"
        ) from exc
    check_provenance(
        dist / "buildscope-provenance.json",
        version=version,
        tag=tag,
        target_sha=target_sha,
        main_sha=main_sha,
        merge_gate_url=merge_gate_url,
        repository=repository,
        ici_version=ici_version,
        ici_sha256=ici_sha256,
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dist", type=Path)
    parser.add_argument("version")
    parser.add_argument("tag")
    parser.add_argument("target_sha")
    parser.add_argument("main_sha")
    parser.add_argument("merge_gate_url")
    parser.add_argument("repository")
    parser.add_argument("ici_version")
    parser.add_argument("ici_sha256")
    args = parser.parse_args(argv)
    try:
        check_release_payload(
            args.dist,
            args.version,
            args.tag,
            args.target_sha,
            args.main_sha,
            args.merge_gate_url,
            args.repository,
            args.ici_version,
            args.ici_sha256,
        )
    except BuildScopeReleasePayloadError as exc:
        parser.exit(1, f"BuildScope release payload audit failed: {exc}{os.linesep}")
    print(f"audited BuildScope {args.version}: provenance and archive contracts")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
