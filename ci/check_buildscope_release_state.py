#!/usr/bin/env python3
"""Validate BuildScope's GitHub Release slot and state transitions."""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import stat
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

MAX_JSON_BYTES = 20_000_000
MAX_RELEASE_PAGES = 1_000
MAX_RELEASES_PER_PAGE = 100
MAX_RELEASE_ID = (1 << 63) - 1
MAX_TAG_BYTES = 255
VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
SHA_PATTERN = re.compile(r"^[0-9a-f]{40}$")


class BuildScopeReleaseStateError(ValueError):
    """The BuildScope release slot or state is unsafe."""


@dataclass(frozen=True)
class ReleaseSlot:
    mode: str
    release_id: int | None


def _bounded_int(raw: str) -> int:
    if len(raw) > 20:
        raise ValueError("JSON integer exceeds 20 decimal digits")
    return int(raw)


def _bounded_float(raw: str) -> float:
    if len(raw) > 100:
        raise ValueError("JSON float exceeds 100 characters")
    value = float(raw)
    if not math.isfinite(value):
        raise ValueError("JSON float must be finite")
    return value


def _reject_constant(raw: str) -> None:
    raise ValueError(f"non-standard JSON constant: {raw}")


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise ValueError(f"duplicate JSON key: {key}")
        value[key] = item
    return value


def _read_json(path: Path, label: str) -> Any:
    flags = os.O_RDONLY
    flags |= getattr(os, "O_CLOEXEC", 0)
    flags |= getattr(os, "O_NOFOLLOW", 0)
    flags |= getattr(os, "O_NONBLOCK", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as exc:
        raise BuildScopeReleaseStateError(
            f"{label} cannot be opened safely: {exc}"
        ) from exc
    try:
        opened = os.fstat(descriptor)
        if not stat.S_ISREG(opened.st_mode):
            raise BuildScopeReleaseStateError(f"{label} must be a regular file")
        if opened.st_size <= 0 or opened.st_size > MAX_JSON_BYTES:
            raise BuildScopeReleaseStateError(
                f"{label} size is outside the accepted range: {opened.st_size} bytes"
            )
        with os.fdopen(descriptor, "rb", closefd=True) as stream:
            descriptor = -1
            payload = stream.read(MAX_JSON_BYTES + 1)
            after = os.fstat(stream.fileno())
        try:
            named = path.stat(follow_symlinks=False)
        except OSError as exc:
            raise BuildScopeReleaseStateError(
                f"{label} path cannot be rechecked: {exc}"
            ) from exc
        identity = (opened.st_dev, opened.st_ino)
        if (
            not stat.S_ISREG(named.st_mode)
            or identity != (after.st_dev, after.st_ino)
            or identity != (named.st_dev, named.st_ino)
            or (opened.st_size, opened.st_mtime_ns, opened.st_ctime_ns)
            != (after.st_size, after.st_mtime_ns, after.st_ctime_ns)
            or (opened.st_size, opened.st_mtime_ns, opened.st_ctime_ns)
            != (named.st_size, named.st_mtime_ns, named.st_ctime_ns)
        ):
            raise BuildScopeReleaseStateError(f"{label} changed while it was read")
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    if len(payload) > MAX_JSON_BYTES:
        raise BuildScopeReleaseStateError(f"{label} exceeds the read bound")
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise BuildScopeReleaseStateError(f"{label} is not valid UTF-8: {exc}") from exc
    try:
        return json.loads(
            text,
            parse_int=_bounded_int,
            parse_float=_bounded_float,
            parse_constant=_reject_constant,
            object_pairs_hook=_unique_object,
        )
    except (json.JSONDecodeError, ValueError) as exc:
        raise BuildScopeReleaseStateError(f"{label} is not valid JSON: {exc}") from exc


def _validate_inputs(tag: str, version: str, target_sha: str) -> None:
    if VERSION_PATTERN.fullmatch(version) is None or tag != f"buildscope-v{version}":
        raise BuildScopeReleaseStateError("BuildScope version and tag do not agree")
    if SHA_PATTERN.fullmatch(target_sha) is None:
        raise BuildScopeReleaseStateError(
            "release target must be a full lowercase commit SHA"
        )


def _release_id(release: dict[str, Any]) -> int:
    release_id = release.get("id")
    if (
        isinstance(release_id, bool)
        or not isinstance(release_id, int)
        or release_id <= 0
        or release_id > MAX_RELEASE_ID
    ):
        raise BuildScopeReleaseStateError(f"release id is invalid: {release_id!r}")
    return release_id


def _validate_release_state(
    release: dict[str, Any],
    tag: str,
    version: str,
    target_sha: str,
    stage: str,
    *,
    expected_release_id: int | None = None,
    expected_asset_count: int | None = None,
) -> int:
    if stage not in {"draft", "final"}:
        raise BuildScopeReleaseStateError(f"invalid release stage: {stage!r}")
    release_id = _release_id(release)
    if expected_release_id is not None and release_id != expected_release_id:
        raise BuildScopeReleaseStateError(
            f"release id mismatch: {release_id} != {expected_release_id}"
        )
    expected = {
        "tag_name": tag,
        "name": f"BuildScope {version}",
        "target_commitish": target_sha,
        "prerelease": False,
        "draft": stage == "draft",
    }
    for key, expected_value in expected.items():
        if release.get(key) != expected_value:
            raise BuildScopeReleaseStateError(
                f"release {key} mismatch: {release.get(key)!r} != {expected_value!r}"
            )
    published_at = release.get("published_at")
    if stage == "draft":
        if published_at is not None:
            raise BuildScopeReleaseStateError(
                f"draft release must not have published_at: {published_at!r}"
            )
    elif not isinstance(published_at, str) or not published_at.strip():
        raise BuildScopeReleaseStateError(
            f"final release must have published_at: {published_at!r}"
        )
    if expected_asset_count is not None:
        assets = release.get("assets")
        if not isinstance(assets, list) or len(assets) != expected_asset_count:
            actual = len(assets) if isinstance(assets, list) else "not-an-array"
            raise BuildScopeReleaseStateError(
                f"release asset count mismatch: {actual!r} != {expected_asset_count}"
            )
    return release_id


def check_release_state(
    release_json: Path,
    tag: str,
    version: str,
    target_sha: str,
    stage: str,
    *,
    expected_release_id: int | None = None,
    expected_asset_count: int | None = None,
) -> int:
    """Validate one release response and return its positive numeric ID."""

    _validate_inputs(tag, version, target_sha)
    release = _read_json(release_json, "release metadata")
    if not isinstance(release, dict):
        raise BuildScopeReleaseStateError("release metadata must be a JSON object")
    return _validate_release_state(
        release,
        tag,
        version,
        target_sha,
        stage,
        expected_release_id=expected_release_id,
        expected_asset_count=expected_asset_count,
    )


def inspect_release_slot(
    release_pages_json: Path,
    tag: str,
    version: str,
    target_sha: str,
) -> ReleaseSlot:
    """Return an empty/final slot and reject every pre-existing draft."""

    _validate_inputs(tag, version, target_sha)
    pages = _read_json(release_pages_json, "paginated release listing")
    if not isinstance(pages, list) or len(pages) > MAX_RELEASE_PAGES:
        raise BuildScopeReleaseStateError("paginated release listing has invalid pages")
    matches: list[dict[str, Any]] = []
    for page_index, page in enumerate(pages):
        if not isinstance(page, list) or len(page) > MAX_RELEASES_PER_PAGE:
            raise BuildScopeReleaseStateError(
                f"release listing page {page_index} has invalid shape or size"
            )
        for release_index, release in enumerate(page):
            if not isinstance(release, dict):
                raise BuildScopeReleaseStateError(
                    f"release listing entry {page_index}:{release_index} is not an object"
                )
            listed_tag = release.get("tag_name")
            if (
                not isinstance(listed_tag, str)
                or len(listed_tag.encode("utf-8")) > MAX_TAG_BYTES
            ):
                raise BuildScopeReleaseStateError(
                    f"release listing entry {page_index}:{release_index} has invalid tag"
                )
            if listed_tag == tag:
                matches.append(release)
    if not matches:
        return ReleaseSlot(mode="empty", release_id=None)
    if len(matches) != 1:
        raise BuildScopeReleaseStateError(
            f"release slot contains {len(matches)} entries for {tag}"
        )
    release = matches[0]
    if release.get("draft") is True:
        release_id = _release_id(release)
        raise BuildScopeReleaseStateError(
            f"private draft {release_id} already occupies {tag}; inspect or delete it explicitly"
        )
    release_id = _validate_release_state(
        release,
        tag,
        version,
        target_sha,
        "final",
    )
    return ReleaseSlot(mode="final", release_id=release_id)


def _positive_id(value: str) -> int:
    if not value.isascii() or not value.isdecimal():
        raise argparse.ArgumentTypeError("release id must contain decimal digits")
    parsed = int(value)
    if parsed <= 0 or parsed > MAX_RELEASE_ID:
        raise argparse.ArgumentTypeError("release id is outside the accepted range")
    return parsed


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    slot_parser = subparsers.add_parser(
        "slot", help="inspect a paginated release listing"
    )
    slot_parser.add_argument("release_pages_json", type=Path)
    slot_parser.add_argument("tag")
    slot_parser.add_argument("version")
    slot_parser.add_argument("target_sha")

    state_parser = subparsers.add_parser("state", help="validate one release response")
    state_parser.add_argument("release_json", type=Path)
    state_parser.add_argument("tag")
    state_parser.add_argument("version")
    state_parser.add_argument("target_sha")
    state_parser.add_argument("--stage", choices=("draft", "final"), required=True)
    state_parser.add_argument("--expected-release-id", type=_positive_id)
    state_parser.add_argument("--expected-asset-count", type=int)

    args = parser.parse_args(argv)
    try:
        if args.command == "slot":
            slot = inspect_release_slot(
                args.release_pages_json,
                args.tag,
                args.version,
                args.target_sha,
            )
            print(
                json.dumps(
                    {"mode": slot.mode, "release_id": slot.release_id},
                    separators=(",", ":"),
                    sort_keys=True,
                )
            )
        else:
            release_id = check_release_state(
                args.release_json,
                args.tag,
                args.version,
                args.target_sha,
                args.stage,
                expected_release_id=args.expected_release_id,
                expected_asset_count=args.expected_asset_count,
            )
            print(release_id)
    except BuildScopeReleaseStateError as exc:
        parser.exit(1, f"BuildScope release state audit failed: {exc}{os.linesep}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
