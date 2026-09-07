#!/usr/bin/env python3
"""Filesystem and version primitives shared by every product's release audit.

These checks are about *how* a release asset is read, not about which product
produced it: refuse to follow a symlink at the final path component, refuse a
FIFO without blocking on open(2), and confirm the file did not change identity
between the stat that authorized the read and the read itself. Nothing here
knows a product name, so BuildScope's audit and every product added after it
enforce the same contract rather than a re-typed approximation of it.

Dependency-free standard library only: the release workflow runs these before
any toolchain setup.
"""

from __future__ import annotations

import errno
import hashlib
import json
import math
import os
import re
import stat
from pathlib import Path
from typing import Any

MAX_RELEASE_JSON_BYTES = 20_000_000
HASH_CHUNK_BYTES = 1024 * 1024
MAX_GITHUB_ID = (1 << 63) - 1
MAX_ASSET_NAME_BYTES = 255
MAX_ASSET_BYTES = 256 * 1024 * 1024
MAX_TOTAL_ASSET_BYTES = 512 * 1024 * 1024
VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
SHA256_DIGEST_PATTERN = re.compile(r"^sha256:[0-9a-f]{64}$")

_NOFOLLOW = getattr(os, "O_NOFOLLOW", None)
_CLOEXEC = getattr(os, "O_CLOEXEC", 0)
_DIRECTORY = getattr(os, "O_DIRECTORY", 0)
_NONBLOCK = getattr(os, "O_NONBLOCK", 0)


class ReleaseAssetError(ValueError):
    """A release or one of its assets is invalid."""


def _open_flags(*, directory: bool = False) -> int:
    """Return descriptor flags that never follow the final path component."""

    if _NOFOLLOW is None:
        raise ReleaseAssetError(
            "this platform does not provide O_NOFOLLOW for safe release audits"
        )
    flags = os.O_RDONLY | _CLOEXEC | _NOFOLLOW
    if not directory:
        # A FIFO must be rejected after fstat without allowing open(2) to block.
        flags |= _NONBLOCK
    if directory:
        if not _DIRECTORY:
            raise ReleaseAssetError(
                "this platform does not provide O_DIRECTORY for safe directory audits"
            )
        flags |= _DIRECTORY
    return flags


def _stat_signature(info: os.stat_result) -> tuple[int, ...]:
    """Return metadata that must stay stable while a file is being consumed."""

    return (
        info.st_dev,
        info.st_ino,
        stat.S_IFMT(info.st_mode),
        info.st_nlink,
        info.st_size,
        info.st_mtime_ns,
        info.st_ctime_ns,
    )


def _assert_path_matches(path: Path, label: str, initial_info: os.stat_result) -> None:
    """Reject a path that was replaced while its descriptor was consumed."""

    try:
        final_info = os.stat(path, follow_symlinks=False)
    except OSError as exc:
        raise ReleaseAssetError(
            f"{label} disappeared while it was being audited: {exc}"
        ) from exc
    if _stat_signature(final_info) != _stat_signature(initial_info):
        raise ReleaseAssetError(f"{label} changed while it was being audited")


def _open_regular_file(path: Path, label: str) -> tuple[int, os.stat_result]:
    """Open a regular file without following a symlink and return its first stat."""

    try:
        fd = os.open(path, _open_flags())
    except ReleaseAssetError:
        raise
    except OSError as exc:
        if exc.errno == errno.ELOOP:
            raise ReleaseAssetError(
                f"{label} must be a regular file (symlinks are not allowed)"
            ) from exc
        raise ReleaseAssetError(
            f"{label} cannot be inspected: {exc}"
        ) from exc
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise ReleaseAssetError(f"{label} must be a regular file")
        return fd, info
    except ReleaseAssetError:
        os.close(fd)
        raise
    except OSError as exc:
        os.close(fd)
        raise ReleaseAssetError(
            f"{label} cannot be inspected: {exc}"
        ) from exc


def _open_real_directory(path: Path, label: str) -> tuple[int, os.stat_result]:
    """Open a directory descriptor without following its final symlink."""

    try:
        fd = os.open(path, _open_flags(directory=True))
    except ReleaseAssetError:
        raise
    except OSError as exc:
        if exc.errno in {errno.ELOOP, errno.ENOTDIR}:
            raise ReleaseAssetError(
                f"{label} must be a real directory (symlinks are not allowed)"
            ) from exc
        raise ReleaseAssetError(
            f"{label} cannot be inspected: {exc}"
        ) from exc
    try:
        info = os.fstat(fd)
        if not stat.S_ISDIR(info.st_mode):
            raise ReleaseAssetError(f"{label} must be a real directory")
        return fd, info
    except ReleaseAssetError:
        os.close(fd)
        raise
    except OSError as exc:
        os.close(fd)
        raise ReleaseAssetError(
            f"{label} cannot be inspected: {exc}"
        ) from exc


def _regular_file_info(path: Path, label: str) -> os.stat_result:
    fd, info = _open_regular_file(path, label)
    try:
        _assert_path_matches(path, label, info)
    finally:
        os.close(fd)
    return info


def _require_regular_directory(path: Path, label: str) -> None:
    fd, info = _open_real_directory(path, label)
    try:
        _assert_path_matches(path, label, info)
    finally:
        os.close(fd)


def _read_release_json(path: Path) -> dict[str, Any]:
    fd, info = _open_regular_file(path, "release metadata")
    if info.st_size <= 0 or info.st_size > MAX_RELEASE_JSON_BYTES:
        os.close(fd)
        raise ReleaseAssetError(
            "release metadata size is outside the accepted range: "
            f"{info.st_size} bytes (maximum {MAX_RELEASE_JSON_BYTES})"
        )

    try:
        with os.fdopen(fd, "rb", closefd=True) as stream:
            payload = stream.read(MAX_RELEASE_JSON_BYTES + 1)
            final_info = os.fstat(stream.fileno())
            if _stat_signature(final_info) != _stat_signature(info):
                raise ReleaseAssetError(
                    "release metadata changed while it was being audited"
                )
            _assert_path_matches(path, "release metadata", info)
    except ReleaseAssetError:
        raise
    except OSError as exc:
        raise ReleaseAssetError(
            f"release metadata cannot be read: {exc}"
        ) from exc
    if len(payload) > MAX_RELEASE_JSON_BYTES:
        raise ReleaseAssetError(
            "release metadata exceeds the accepted bound "
            f"of {MAX_RELEASE_JSON_BYTES} bytes"
        )
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ReleaseAssetError(
            f"release metadata is not valid UTF-8: {exc}"
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
        value: dict[str, Any] = {}
        for key, item in pairs:
            if key in value:
                raise ValueError(f"duplicate JSON key: {key}")
            value[key] = item
        return value

    try:
        release = json.loads(
            text,
            parse_int=bounded_int,
            parse_float=bounded_float,
            parse_constant=reject_constant,
            object_pairs_hook=unique_object,
        )
    except (json.JSONDecodeError, ValueError) as exc:
        raise ReleaseAssetError(
            f"release metadata is not valid JSON: {exc}"
        ) from exc
    if not isinstance(release, dict):
        raise ReleaseAssetError("release metadata must be a JSON object")
    return release


def _stream_sha256(
    path: Path, label: str, initial_info: os.stat_result
) -> tuple[int, str]:
    """Read ``path`` in bounded chunks and return its byte count and SHA-256."""

    digest = hashlib.sha256()
    total = 0
    fd, opened_info = _open_regular_file(path, label)
    if _stat_signature(opened_info) != _stat_signature(initial_info):
        os.close(fd)
        raise ReleaseAssetError(f"{label} changed while it was being audited")
    try:
        with os.fdopen(fd, "rb", closefd=True) as stream:
            while True:
                chunk = stream.read(HASH_CHUNK_BYTES)
                if not chunk:
                    break
                digest.update(chunk)
                total += len(chunk)
            final_info = os.fstat(stream.fileno())
            if _stat_signature(final_info) != _stat_signature(opened_info):
                raise ReleaseAssetError(
                    f"{label} changed while it was being audited"
                )
            _assert_path_matches(path, label, opened_info)
    except ReleaseAssetError:
        raise
    except OSError as exc:
        raise ReleaseAssetError(f"{label} cannot be read: {exc}") from exc
    return total, digest.hexdigest()


def validate_version(version: str, tag: str, tag_prefix: str, product: str) -> None:
    """Confirm ``version`` is a release version and ``tag`` is its exact tag.

    ``tag_prefix`` carries the product identity, so a tag that names a
    different product than the one being audited fails here rather than
    passing an audit that never looked at the name. ``product`` is the display
    name used in the message, so a failure in the shared module still says
    which product's release was being audited.
    """

    if VERSION_PATTERN.fullmatch(version) is None:
        raise ReleaseAssetError(f"invalid {product} version: {version!r}")
    expected_tag = f"{tag_prefix}{version}"
    if tag != expected_tag:
        raise ReleaseAssetError(
            f"release tag argument does not match version: expected {expected_tag!r}, got {tag!r}"
        )

