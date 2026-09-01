#!/usr/bin/env python3
"""Validate BuildScope's exact release checksum manifest and sidecar."""

from __future__ import annotations

import argparse
import os
import re
from collections.abc import Sequence
from pathlib import Path

from check_buildscope_release_assets import (
    BuildScopeReleaseAssetError,
    _regular_file_info,
    _require_regular_directory,
    _stream_sha256,
    _validate_version,
    expected_asset_names,
)

MAX_MANIFEST_BYTES = 128 * 1024
MAX_SIDECAR_BYTES = 1024
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


def manifest_asset_names(version: str) -> tuple[str, ...]:
    """Return the exact eight files covered by ``SHA256SUMS`` in order."""

    names = expected_asset_names(version)
    if names[-1] != "SHA256SUMS":
        raise BuildScopeReleaseAssetError("internal manifest asset order is invalid")
    return names[:-1]


def _read_bounded_regular_file(
    path: Path,
    label: str,
    maximum_bytes: int,
) -> bytes:
    info = _regular_file_info(path, label)
    if info.st_size <= 0 or info.st_size > maximum_bytes:
        raise BuildScopeReleaseAssetError(
            f"{label} size is outside the accepted range: {info.st_size} bytes "
            f"(maximum {maximum_bytes})"
        )
    try:
        with path.open("rb") as stream:
            opened_info = os.fstat(stream.fileno())
            if opened_info.st_dev != info.st_dev or opened_info.st_ino != info.st_ino:
                raise BuildScopeReleaseAssetError(
                    f"{label} changed while it was being audited"
                )
            payload = stream.read(maximum_bytes + 1)
    except BuildScopeReleaseAssetError:
        raise
    except OSError as exc:
        raise BuildScopeReleaseAssetError(f"{label} cannot be read: {exc}") from exc
    if len(payload) > maximum_bytes:
        raise BuildScopeReleaseAssetError(
            f"{label} exceeds the accepted bound of {maximum_bytes} bytes"
        )
    return payload


def _decode_checksum_file(path: Path, label: str, maximum_bytes: int) -> str:
    payload = _read_bounded_regular_file(path, label, maximum_bytes)
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise BuildScopeReleaseAssetError(f"{label} is not valid UTF-8: {exc}") from exc
    if "\x00" in text or "\r" in text:
        raise BuildScopeReleaseAssetError(f"{label} contains forbidden control bytes")
    if not text.endswith("\n"):
        raise BuildScopeReleaseAssetError(f"{label} must end with one newline")
    return text


def _parse_manifest(text: str, version: str) -> dict[str, str]:
    expected_names = manifest_asset_names(version)
    lines = text.splitlines()
    if len(lines) != len(expected_names):
        raise BuildScopeReleaseAssetError(
            f"SHA256SUMS entry count mismatch: expected={len(expected_names)} "
            f"actual={len(lines)}"
        )

    parsed: dict[str, str] = {}
    for index, (line, expected_name) in enumerate(
        zip(lines, expected_names, strict=True),
        start=1,
    ):
        if len(line) < 67 or line[64:66] != "  ":
            raise BuildScopeReleaseAssetError(
                f"SHA256SUMS line {index} has invalid sha256sum syntax"
            )
        digest = line[:64]
        name = line[66:]
        if SHA256_PATTERN.fullmatch(digest) is None:
            raise BuildScopeReleaseAssetError(
                f"SHA256SUMS line {index} has an invalid SHA-256 digest"
            )
        if name != expected_name:
            raise BuildScopeReleaseAssetError(
                f"SHA256SUMS line {index} name mismatch: {name!r} != {expected_name!r}"
            )
        if name in parsed:
            raise BuildScopeReleaseAssetError(
                f"SHA256SUMS contains a duplicate name: {name}"
            )
        parsed[name] = digest
    return parsed


def check_release_manifest(dist: Path, version: str) -> None:
    """Raise unless the exact manifest, sidecar, and eight files agree."""

    _validate_version(version, f"buildscope-v{version}")
    _require_regular_directory(dist, "release asset directory")
    manifest_text = _decode_checksum_file(
        dist / "SHA256SUMS",
        "SHA256SUMS",
        MAX_MANIFEST_BYTES,
    )
    expected_digests = _parse_manifest(manifest_text, version)

    actual_digests: dict[str, str] = {}
    for name in manifest_asset_names(version):
        local = dist / name
        info = _regular_file_info(local, f"manifest asset {name}")
        if info.st_size <= 0:
            raise BuildScopeReleaseAssetError(
                f"manifest asset must not be empty: {name}"
            )
        _, digest = _stream_sha256(local, f"manifest asset {name}", info)
        actual_digests[name] = digest
        if digest != expected_digests[name]:
            raise BuildScopeReleaseAssetError(
                f"SHA256SUMS digest mismatch: {name}: "
                f"{expected_digests[name]} != {digest}"
            )

    sidecar = _decode_checksum_file(
        dist / "buildscope.pyz.sha256",
        "buildscope.pyz.sha256",
        MAX_SIDECAR_BYTES,
    )
    expected_sidecar = f"{actual_digests['buildscope.pyz']}  buildscope.pyz\n"
    if sidecar != expected_sidecar:
        raise BuildScopeReleaseAssetError(
            "buildscope.pyz.sha256 must contain exactly the standalone pyz digest"
        )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dist", type=Path)
    parser.add_argument("version")
    args = parser.parse_args(argv)
    try:
        check_release_manifest(args.dist, args.version)
    except BuildScopeReleaseAssetError as exc:
        parser.exit(1, f"BuildScope release manifest audit failed: {exc}{os.linesep}")
    print(
        f"audited BuildScope {args.version}: exact 8-entry SHA256SUMS and pyz sidecar"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
