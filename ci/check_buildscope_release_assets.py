#!/usr/bin/env python3
"""Audit the final BuildScope GitHub Release and its downloaded assets.

After publication, the release workflow compares the GitHub API response with
the same nine local artifacts that it uploaded. Keeping this dependency-free
standard-library audit in a normal Python module makes the final-publication
contract testable on every supported interpreter and prevents the workflow
from silently drifting away from its unit-tested checks.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
from collections.abc import Sequence
from pathlib import Path
from typing import Any

MAX_RELEASE_JSON_BYTES = 20_000_000
HASH_CHUNK_BYTES = 1024 * 1024
VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")


class BuildScopeReleaseAssetError(ValueError):
    """The final BuildScope release or one of its assets is invalid."""


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


def _regular_file_info(path: Path, label: str) -> os.stat_result:
    try:
        info = path.lstat()
    except OSError as exc:
        raise BuildScopeReleaseAssetError(
            f"{label} cannot be inspected: {exc}"
        ) from exc
    if not stat.S_ISREG(info.st_mode):
        raise BuildScopeReleaseAssetError(f"{label} must be a regular file")
    return info


def _require_regular_directory(path: Path, label: str) -> None:
    try:
        info = path.lstat()
    except OSError as exc:
        raise BuildScopeReleaseAssetError(
            f"{label} cannot be inspected: {exc}"
        ) from exc
    if not stat.S_ISDIR(info.st_mode):
        raise BuildScopeReleaseAssetError(f"{label} must be a real directory")


def _read_release_json(path: Path) -> dict[str, Any]:
    info = _regular_file_info(path, "release metadata")
    if info.st_size <= 0 or info.st_size > MAX_RELEASE_JSON_BYTES:
        raise BuildScopeReleaseAssetError(
            "release metadata size is outside the accepted range: "
            f"{info.st_size} bytes (maximum {MAX_RELEASE_JSON_BYTES})"
        )

    try:
        with path.open("rb") as stream:
            opened_info = os.fstat(stream.fileno())
            if not stat.S_ISREG(opened_info.st_mode):
                raise BuildScopeReleaseAssetError(
                    "release metadata must be a regular file"
                )
            if opened_info.st_dev != info.st_dev or opened_info.st_ino != info.st_ino:
                raise BuildScopeReleaseAssetError(
                    "release metadata changed while it was being audited"
                )
            payload = stream.read(MAX_RELEASE_JSON_BYTES + 1)
    except BuildScopeReleaseAssetError:
        raise
    except OSError as exc:
        raise BuildScopeReleaseAssetError(
            f"release metadata cannot be read: {exc}"
        ) from exc
    if len(payload) > MAX_RELEASE_JSON_BYTES:
        raise BuildScopeReleaseAssetError(
            "release metadata exceeds the accepted bound "
            f"of {MAX_RELEASE_JSON_BYTES} bytes"
        )
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise BuildScopeReleaseAssetError(
            f"release metadata is not valid UTF-8: {exc}"
        ) from exc
    try:
        release = json.loads(text)
    except json.JSONDecodeError as exc:
        raise BuildScopeReleaseAssetError(
            f"release metadata is not valid JSON: {exc}"
        ) from exc
    if not isinstance(release, dict):
        raise BuildScopeReleaseAssetError("release metadata must be a JSON object")
    return release


def _stream_sha256(
    path: Path, label: str, initial_info: os.stat_result
) -> tuple[int, str]:
    """Read ``path`` in bounded chunks and return its byte count and SHA-256."""

    digest = hashlib.sha256()
    total = 0
    try:
        with path.open("rb") as stream:
            opened_info = os.fstat(stream.fileno())
            if not stat.S_ISREG(opened_info.st_mode):
                raise BuildScopeReleaseAssetError(f"{label} must be a regular file")
            if (
                opened_info.st_dev != initial_info.st_dev
                or opened_info.st_ino != initial_info.st_ino
            ):
                raise BuildScopeReleaseAssetError(
                    f"{label} changed while it was being audited"
                )
            while True:
                chunk = stream.read(HASH_CHUNK_BYTES)
                if not chunk:
                    break
                digest.update(chunk)
                total += len(chunk)
    except BuildScopeReleaseAssetError:
        raise
    except OSError as exc:
        raise BuildScopeReleaseAssetError(f"{label} cannot be read: {exc}") from exc
    return total, digest.hexdigest()


def _validate_version(version: str, tag: str) -> None:
    if VERSION_PATTERN.fullmatch(version) is None:
        raise BuildScopeReleaseAssetError(f"invalid BuildScope version: {version!r}")
    expected_tag = f"buildscope-v{version}"
    if tag != expected_tag:
        raise BuildScopeReleaseAssetError(
            f"release tag argument does not match version: expected {expected_tag!r}, got {tag!r}"
        )


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
        or release_id <= 0
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
    by_name: dict[str, dict[str, Any]] = {}
    duplicates: set[str] = set()
    asset_ids: set[int] = set()
    duplicate_ids: set[int] = set()
    for index, asset in enumerate(assets):
        if not isinstance(asset, dict):
            raise BuildScopeReleaseAssetError(
                f"public asset entry {index} is not an object"
            )
        name = asset.get("name")
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
        if isinstance(asset_id, bool) or not isinstance(asset_id, int) or asset_id <= 0:
            raise BuildScopeReleaseAssetError(
                f"public asset entry {index} has an invalid id: {asset_id!r}"
            )
        if asset_id in asset_ids:
            duplicate_ids.add(asset_id)
        asset_ids.add(asset_id)

    if duplicates:
        raise BuildScopeReleaseAssetError(
            f"duplicate public asset names: {sorted(duplicates)!r}"
        )
    if duplicate_ids:
        raise BuildScopeReleaseAssetError(
            f"duplicate public asset ids: {sorted(duplicate_ids)!r}"
        )
    if len(assets) != len(expected):
        raise BuildScopeReleaseAssetError(
            f"public asset count mismatch: expected={len(expected)} actual={len(assets)}"
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
        state = asset.get("state")
        if state != "uploaded":
            raise BuildScopeReleaseAssetError(
                f"asset is not uploaded: {name} (state={state!r})"
            )

        local = dist / name
        initial_info = _regular_file_info(local, f"local asset {name}")
        if initial_info.st_size <= 0:
            raise BuildScopeReleaseAssetError(f"local asset must not be empty: {name}")
        api_size = asset.get("size")
        if isinstance(api_size, bool) or not isinstance(api_size, int):
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
