#!/usr/bin/env python3
"""Download and verify every asset from a private BuildScope draft release."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import time
from collections.abc import Sequence
from pathlib import Path

from check_buildscope_release_assets import (
    BuildScopeReleaseAssetError,
    _require_regular_directory,
    check_release_assets,
    expected_asset_names,
    load_release_assets,
)

REPOSITORY_PATTERN = re.compile(
    r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,99}/[A-Za-z0-9][A-Za-z0-9_.-]{0,99}$"
)
DOWNLOAD_TIMEOUT_SECONDS = 120
DOWNLOAD_BUDGET_SECONDS = 600
MAX_ERROR_TEXT_BYTES = 4096


def _safe_repository(repository: str) -> str:
    if REPOSITORY_PATTERN.fullmatch(repository) is None:
        raise BuildScopeReleaseAssetError(
            f"invalid GitHub repository identifier: {repository!r}"
        )
    return repository


def _bounded_error_text(payload: bytes | None) -> str:
    if not payload:
        return "no stderr"
    return payload[:MAX_ERROR_TEXT_BYTES].decode("utf-8", errors="replace").strip()


def download_release_assets(
    release_json: Path,
    destination: Path,
    repository: str,
    tag: str,
    version: str,
    *,
    stage: str = "draft",
    gh_executable: str = "gh",
) -> None:
    """Download exact API asset IDs into a fresh directory and verify all bytes."""

    repository = _safe_repository(repository)
    _, by_name = load_release_assets(
        release_json,
        tag,
        version,
        stage=stage,
    )
    _require_regular_directory(destination.parent, "download parent directory")
    try:
        destination.mkdir(mode=0o700)
    except OSError as exc:
        raise BuildScopeReleaseAssetError(
            f"download destination must be new and creatable: {exc}"
        ) from exc

    deadline = time.monotonic() + DOWNLOAD_BUDGET_SECONDS
    for name in expected_asset_names(version):
        asset_id = by_name[name]["id"]
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise BuildScopeReleaseAssetError(
                "release asset download exceeded the overall time budget"
            )
        timeout = max(1, min(DOWNLOAD_TIMEOUT_SECONDS, int(remaining)))
        partial = destination / f".{name}.partial"
        completed: subprocess.CompletedProcess[bytes]
        try:
            with partial.open("xb") as output:
                completed = subprocess.run(
                    [
                        gh_executable,
                        "api",
                        "-H",
                        "Accept: application/octet-stream",
                        f"repos/{repository}/releases/assets/{asset_id}",
                    ],
                    check=False,
                    stdin=subprocess.DEVNULL,
                    stdout=output,
                    stderr=subprocess.PIPE,
                    timeout=timeout,
                )
        except (OSError, subprocess.TimeoutExpired) as exc:
            partial.unlink(missing_ok=True)
            raise BuildScopeReleaseAssetError(
                f"release asset download failed before completion: {name}: {exc}"
            ) from exc
        if completed.returncode != 0:
            partial.unlink(missing_ok=True)
            raise BuildScopeReleaseAssetError(
                f"release asset download failed: {name}: "
                f"{_bounded_error_text(completed.stderr)}"
            )
        try:
            os.replace(partial, destination / name)
        except OSError as exc:
            partial.unlink(missing_ok=True)
            raise BuildScopeReleaseAssetError(
                f"downloaded asset cannot be finalized: {name}: {exc}"
            ) from exc

    check_release_assets(
        release_json,
        destination,
        tag,
        version,
        stage=stage,
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release_json", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("repository")
    parser.add_argument("tag")
    parser.add_argument("version")
    parser.add_argument(
        "--stage",
        choices=("draft", "final"),
        default="draft",
        help="release visibility required by the download (default: draft)",
    )
    args = parser.parse_args(argv)
    try:
        download_release_assets(
            args.release_json,
            args.destination,
            args.repository,
            args.tag,
            args.version,
            stage=args.stage,
        )
    except BuildScopeReleaseAssetError as exc:
        parser.exit(1, f"BuildScope release download audit failed: {exc}{os.linesep}")
    print(
        f"downloaded and audited {args.stage} BuildScope {args.version}: exact 9 assets"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
