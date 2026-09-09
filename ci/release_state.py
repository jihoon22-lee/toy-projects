#!/usr/bin/env python3
"""Validate a product's GitHub Release slot and state transitions.

Publishing writes to a slot that may already hold something. This module is
what stops a run from adopting a release it did not create: the owner marker
embedded in the draft body names the repository, the run, and the exact commit,
and a draft that does not carry this run's marker is never touched.

Nothing here is product-specific. The tag prefix, display name and the marker
namespace come from the product's declaration in ``ci/projects.json`` — the
namespace is per product so two products' releases cannot adopt each other.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import stat
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_registry import load_release_specs  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent


def specs_display(product: str) -> str:
    """Best-effort display name for diagnostics; falls back to the key itself."""

    try:
        return resolve_display_name(product)
    except Exception:
        return product


def resolve_display_name(product: str, repo_root: Path | None = None) -> str:
    """Look up the human-facing name the product declared for its releases."""

    specs = load_release_specs(repo_root or REPO_ROOT)
    if product not in specs:
        raise ReleaseStateError(f"{product} is not a released product")
    return specs[product].display_name

MAX_JSON_BYTES = 20_000_000
MAX_RELEASE_PAGES = 1_000
MAX_RELEASES_PER_PAGE = 100
MAX_RELEASE_ID = (1 << 63) - 1
MAX_TAG_BYTES = 255
MAX_RELEASE_BODY_BYTES = 1_000_000
VERSION_PATTERN = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
SHA_PATTERN = re.compile(r"^[0-9a-f]{40}$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
_OWNER_MARKER_BODY = (
    r"(?P<owner_repo>[A-Za-z0-9](?:[A-Za-z0-9_.-]{0,99})/"
    r"[A-Za-z0-9](?:[A-Za-z0-9_.-]{0,99}))"
    r":(?P<run_id>[1-9][0-9]{0,19})"
    r":(?P<target_sha>[0-9a-f]{40}) -->"
)


def owner_marker_prefix(product: str) -> str:
    return f"<!-- {product}-release-owner:"


def owner_marker_pattern(product: str) -> re.Pattern[str]:
    return re.compile(re.escape(owner_marker_prefix(product)) + _OWNER_MARKER_BODY)


class ReleaseStateError(ValueError):
    """The release slot or state is unsafe."""


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
        raise ReleaseStateError(
            f"{label} cannot be opened safely: {exc}"
        ) from exc
    try:
        opened = os.fstat(descriptor)
        if not stat.S_ISREG(opened.st_mode):
            raise ReleaseStateError(f"{label} must be a regular file")
        if opened.st_size <= 0 or opened.st_size > MAX_JSON_BYTES:
            raise ReleaseStateError(
                f"{label} size is outside the accepted range: {opened.st_size} bytes"
            )
        with os.fdopen(descriptor, "rb", closefd=True) as stream:
            descriptor = -1
            payload = stream.read(MAX_JSON_BYTES + 1)
            after = os.fstat(stream.fileno())
        try:
            named = path.stat(follow_symlinks=False)
        except OSError as exc:
            raise ReleaseStateError(
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
            raise ReleaseStateError(f"{label} changed while it was read")
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    if len(payload) > MAX_JSON_BYTES:
        raise ReleaseStateError(f"{label} exceeds the read bound")
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ReleaseStateError(f"{label} is not valid UTF-8: {exc}") from exc
    try:
        return json.loads(
            text,
            parse_int=_bounded_int,
            parse_float=_bounded_float,
            parse_constant=_reject_constant,
            object_pairs_hook=_unique_object,
        )
    except (json.JSONDecodeError, ValueError) as exc:
        raise ReleaseStateError(f"{label} is not valid JSON: {exc}") from exc


def _validate_inputs(product: str, tag: str, version: str, target_sha: str) -> None:
    if VERSION_PATTERN.fullmatch(version) is None or tag != f"{product}-v{version}":
        raise ReleaseStateError(f"{product} version and tag do not agree")
    if SHA_PATTERN.fullmatch(target_sha) is None:
        raise ReleaseStateError(
            "release target must be a full lowercase commit SHA"
        )


def _validate_run_id(run_id: object) -> int:
    if (
        isinstance(run_id, bool)
        or not isinstance(run_id, int)
        or run_id <= 0
        or run_id > MAX_RELEASE_ID
    ):
        raise ReleaseStateError(f"release run id is invalid: {run_id!r}")
    return run_id


def _validate_owner_marker(product: str, owner_marker: object, target_sha: str) -> str:
    if not isinstance(owner_marker, str):
        raise ReleaseStateError("owner marker must be a string")
    match = owner_marker_pattern(product).fullmatch(owner_marker)
    if match is None:
        raise ReleaseStateError(
            "owner marker has an invalid exact HTML comment format"
        )
    try:
        marker_run_id = _validate_run_id(int(match.group("run_id")))
    except ReleaseStateError as exc:
        raise ReleaseStateError(
            f"owner marker run id is invalid: {match.group('run_id')}"
        ) from exc
    if str(marker_run_id) != match.group("run_id"):
        raise ReleaseStateError("owner marker run id is not canonical")
    if match.group("target_sha") != target_sha:
        raise ReleaseStateError(
            "owner marker target SHA does not match the requested target"
        )
    return owner_marker


def _release_body_bytes(release: dict[str, Any]) -> bytes:
    body = release.get("body")
    if not isinstance(body, str):
        raise ReleaseStateError("release body must be a string")
    try:
        encoded = body.encode("utf-8")
    except UnicodeEncodeError as exc:
        raise ReleaseStateError(
            f"release body is not valid UTF-8: {exc}"
        ) from exc
    if len(encoded) > MAX_RELEASE_BODY_BYTES:
        raise ReleaseStateError("release body exceeds the accepted bound")
    return encoded


def _validate_owner_marker_body(
    product: str,
    release: dict[str, Any], expected_owner_marker: str
) -> None:
    body = _release_body_bytes(release).decode("utf-8")
    if (
        body.count(owner_marker_prefix(product)) != 1
        or body.count(expected_owner_marker) != 1
        or not body.endswith(expected_owner_marker)
    ):
        raise ReleaseStateError(
            "release body must end with the exact current-run owner marker"
        )


def _validate_body_sha256(release: dict[str, Any], expected_body_sha256: str) -> None:
    if SHA256_PATTERN.fullmatch(expected_body_sha256) is None:
        raise ReleaseStateError(
            "expected release body SHA-256 must be lowercase hexadecimal"
        )
    actual_body_sha256 = hashlib.sha256(_release_body_bytes(release)).hexdigest()
    if actual_body_sha256 != expected_body_sha256:
        raise ReleaseStateError(
            "release body SHA-256 mismatch: "
            f"{actual_body_sha256} != {expected_body_sha256}"
        )


def _release_id(release: dict[str, Any]) -> int:
    release_id = release.get("id")
    if (
        isinstance(release_id, bool)
        or not isinstance(release_id, int)
        or release_id <= 0
        or release_id > MAX_RELEASE_ID
    ):
        raise ReleaseStateError(f"release id is invalid: {release_id!r}")
    return release_id


def _validate_release_state(
    release: dict[str, Any],
    product: str,
    display_name: str,
    tag: str,
    version: str,
    target_sha: str,
    stage: str,
    *,
    expected_release_id: int | None = None,
    expected_asset_count: int | None = None,
    expected_owner_marker: str | None = None,
    expected_body_sha256: str | None = None,
) -> int:
    if stage not in {"draft", "final"}:
        raise ReleaseStateError(f"invalid release stage: {stage!r}")
    release_id = _release_id(release)
    if expected_release_id is not None and release_id != expected_release_id:
        raise ReleaseStateError(
            f"release id mismatch: {release_id} != {expected_release_id}"
        )
    expected = {
        "tag_name": tag,
        "name": f"{display_name} {version}",
        "prerelease": False,
        "draft": stage == "draft",
    }
    for key, expected_value in expected.items():
        if release.get(key) != expected_value:
            raise ReleaseStateError(
                f"release {key} mismatch: {release.get(key)!r} != {expected_value!r}"
            )
    published_at = release.get("published_at")
    if stage == "draft":
        if published_at is not None:
            raise ReleaseStateError(
                f"draft release must not have published_at: {published_at!r}"
            )
    elif not isinstance(published_at, str) or not published_at.strip():
        raise ReleaseStateError(
            f"final release must have published_at: {published_at!r}"
        )
    if expected_asset_count is not None:
        assets = release.get("assets")
        if not isinstance(assets, list) or len(assets) != expected_asset_count:
            actual = len(assets) if isinstance(assets, list) else "not-an-array"
            raise ReleaseStateError(
                f"release asset count mismatch: {actual!r} != {expected_asset_count}"
            )
    if expected_owner_marker is not None:
        expected_owner_marker = _validate_owner_marker(
            product,
            expected_owner_marker, target_sha
        )
        _validate_owner_marker_body(product, release, expected_owner_marker)
    if expected_body_sha256 is not None:
        _validate_body_sha256(release, expected_body_sha256)
    return release_id


def _matching_release_entries(
    release_pages_json: Path, tag: str
) -> list[dict[str, Any]]:
    pages = _read_json(release_pages_json, "paginated release listing")
    if not isinstance(pages, list) or len(pages) > MAX_RELEASE_PAGES:
        raise ReleaseStateError("paginated release listing has invalid pages")
    matches: list[dict[str, Any]] = []
    for page_index, page in enumerate(pages):
        if not isinstance(page, list) or len(page) > MAX_RELEASES_PER_PAGE:
            raise ReleaseStateError(
                f"release listing page {page_index} has invalid shape or size"
            )
        for release_index, release in enumerate(page):
            if not isinstance(release, dict):
                raise ReleaseStateError(
                    f"release listing entry {page_index}:{release_index} is not an object"
                )
            listed_tag = release.get("tag_name")
            if (
                not isinstance(listed_tag, str)
                or len(listed_tag.encode("utf-8")) > MAX_TAG_BYTES
            ):
                raise ReleaseStateError(
                    f"release listing entry {page_index}:{release_index} has invalid tag"
                )
            if listed_tag == tag:
                matches.append(release)
    return matches


def check_release_state(
    release_json: Path,
    product: str,
    display_name: str,
    tag: str,
    version: str,
    target_sha: str,
    stage: str,
    *,
    expected_release_id: int | None = None,
    expected_asset_count: int | None = None,
    expected_owner_marker: str | None = None,
    expected_body_sha256: str | None = None,
) -> int:
    """Validate one release response and return its positive numeric ID."""

    _validate_inputs(product, tag, version, target_sha)
    release = _read_json(release_json, "release metadata")
    if not isinstance(release, dict):
        raise ReleaseStateError("release metadata must be a JSON object")
    return _validate_release_state(
        release,
        product,
        display_name,
        tag,
        version,
        target_sha,
        stage,
        expected_release_id=expected_release_id,
        expected_asset_count=expected_asset_count,
        expected_owner_marker=expected_owner_marker,
        expected_body_sha256=expected_body_sha256,
    )


def inspect_release_slot(
    release_pages_json: Path,
    product: str,
    display_name: str,
    tag: str,
    version: str,
    target_sha: str,
) -> ReleaseSlot:
    """Return an empty/final slot and reject every pre-existing draft."""

    _validate_inputs(product, tag, version, target_sha)
    matches = _matching_release_entries(release_pages_json, tag)
    if not matches:
        return ReleaseSlot(mode="empty", release_id=None)
    if len(matches) != 1:
        raise ReleaseStateError(
            f"release slot contains {len(matches)} entries for {tag}"
        )
    release = matches[0]
    if release.get("draft") is True:
        release_id = _release_id(release)
        raise ReleaseStateError(
            f"private draft {release_id} already occupies {tag}; inspect or delete it explicitly"
        )
    release_id = _validate_release_state(
        release,
        product,
        display_name,
        tag,
        version,
        target_sha,
        "final",
    )
    return ReleaseSlot(mode="final", release_id=release_id)


def _validate_owned_draft(
    release: dict[str, Any],
    product: str,
    display_name: str,
    tag: str,
    version: str,
    expected_owner_marker: str,
    expected_body_sha256: str,
) -> int:
    release_id = _release_id(release)
    expected = {
        "tag_name": tag,
        "name": f"{display_name} {version}",
        "draft": True,
        "prerelease": False,
        "published_at": None,
    }
    for key, expected_value in expected.items():
        if release.get(key) != expected_value:
            raise ReleaseStateError(
                f"owned draft {key} mismatch: {release.get(key)!r} != {expected_value!r}"
            )
    assets = release.get("assets")
    if not isinstance(assets, list) or assets:
        actual = len(assets) if isinstance(assets, list) else "not-an-array"
        raise ReleaseStateError(
            f"owned draft must have zero assets: {actual!r}"
        )
    _validate_owner_marker_body(product, release, expected_owner_marker)
    _validate_body_sha256(release, expected_body_sha256)
    return release_id


def recover_owned_draft(
    release_pages_json: Path,
    product: str,
    display_name: str,
    tag: str,
    version: str,
    target_sha: str,
    owner_marker: str,
    expected_body_sha256: str,
) -> int:
    """Recover one private draft created by this exact workflow run.

    The owner marker is a terminal HTML comment with the strict form
    ``<!-- <product>-release-owner:<owner/repo>:<run_id>:<40hexsha> -->``.
    The annotated tag's peeled SHA is authoritative; the API's
    ``target_commitish`` field is intentionally not part of this check.
    """

    _validate_inputs(product, tag, version, target_sha)
    owner_marker = _validate_owner_marker(product, owner_marker, target_sha)
    matches = _matching_release_entries(release_pages_json, tag)
    if not matches:
        raise ReleaseStateError(
            f"no private draft found for recovery at {tag}"
        )
    if len(matches) != 1:
        raise ReleaseStateError(
            f"release slot contains {len(matches)} entries for {tag}"
        )
    return _validate_owned_draft(
        matches[0], product, display_name, tag, version, owner_marker, expected_body_sha256
    )


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
    slot_parser.add_argument("product")
    slot_parser.add_argument("tag")
    slot_parser.add_argument("version")
    slot_parser.add_argument("target_sha")

    recover_parser = subparsers.add_parser(
        "recover-owned-draft",
        help="recover one current-run-owned zero-asset private draft",
    )
    recover_parser.add_argument("release_pages_json", type=Path)
    recover_parser.add_argument("product")
    recover_parser.add_argument("tag")
    recover_parser.add_argument("version")
    recover_parser.add_argument("target_sha")
    recover_parser.add_argument("owner_marker")
    recover_parser.add_argument("expected_body_sha256")

    state_parser = subparsers.add_parser("state", help="validate one release response")
    state_parser.add_argument("release_json", type=Path)
    state_parser.add_argument("product")
    state_parser.add_argument("tag")
    state_parser.add_argument("version")
    state_parser.add_argument("target_sha")
    state_parser.add_argument("--stage", choices=("draft", "final"), required=True)
    state_parser.add_argument("--expected-release-id", type=_positive_id)
    state_parser.add_argument("--expected-asset-count", type=int)
    state_parser.add_argument("--expected-owner-marker")
    state_parser.add_argument("--expected-body-sha256")

    args = parser.parse_args(argv)
    try:
        if args.command == "slot":
            slot = inspect_release_slot(
                args.release_pages_json,
                args.product,
                resolve_display_name(args.product),
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
        elif args.command == "recover-owned-draft":
            release_id = recover_owned_draft(
                args.release_pages_json,
                args.product,
                resolve_display_name(args.product),
                args.tag,
                args.version,
                args.target_sha,
                args.owner_marker,
                args.expected_body_sha256,
            )
            print(release_id)
        else:
            release_id = check_release_state(
                args.release_json,
                args.product,
                resolve_display_name(args.product),
                args.tag,
                args.version,
                args.target_sha,
                args.stage,
                expected_release_id=args.expected_release_id,
                expected_asset_count=args.expected_asset_count,
                expected_owner_marker=args.expected_owner_marker,
                expected_body_sha256=args.expected_body_sha256,
            )
            print(release_id)
    except ReleaseStateError as exc:
        display = specs_display(args.product)
        parser.exit(1, f"{display} release state audit failed: {exc}{os.linesep}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
