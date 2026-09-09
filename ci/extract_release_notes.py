#!/usr/bin/env python3
"""Extract one product's release notes from the shared CHANGELOG.

The five products share one CHANGELOG, so a release body has to be cut from a
heading that names the product. `### AbiLens 0.1.0` is unambiguous where
`## [0.1.0]` is not, and the section ends at the next heading of any level so a
release cannot silently absorb the section below it.

An empty or duplicated section is refused rather than published: a release
whose notes are blank tells a reader nothing, and two sections for one version
means the CHANGELOG does not know what shipped.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


class ReleaseNotesError(Exception):
    """The CHANGELOG cannot describe this release."""


def extract_release_notes(repo_root: Path, display_name: str, version: str) -> str:
    """Return the section body, or raise describing why it cannot be used."""

    text = (repo_root / "CHANGELOG.md").read_text(encoding="utf-8")
    heading = rf"^### {re.escape(display_name)} {re.escape(version)}\s*$"

    if len(re.findall(heading, text, re.MULTILINE)) > 1:
        raise ReleaseNotesError(
            f"CHANGELOG.md declares '{display_name} {version}' more than once"
        )

    pattern = re.compile(
        heading + r"\n(?P<body>.*?)(?=^#{1,6} |\Z)",
        re.MULTILINE | re.DOTALL,
    )
    match = pattern.search(text)
    if match is None:
        raise ReleaseNotesError(
            f"CHANGELOG.md has no '### {display_name} {version}' section"
        )

    body = match.group("body").strip()
    if not body:
        raise ReleaseNotesError(
            f"CHANGELOG.md section for {display_name} {version} is empty"
        )
    return body


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("display_name")
    parser.add_argument("version")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args(argv)

    try:
        print(extract_release_notes(args.repo_root, args.display_name, args.version))
    except ReleaseNotesError as error:
        print(f"release notes are unusable: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
