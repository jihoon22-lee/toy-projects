#!/usr/bin/env python3
"""Audit a product's GitHub Release and its downloaded assets.

After publication, a release workflow compares the GitHub API response with the
same local artifacts it uploaded. Keeping this dependency-free standard-library
audit in a normal Python module makes the publication contract testable on
every supported interpreter and prevents a workflow from silently drifting away
from its unit-tested checks.

Nothing here is product-specific. The filesystem and version primitives come
from ``release_audit``, and the asset list, tag prefix and display name come
from the product's own declaration in ``ci/projects.json`` via
``release_registry``. A product that publishes a different set of artifacts
gets audited against its own set without editing this file.
"""

from __future__ import annotations

import argparse
import json
import math
import os
from collections.abc import Sequence
from pathlib import Path
from typing import Any

from release_registry import load_release_specs
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

REPO_ROOT = Path(__file__).resolve().parent.parent


def _spec(product: str, repo_root: Path | None = None):
    """Resolve the release spec a product declared in ``ci/projects.json``."""

    specs = load_release_specs(repo_root or REPO_ROOT)
    if product not in specs:
        raise ReleaseAssetError(
            f"{product} is not a released product (released: {sorted(specs)})"
        )
    return specs[product]


def expected_asset_names(
    product: str, version: str, repo_root: Path | None = None
) -> tuple[str, ...]:
    """Return the exact asset names ``product`` publishes for ``version``."""

    return _spec(product, repo_root).expected_asset_names(version)



def load_release_assets(
    release_json: Path,
    product: str,
    tag: str,
    version: str,
    *,
    stage: str = "final",
    repo_root: Path | None = None,
) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    """Load and validate release metadata before any asset bytes are trusted."""

    spec = _spec(product, repo_root)
    validate_version(version, tag, spec.tag_prefix, spec.display_name)
    if stage not in {"draft", "final"}:
        raise ReleaseAssetError(f"invalid release stage: {stage!r}")
    release = _read_release_json(release_json)

    release_id = release.get("id")
    if (
        isinstance(release_id, bool)
        or not isinstance(release_id, int)
        or not 0 < release_id <= MAX_GITHUB_ID
    ):
        raise ReleaseAssetError(f"release id is invalid: {release_id!r}")
    release_tag = release.get("tag_name")
    if release_tag != tag:
        raise ReleaseAssetError(
            f"release tag mismatch: {release_tag!r} != {tag!r}"
        )
    release_name = release.get("name")
    expected_name = f"{spec.display_name} {version}"
    if release_name != expected_name:
        raise ReleaseAssetError(
            f"release name mismatch: {release_name!r} != {expected_name!r}"
        )

    expected_draft = stage == "draft"
    if release.get("draft") is not expected_draft:
        raise ReleaseAssetError(
            f"{spec.display_name} {stage} audit requires draft={expected_draft!r}"
        )
    if release.get("prerelease") is not False:
        raise ReleaseAssetError(
            f"{spec.display_name} product release must not be a prerelease"
        )
    published_at = release.get("published_at")
    if stage == "draft":
        if published_at is not None:
            raise ReleaseAssetError(
                f"draft release must not have published_at: {published_at!r}"
            )
    elif not isinstance(published_at, str) or not published_at.strip():
        raise ReleaseAssetError(
            f"final release must have published_at: {published_at!r}"
        )

    assets = release.get("assets")
    if not isinstance(assets, list):
        raise ReleaseAssetError(
            "release assets are unavailable or not a JSON array"
        )

    expected = set(spec.expected_asset_names(version))
    if len(assets) != len(expected):
        raise ReleaseAssetError(
            f"public asset count mismatch: expected={len(expected)} actual={len(assets)}"
        )
    by_name: dict[str, dict[str, Any]] = {}
    duplicates: set[str] = set()
    asset_ids: set[int] = set()
    duplicate_ids: set[int] = set()
    total_size = 0
    for index, asset in enumerate(assets):
        if not isinstance(asset, dict):
            raise ReleaseAssetError(
                f"public asset entry {index} is not an object"
            )
        name = asset.get("name")
        if isinstance(name, str):
            try:
                name_bytes = len(name.encode("utf-8"))
            except UnicodeEncodeError as exc:
                raise ReleaseAssetError(
                    f"public asset entry {index} has an unencodable name"
                ) from exc
            if name_bytes > MAX_ASSET_NAME_BYTES:
                raise ReleaseAssetError(
                    f"public asset entry {index} has an excessively long name"
                )
        if (
            not isinstance(name, str)
            or not name
            or "\x00" in name
            or "/" in name
            or "\\" in name
        ):
            raise ReleaseAssetError(
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
            raise ReleaseAssetError(
                f"public asset entry {index} has an invalid id: {asset_id!r}"
            )
        if asset_id in asset_ids:
            duplicate_ids.add(asset_id)
        asset_ids.add(asset_id)

        state = asset.get("state")
        if state != "uploaded":
            raise ReleaseAssetError(
                f"asset is not uploaded: {name} (state={state!r})"
            )
        size = asset.get("size")
        if (
            isinstance(size, bool)
            or not isinstance(size, int)
            or not 0 < size <= MAX_ASSET_BYTES
        ):
            raise ReleaseAssetError(
                f"asset size is outside the accepted range: {name} (size={size!r})"
            )
        total_size += size
        if total_size > MAX_TOTAL_ASSET_BYTES:
            raise ReleaseAssetError(
                "public asset sizes exceed the accepted total bound of "
                f"{MAX_TOTAL_ASSET_BYTES} bytes"
            )
        digest = asset.get("digest")
        if (
            not isinstance(digest, str)
            or SHA256_DIGEST_PATTERN.fullmatch(digest) is None
        ):
            raise ReleaseAssetError(
                f"asset digest is invalid: {name} (digest={digest!r})"
            )

    if duplicates:
        raise ReleaseAssetError(
            f"duplicate public asset names: {sorted(duplicates)!r}"
        )
    if duplicate_ids:
        raise ReleaseAssetError(
            f"duplicate public asset ids: {sorted(duplicate_ids)!r}"
        )
    actual = set(by_name)
    if actual != expected:
        raise ReleaseAssetError(
            "public asset set mismatch: "
            f"missing={sorted(expected - actual)!r} extra={sorted(actual - expected)!r}"
        )
    return release, by_name


def check_release_assets(
    release_json: Path,
    dist: Path,
    product: str,
    tag: str,
    version: str,
    *,
    stage: str = "final",
    repo_root: Path | None = None,
) -> None:
    """Raise unless the requested release stage and all nine assets agree exactly."""

    _, by_name = load_release_assets(
        release_json,
        product,
        tag,
        version,
        stage=stage,
        repo_root=repo_root,
    )
    _require_regular_directory(dist, "release asset directory")

    for name in sorted(by_name):
        asset = by_name[name]
        local = dist / name
        initial_info = _regular_file_info(local, f"local asset {name}")
        if initial_info.st_size <= 0:
            raise ReleaseAssetError(f"local asset must not be empty: {name}")
        api_size = asset.get("size")
        if (
            isinstance(api_size, bool)
            or not isinstance(api_size, int)
            or not 0 < api_size <= MAX_ASSET_BYTES
        ):
            raise ReleaseAssetError(
                f"asset size is invalid: {name} (size={api_size!r})"
            )
        if api_size != initial_info.st_size:
            raise ReleaseAssetError(
                f"asset size mismatch: {name}: API={api_size} local={initial_info.st_size}"
            )

        actual_size, hex_digest = _stream_sha256(
            local, f"local asset {name}", initial_info
        )
        if actual_size != api_size:
            raise ReleaseAssetError(
                f"asset size changed while hashing: {name}: API={api_size} local={actual_size}"
            )
        expected_digest = f"sha256:{hex_digest}"
        api_digest = asset.get("digest")
        if api_digest != expected_digest:
            raise ReleaseAssetError(
                f"asset digest mismatch: {name}: {api_digest!r} != {expected_digest!r}"
            )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release_json", type=Path)
    parser.add_argument("dist", type=Path)
    parser.add_argument("product")
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
            args.product,
            args.tag,
            args.version,
            stage=args.stage,
        )
    except ReleaseAssetError as exc:
        parser.exit(1, f"{args.product} release asset audit failed: {exc}{os.linesep}")
    expected = expected_asset_names(args.product, args.version)
    print(
        f"audited {args.stage} {args.product} {args.version}: "
        f"exact {len(expected)} assets and digests"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
