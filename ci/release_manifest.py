#!/usr/bin/env python3
"""Validate a product's exact release checksum manifest before upload.

`SHA256SUMS` is the file a consumer runs `sha256sum --check` against, so it has
to cover every asset the product publishes except itself, in the declared
order, with digests that match the bytes on disk.

Nothing here is product-specific. The covered names come from the product's own
declaration in `ci/projects.json`. The `.pyz.sha256` sidecar is checked only for
a product that declares a pyz — requiring it everywhere would fail four
products that correctly do not ship one.
"""

from __future__ import annotations

import argparse
import os
import re
from collections.abc import Sequence
from pathlib import Path

from release_assets import _spec, expected_asset_names
from release_audit import (
    ReleaseAssetError,
    _assert_path_matches,
    _open_regular_file,
    _regular_file_info,
    _require_regular_directory,
    _stat_signature,
    _stream_sha256,
    validate_version,
)

MAX_MANIFEST_BYTES = 128 * 1024
MAX_SIDECAR_BYTES = 1024
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


def manifest_asset_names(
    product: str, version: str, repo_root: Path | None = None
) -> tuple[str, ...]:
    """Return the files ``SHA256SUMS`` covers, in order, excluding itself."""

    names = expected_asset_names(product, version, repo_root)
    if names[-1] != "SHA256SUMS":
        raise ReleaseAssetError("internal manifest asset order is invalid")
    return names[:-1]


def _read_bounded_regular_file(
    path: Path,
    label: str,
    maximum_bytes: int,
) -> bytes:
    fd, info = _open_regular_file(path, label)
    if info.st_size <= 0 or info.st_size > maximum_bytes:
        os.close(fd)
        raise ReleaseAssetError(
            f"{label} size is outside the accepted range: {info.st_size} bytes "
            f"(maximum {maximum_bytes})"
        )
    try:
        with os.fdopen(fd, "rb", closefd=True) as stream:
            payload = stream.read(maximum_bytes + 1)
            final_info = os.fstat(stream.fileno())
            if _stat_signature(final_info) != _stat_signature(info):
                raise ReleaseAssetError(
                    f"{label} changed while it was being audited"
                )
            _assert_path_matches(path, label, info)
    except ReleaseAssetError:
        raise
    except OSError as exc:
        raise ReleaseAssetError(f"{label} cannot be read: {exc}") from exc
    if len(payload) > maximum_bytes:
        raise ReleaseAssetError(
            f"{label} exceeds the accepted bound of {maximum_bytes} bytes"
        )
    return payload


def _decode_checksum_file(path: Path, label: str, maximum_bytes: int) -> str:
    payload = _read_bounded_regular_file(path, label, maximum_bytes)
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ReleaseAssetError(f"{label} is not valid UTF-8: {exc}") from exc
    if "\x00" in text or "\r" in text:
        raise ReleaseAssetError(f"{label} contains forbidden control bytes")
    if not text.endswith("\n"):
        raise ReleaseAssetError(f"{label} must end with one newline")
    return text


def _parse_manifest(
    text: str, product: str, version: str, repo_root: Path | None = None
) -> dict[str, str]:
    expected_names = manifest_asset_names(product, version, repo_root)
    # ``splitlines`` accepts Unicode separators such as U+2028.  Release
    # manifests are byte-oriented sha256sum files and permit LF only.
    lines_with_sentinel = text.split("\n")
    if not lines_with_sentinel or lines_with_sentinel[-1] != "":
        raise ReleaseAssetError("SHA256SUMS must end with one newline")
    lines = lines_with_sentinel[:-1]
    if len(lines) != len(expected_names):
        raise ReleaseAssetError(
            f"SHA256SUMS entry count mismatch: expected={len(expected_names)} "
            f"actual={len(lines)}"
        )

    parsed: dict[str, str] = {}
    for index, (line, expected_name) in enumerate(
        zip(lines, expected_names, strict=True),
        start=1,
    ):
        if len(line) < 67 or line[64:66] != "  ":
            raise ReleaseAssetError(
                f"SHA256SUMS line {index} has invalid sha256sum syntax"
            )
        digest = line[:64]
        name = line[66:]
        if SHA256_PATTERN.fullmatch(digest) is None:
            raise ReleaseAssetError(
                f"SHA256SUMS line {index} has an invalid SHA-256 digest"
            )
        if name != expected_name:
            raise ReleaseAssetError(
                f"SHA256SUMS line {index} name mismatch: {name!r} != {expected_name!r}"
            )
        if name in parsed:
            raise ReleaseAssetError(
                f"SHA256SUMS contains a duplicate name: {name}"
            )
        parsed[name] = digest
    return parsed


def check_release_manifest(
    dist: Path, product: str, version: str, repo_root: Path | None = None
) -> None:
    """Raise unless the manifest and every file it covers agree exactly."""

    spec = _spec(product, repo_root)
    validate_version(version, f"{spec.tag_prefix}{version}", spec.tag_prefix, spec.display_name)
    _require_regular_directory(dist, "release asset directory")
    manifest_text = _decode_checksum_file(
        dist / "SHA256SUMS",
        "SHA256SUMS",
        MAX_MANIFEST_BYTES,
    )
    expected_digests = _parse_manifest(manifest_text, product, version, repo_root)

    actual_digests: dict[str, str] = {}
    for name in manifest_asset_names(product, version, repo_root):
        local = dist / name
        info = _regular_file_info(local, f"manifest asset {name}")
        if info.st_size <= 0:
            raise ReleaseAssetError(
                f"manifest asset must not be empty: {name}"
            )
        _, digest = _stream_sha256(local, f"manifest asset {name}", info)
        actual_digests[name] = digest
        if digest != expected_digests[name]:
            raise ReleaseAssetError(
                f"SHA256SUMS digest mismatch: {name}: "
                f"{expected_digests[name]} != {digest}"
            )

    # Only a product that ships a standalone zipapp has a sidecar to check.
    # Demanding one from a native-bundle product would fail a correct release.
    sidecar_name = f"{product}.pyz.sha256"
    if sidecar_name in actual_digests:
        sidecar = _decode_checksum_file(
            dist / sidecar_name, sidecar_name, MAX_SIDECAR_BYTES
        )
        expected_sidecar = f"{actual_digests[f'{product}.pyz']}  {product}.pyz\n"
        if sidecar != expected_sidecar:
            raise ReleaseAssetError(
                f"{sidecar_name} must contain exactly the standalone pyz digest"
            )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dist", type=Path)
    parser.add_argument("product")
    parser.add_argument("version")
    args = parser.parse_args(argv)
    try:
        check_release_manifest(args.dist, args.product, args.version)
    except ReleaseAssetError as exc:
        parser.exit(1, f"{args.product} release manifest audit failed: {exc}{os.linesep}")
    covered = manifest_asset_names(args.product, args.version)
    print(
        f"audited {args.product} {args.version}: "
        f"exact {len(covered)}-entry SHA256SUMS"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
