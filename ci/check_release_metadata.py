#!/usr/bin/env python3
"""Gate a product release on its own metadata agreeing with itself.

Run at release time, before anything is built or published. Publishing
`widget-v1.2.3` is a claim about three separate things, and this refuses the
release unless all three say the same number:

- the tag the maintainer pushed,
- every place the product states its version (see `version_surfaces`),
- the CHANGELOG entry a reader will be pointed at.

The five products share one CHANGELOG, so the heading has to name the product.
`### AbiLens 0.1.0` is unambiguous where `## [0.1.0]` is not.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from version_surfaces import (  # noqa: E402
    VersionSurfaceError,
    manifest_version,
    version_surfaces,
)


class ReleaseMetadataError(Exception):
    """The release's own metadata disagrees about what is being released."""


def _release_display_names(repo_root: Path) -> dict[str, str]:
    manifest = json.loads((repo_root / "ci" / "projects.json").read_text(encoding="utf-8"))
    names = {}
    for project in manifest["projects"]:
        release = project.get("release", {})
        if release.get("enabled"):
            names[project["name"]] = release["display_name"]
    return names


def _check_changelog(repo_root: Path, display_name: str, version: str) -> None:
    changelog = (repo_root / "CHANGELOG.md").read_text(encoding="utf-8")
    pattern = re.compile(
        rf"^### {re.escape(display_name)} {re.escape(version)}\s*$", re.MULTILINE
    )
    headings = pattern.findall(changelog)
    if len(headings) != 1:
        raise ReleaseMetadataError(
            f"CHANGELOG.md must contain exactly one '### {display_name} {version}' heading, "
            f"found {len(headings)}"
        )


def check_release_metadata(
    repo_root: Path,
    product: str,
    version: str,
    *,
    require_changelog: bool = True,
) -> dict[str, str]:
    """Return the surfaces checked, or raise describing the first disagreement."""

    display_names = _release_display_names(repo_root)
    if product not in display_names:
        raise ReleaseMetadataError(
            f"{product} is not a released product in ci/projects.json "
            f"(released: {sorted(display_names)})"
        )

    product_root = repo_root / product
    try:
        declared = manifest_version(product_root / "ici.toml")
        surfaces = version_surfaces(product_root, repo_root)
    except VersionSurfaceError as error:
        raise ReleaseMetadataError(str(error)) from error

    if declared != version:
        raise ReleaseMetadataError(
            f"{product}/ici.toml cuts {declared!r} but the release is {version!r}"
        )

    disagreeing = {path: stated for path, stated in surfaces.items() if stated != version}
    if disagreeing:
        detail = ", ".join(f"{path} states {stated!r}" for path, stated in sorted(disagreeing.items()))
        raise ReleaseMetadataError(f"release is {version!r} but {detail}")

    if require_changelog:
        _check_changelog(repo_root, display_names[product], version)

    return surfaces


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("product")
    parser.add_argument("version")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument(
        "--skip-changelog",
        action="store_true",
        help="check version surfaces only; used where the CHANGELOG is not yet written",
    )
    args = parser.parse_args(argv)

    try:
        surfaces = check_release_metadata(
            args.repo_root,
            args.product,
            args.version,
            require_changelog=not args.skip_changelog,
        )
    except ReleaseMetadataError as error:
        print(f"release metadata check failed: {error}", file=sys.stderr)
        return 1

    print(f"validated {args.product} {args.version} across {len(surfaces)} version surface(s):")
    for path, stated in sorted(surfaces.items()):
        print(f"  {path} -> {stated}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
