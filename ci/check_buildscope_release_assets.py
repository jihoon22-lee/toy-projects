#!/usr/bin/env python3
"""Audit the final BuildScope GitHub Release and its downloaded assets.

After publication, the release workflow compares the GitHub API response with
the same nine local artifacts that it uploaded. Keeping this dependency-free
standard-library audit in a normal Python module makes the final-publication
contract testable on every supported interpreter and prevents the workflow
from silently drifting away from its unit-tested checks.

The filesystem and version primitives live in ``release_audit`` so every
product's release enforces the same read contract; only the asset list and the
tag prefix below are BuildScope's own.
"""

from __future__ import annotations

import argparse
import json
import math
import os
from collections.abc import Sequence
from pathlib import Path
from typing import Any

from release_audit import (
    HASH_CHUNK_BYTES,
    MAX_ASSET_BYTES,
    MAX_ASSET_NAME_BYTES,
    MAX_GITHUB_ID,
    MAX_RELEASE_JSON_BYTES,
    MAX_TOTAL_ASSET_BYTES,
    SHA256_DIGEST_PATTERN,
    VERSION_PATTERN,
    ReleaseAssetError,
    _assert_path_matches,
    _open_real_directory,
    _open_regular_file,
    _read_release_json,
    _regular_file_info,
    _require_regular_directory,
    _stat_signature,
    _stream_sha256,
    validate_version,
)

TAG_PREFIX = "buildscope-v"
PRODUCT = "BuildScope"

# Kept as an alias: this module's public errors have always carried this name,
# and callers catch it by name.
BuildScopeReleaseAssetError = ReleaseAssetError


def expected_asset_names(version: str) -> tuple[str, ...]:
    """Return the exact nine asset names required for ``version``."""

    return (
        "buildscope.pyz",
        "buildscope.pyz.sha256",
        f"buildscope-{version}-py3-none-any.whl",
        f"buildscope-{version}.tar.gz",
        "buildscope-ici-deep.json",
        "buildscope-ici-deep.html",
        "buildscope-provenance.json",
        f"buildscope-{version}-linux-x86_64.tar.gz",
        "SHA256SUMS",
    )


def _validate_version(version: str, tag: str) -> None:
    validate_version(version, tag, TAG_PREFIX, PRODUCT)



def load_release_assets(
    release_json: Path,
    tag: str,
    version: str,
    *,
    stage: str = "final",
) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    """Load and validate release metadata before any asset bytes are trusted."""

    _validate_version(version, tag)
    if stage not in {"draft", "final"}:
        raise BuildScopeReleaseAssetError(f"invalid release stage: {stage!r}")
    release = _read_release_json(release_json)

    release_id = release.get("id")
    if (
        isinstance(release_id, bool)
        or not isinstance(release_id, int)
        or not 0 < release_id <= MAX_GITHUB_ID
    ):
        raise BuildScopeReleaseAssetError(f"release id is invalid: {release_id!r}")
    release_tag = release.get("tag_name")
    if release_tag != tag:
        raise BuildScopeReleaseAssetError(
            f"release tag mismatch: {release_tag!r} != {tag!r}"
        )
    release_name = release.get("name")
    expected_name = f"BuildScope {version}"
    if release_name != expected_name:
        raise BuildScopeReleaseAssetError(
            f"release name mismatch: {release_name!r} != {expected_name!r}"
        )

    expected_draft = stage == "draft"
    if release.get("draft") is not expected_draft:
        raise BuildScopeReleaseAssetError(
            f"BuildScope {stage} audit requires draft={expected_draft!r}"
        )
    if release.get("prerelease") is not False:
        raise BuildScopeReleaseAssetError(
            "BuildScope product release must not be a prerelease"
        )
    published_at = release.get("published_at")
    if stage == "draft":
        if published_at is not None:
            raise BuildScopeReleaseAssetError(
                f"draft release must not have published_at: {published_at!r}"
            )
    elif not isinstance(published_at, str) or not published_at.strip():
        raise BuildScopeReleaseAssetError(
            f"final release must have published_at: {published_at!r}"
        )

    assets = release.get("assets")
    if not isinstance(assets, list):
        raise BuildScopeReleaseAssetError(
            "release assets are unavailable or not a JSON array"
        )

    expected = set(expected_asset_names(version))
    if len(assets) != len(expected):
        raise BuildScopeReleaseAssetError(
            f"public asset count mismatch: expected={len(expected)} actual={len(assets)}"
        )
    by_name: dict[str, dict[str, Any]] = {}
    duplicates: set[str] = set()
    asset_ids: set[int] = set()
    duplicate_ids: set[int] = set()
    total_size = 0
    for index, asset in enumerate(assets):
        if not isinstance(asset, dict):
            raise BuildScopeReleaseAssetError(
                f"public asset entry {index} is not an object"
            )
        name = asset.get("name")
        if isinstance(name, str):
            try:
                name_bytes = len(name.encode("utf-8"))
            except UnicodeEncodeError as exc:
                raise BuildScopeReleaseAssetError(
                    f"public asset entry {index} has an unencodable name"
                ) from exc
            if name_bytes > MAX_ASSET_NAME_BYTES:
                raise BuildScopeReleaseAssetError(
                    f"public asset entry {index} has an excessively long name"
                )
        if (
            not isinstance(name, str)
            or not name
            or "\x00" in name
            or "/" in name
            or "\\" in name
        ):
            raise BuildScopeReleaseAssetError(
                f"public asset entry {index} has an invalid name: {name!r}"
            )
        if name in by_name:
            duplicates.add(name)
        by_name[name] = asset

        asset_id = asset.get("id")
        if (
            isinstance(asset_id, bool)
            or not isinstance(asset_id, int)
            or not 0 < asset_id <= MAX_GITHUB_ID
        ):
            raise BuildScopeReleaseAssetError(
                f"public asset entry {index} has an invalid id: {asset_id!r}"
            )
        if asset_id in asset_ids:
            duplicate_ids.add(asset_id)
        asset_ids.add(asset_id)

        state = asset.get("state")
        if state != "uploaded":
            raise BuildScopeReleaseAssetError(
                f"asset is not uploaded: {name} (state={state!r})"
            )
        size = asset.get("size")
        if (
            isinstance(size, bool)
            or not isinstance(size, int)
            or not 0 < size <= MAX_ASSET_BYTES
        ):
            raise BuildScopeReleaseAssetError(
                f"asset size is outside the accepted range: {name} (size={size!r})"
            )
        total_size += size
        if total_size > MAX_TOTAL_ASSET_BYTES:
            raise BuildScopeReleaseAssetError(
                "public asset sizes exceed the accepted total bound of "
                f"{MAX_TOTAL_ASSET_BYTES} bytes"
            )
        digest = asset.get("digest")
        if (
            not isinstance(digest, str)
            or SHA256_DIGEST_PATTERN.fullmatch(digest) is None
        ):
            raise BuildScopeReleaseAssetError(
                f"asset digest is invalid: {name} (digest={digest!r})"
            )

    if duplicates:
        raise BuildScopeReleaseAssetError(
            f"duplicate public asset names: {sorted(duplicates)!r}"
        )
    if duplicate_ids:
        raise BuildScopeReleaseAssetError(
            f"duplicate public asset ids: {sorted(duplicate_ids)!r}"
        )
    actual = set(by_name)
    if actual != expected:
        raise BuildScopeReleaseAssetError(
            "public asset set mismatch: "
            f"missing={sorted(expected - actual)!r} extra={sorted(actual - expected)!r}"
        )
    return release, by_name


def check_release_assets(
    release_json: Path,
    dist: Path,
    tag: str,
    version: str,
    *,
    stage: str = "final",
) -> None:
    """Raise unless the requested release stage and all nine assets agree exactly."""

    _, by_name = load_release_assets(
        release_json,
        tag,
        version,
        stage=stage,
    )
    _require_regular_directory(dist, "release asset directory")

    for name in sorted(by_name):
        asset = by_name[name]
        local = dist / name
        initial_info = _regular_file_info(local, f"local asset {name}")
        if initial_info.st_size <= 0:
            raise BuildScopeReleaseAssetError(f"local asset must not be empty: {name}")
        api_size = asset.get("size")
        if (
            isinstance(api_size, bool)
            or not isinstance(api_size, int)
            or not 0 < api_size <= MAX_ASSET_BYTES
        ):
            raise BuildScopeReleaseAssetError(
                f"asset size is invalid: {name} (size={api_size!r})"
            )
        if api_size != initial_info.st_size:
            raise BuildScopeReleaseAssetError(
                f"asset size mismatch: {name}: API={api_size} local={initial_info.st_size}"
            )

        actual_size, hex_digest = _stream_sha256(
            local, f"local asset {name}", initial_info
        )
        if actual_size != api_size:
            raise BuildScopeReleaseAssetError(
                f"asset size changed while hashing: {name}: API={api_size} local={actual_size}"
            )
        expected_digest = f"sha256:{hex_digest}"
        api_digest = asset.get("digest")
        if api_digest != expected_digest:
            raise BuildScopeReleaseAssetError(
                f"asset digest mismatch: {name}: {api_digest!r} != {expected_digest!r}"
            )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release_json", type=Path)
    parser.add_argument("dist", type=Path)
    parser.add_argument("tag")
    parser.add_argument("version")
    parser.add_argument(
        "--stage",
        choices=("draft", "final"),
        default="final",
        help="release visibility required by the audit (default: final)",
    )
    args = parser.parse_args(argv)
    try:
        check_release_assets(
            args.release_json,
            args.dist,
            args.tag,
            args.version,
            stage=args.stage,
        )
    except BuildScopeReleaseAssetError as exc:
        parser.exit(1, f"BuildScope release asset audit failed: {exc}{os.linesep}")
    print(f"audited {args.stage} BuildScope {args.version}: exact 9 assets and digests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
