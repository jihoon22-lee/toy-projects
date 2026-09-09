#!/usr/bin/env python3
"""BuildScope's entry point into the shared release manifest audit.

The audit lives in ``release_manifest`` and is product-agnostic. This module
stays so BuildScope's release workflow and its tests keep calling the same
signature — which is what makes them evidence that generalizing changed nothing
about BuildScope's contract.
"""

from __future__ import annotations

import argparse
import os
import sys
from collections.abc import Sequence
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_audit import ReleaseAssetError  # noqa: E402
from release_manifest import (  # noqa: E402
    MAX_MANIFEST_BYTES,
    MAX_SIDECAR_BYTES,
    SHA256_PATTERN,
    _decode_checksum_file,
    _parse_manifest,
    _read_bounded_regular_file,
)
from release_manifest import check_release_manifest as _check_release_manifest  # noqa: E402
from release_manifest import manifest_asset_names as _manifest_asset_names  # noqa: E402

PRODUCT_KEY = "buildscope"

BuildScopeReleaseAssetError = ReleaseAssetError

__all__ = [
    "MAX_MANIFEST_BYTES",
    "MAX_SIDECAR_BYTES",
    "SHA256_PATTERN",
    "BuildScopeReleaseAssetError",
    "_decode_checksum_file",
    "_parse_manifest",
    "_read_bounded_regular_file",
    "check_release_manifest",
    "main",
    "manifest_asset_names",
]


def manifest_asset_names(version: str) -> tuple[str, ...]:
    """Return the exact eight files covered by ``SHA256SUMS`` in order."""

    return _manifest_asset_names(PRODUCT_KEY, version)


def check_release_manifest(dist: Path, version: str) -> None:
    """Raise unless the exact manifest, sidecar, and eight files agree."""

    _check_release_manifest(dist, PRODUCT_KEY, version)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dist", type=Path)
    parser.add_argument("version")
    args = parser.parse_args(argv)
    try:
        check_release_manifest(args.dist, args.version)
    except ReleaseAssetError as exc:
        parser.exit(1, f"BuildScope release manifest audit failed: {exc}{os.linesep}")
    print(
        f"audited BuildScope {args.version}: exact 8-entry SHA256SUMS and pyz sidecar"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
