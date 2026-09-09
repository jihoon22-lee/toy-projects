#!/usr/bin/env python3
"""BuildScope's entry point into the shared release asset audit.

The audit itself is product-agnostic and lives in ``release_assets``; the asset
list, tag prefix and display name come from ``ci/projects.json``. This module
stays because BuildScope's release workflow and several tests call it by name
with a BuildScope-shaped signature, and because pinning that signature is what
proves the generalization did not change BuildScope's contract.
"""

from __future__ import annotations

import argparse
import os
import sys
from collections.abc import Sequence
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_assets import (  # noqa: E402
    check_release_assets as _check_release_assets,
)
from release_assets import (  # noqa: E402
    expected_asset_names as _expected_asset_names,
)
from release_assets import (  # noqa: E402
    load_release_assets as _load_release_assets,
)
# Re-exported because callers have always imported these through this module.
# Keeping the names here is what lets the audit move without touching them.
from release_audit import (  # noqa: E402
    MAX_ASSET_BYTES,
    MAX_ASSET_NAME_BYTES,
    MAX_GITHUB_ID,
    MAX_TOTAL_ASSET_BYTES,
    ReleaseAssetError,
    _assert_path_matches,
    _open_regular_file,
    _regular_file_info,
    _require_regular_directory,
    _stat_signature,
    _stream_sha256,
    validate_version,
)

__all__ = [
    "MAX_ASSET_BYTES",
    "MAX_ASSET_NAME_BYTES",
    "MAX_GITHUB_ID",
    "MAX_TOTAL_ASSET_BYTES",
    "BuildScopeReleaseAssetError",
    "PRODUCT",
    "PRODUCT_KEY",
    "TAG_PREFIX",
    "_assert_path_matches",
    "_open_regular_file",
    "_regular_file_info",
    "_require_regular_directory",
    "_stat_signature",
    "_stream_sha256",
    "_validate_version",
    "check_release_assets",
    "expected_asset_names",
    "load_release_assets",
    "main",
]

PRODUCT_KEY = "buildscope"
TAG_PREFIX = "buildscope-v"
PRODUCT = "BuildScope"

# Kept as an alias: this module's public errors have always carried this name,
# and callers catch it by name.
BuildScopeReleaseAssetError = ReleaseAssetError


def _validate_version(version: str, tag: str) -> None:
    validate_version(version, tag, TAG_PREFIX, PRODUCT)


def expected_asset_names(version: str) -> tuple[str, ...]:
    """Return the exact nine asset names required for ``version``."""

    return _expected_asset_names(PRODUCT_KEY, version)


def load_release_assets(release_json, tag, version, *, stage="final"):
    """Load and validate release metadata before any asset bytes are trusted."""

    return _load_release_assets(release_json, PRODUCT_KEY, tag, version, stage=stage)


def check_release_assets(release_json, dist, tag, version, *, stage="final"):
    """Raise unless the requested release stage and all nine assets agree exactly."""

    return _check_release_assets(release_json, dist, PRODUCT_KEY, tag, version, stage=stage)


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
    except ReleaseAssetError as exc:
        parser.exit(1, f"BuildScope release asset audit failed: {exc}{os.linesep}")
    print(f"audited {args.stage} BuildScope {args.version}: exact 9 assets and digests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
