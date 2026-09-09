#!/usr/bin/env python3
"""BuildScope's entry point into the shared release state audit.

The audit lives in ``release_state`` and is product-agnostic. This module keeps
BuildScope's signatures so its release workflow and its suite go on calling the
same functions the same way — which is what makes that suite evidence the move
changed nothing.
"""

from __future__ import annotations

import argparse
import os
import sys
from collections.abc import Sequence
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import release_state  # noqa: E402
from release_state import (  # noqa: E402
    MAX_JSON_BYTES,
    MAX_RELEASE_BODY_BYTES,
    MAX_RELEASE_ID,
    MAX_RELEASE_PAGES,
    MAX_RELEASES_PER_PAGE,
    MAX_TAG_BYTES,
    ReleaseSlot,
    ReleaseStateError,
)

PRODUCT_KEY = "buildscope"
DISPLAY_NAME = "BuildScope"

BuildScopeReleaseStateError = ReleaseStateError

__all__ = [
    "MAX_JSON_BYTES",
    "MAX_RELEASE_BODY_BYTES",
    "MAX_RELEASE_ID",
    "MAX_RELEASE_PAGES",
    "MAX_RELEASES_PER_PAGE",
    "MAX_TAG_BYTES",
    "BuildScopeReleaseStateError",
    "ReleaseSlot",
    "check_release_state",
    "inspect_release_slot",
    "main",
    "recover_owned_draft",
]


def check_release_state(release_json, tag, version, target_sha, stage, **kwargs):
    """Validate one release response and return its positive numeric ID."""

    return release_state.check_release_state(
        release_json, PRODUCT_KEY, DISPLAY_NAME, tag, version, target_sha, stage, **kwargs
    )


def inspect_release_slot(release_pages_json, tag, version, target_sha):
    """Return an empty/final slot and reject every pre-existing draft."""

    return release_state.inspect_release_slot(
        release_pages_json, PRODUCT_KEY, DISPLAY_NAME, tag, version, target_sha
    )


def recover_owned_draft(
    release_pages_json, tag, version, target_sha, owner_marker, expected_body_sha256
):
    """Recover one private draft created by this exact workflow run."""

    return release_state.recover_owned_draft(
        release_pages_json,
        PRODUCT_KEY,
        DISPLAY_NAME,
        tag,
        version,
        target_sha,
        owner_marker,
        expected_body_sha256,
    )


def main(argv: Sequence[str] | None = None) -> int:
    """Run the shared CLI with BuildScope's product argument supplied."""

    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] in {"slot", "recover-owned-draft", "state"}:
        # The shared CLI takes <json> <product> <tag> ...; BuildScope's callers
        # pass <json> <tag> ..., so insert the product where it now belongs.
        argv = argv[:2] + [PRODUCT_KEY] + argv[2:]
    try:
        return release_state.main(argv)
    except ReleaseStateError as exc:
        parser = argparse.ArgumentParser()
        parser.exit(1, f"BuildScope release state audit failed: {exc}{os.linesep}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
