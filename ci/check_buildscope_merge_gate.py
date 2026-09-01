#!/usr/bin/env python3
"""Validate the exact successful BuildScope Merge Gate workflow run.

The release workflow receives two independent GitHub API responses.  The
Checks API response identifies the newest ``Merge Gate`` check-run for the
target commit; the Actions API response proves that the URL belongs to the
expected push workflow run.  This module deliberately keeps those audits
separate so that a stale successful check cannot be substituted for a newer
pending or failed run, and so that a check-run URL cannot be trusted without
verifying the workflow-run identity behind it.
"""

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
MAX_CHECK_RUNS = 1_000
MAX_JSON_NODES = 100_000
MAX_JSON_DEPTH = 100
MAX_GITHUB_ID = (1 << 63) - 1
MAX_ID_DIGITS = 20
MAX_REPOSITORY_BYTES = 200
SHA_PATTERN = re.compile(r"^[0-9a-f]{40}$")
REPOSITORY_PATTERN = re.compile(
    r"^[A-Za-z0-9][A-Za-z0-9._-]*/[A-Za-z0-9][A-Za-z0-9._-]*$"
)
_POSITIVE_URL_ID = r"[1-9][0-9]{0,19}"


class BuildScopeMergeGateError(ValueError):
    """The Merge Gate check or workflow run does not satisfy the contract."""


@dataclass(frozen=True)
class MergeGateSelection:
    """The trusted workflow run selected from a Checks API response."""

    check_run_id: int
    workflow_run_id: int
    details_url: str


def _bounded_int(raw: str) -> int:
    if len(raw.lstrip("-")) > MAX_ID_DIGITS:
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


def _stat_signature(info: os.stat_result) -> tuple[int, ...]:
    return (
        info.st_dev,
        info.st_ino,
        stat.S_IFMT(info.st_mode),
        info.st_nlink,
        info.st_size,
        info.st_mtime_ns,
        info.st_ctime_ns,
    )


def _read_json(path: Path, label: str) -> Any:
    """Read one bounded regular file without following a final symlink."""

    nofollow = getattr(os, "O_NOFOLLOW", None)
    if nofollow is None:
        raise BuildScopeMergeGateError(
            "this platform does not provide O_NOFOLLOW for safe JSON audits"
        )
    flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | nofollow
    # Opening a FIFO without O_NONBLOCK could hang before fstat can reject it.
    flags |= getattr(os, "O_NONBLOCK", 0)
    try:
        descriptor = os.open(path, flags)
    except OSError as exc:
        raise BuildScopeMergeGateError(
            f"{label} cannot be opened safely: {exc}"
        ) from exc

    try:
        initial = os.fstat(descriptor)
        if not stat.S_ISREG(initial.st_mode):
            raise BuildScopeMergeGateError(f"{label} must be a regular file")
        if initial.st_size <= 0 or initial.st_size > MAX_JSON_BYTES:
            raise BuildScopeMergeGateError(
                f"{label} size is outside the accepted range: {initial.st_size} bytes"
            )
        with os.fdopen(descriptor, "rb", closefd=True) as stream:
            descriptor = -1
            payload = stream.read(MAX_JSON_BYTES + 1)
            final = os.fstat(stream.fileno())
        try:
            named = path.stat(follow_symlinks=False)
        except OSError as exc:
            raise BuildScopeMergeGateError(
                f"{label} cannot be rechecked: {exc}"
            ) from exc
        if _stat_signature(final) != _stat_signature(initial) or _stat_signature(
            named
        ) != _stat_signature(initial):
            raise BuildScopeMergeGateError(f"{label} changed while it was read")
    except BuildScopeMergeGateError:
        raise
    except OSError as exc:
        raise BuildScopeMergeGateError(f"{label} cannot be read: {exc}") from exc
    finally:
        if descriptor >= 0:
            os.close(descriptor)

    if len(payload) > MAX_JSON_BYTES:
        raise BuildScopeMergeGateError(f"{label} exceeds the read bound")
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise BuildScopeMergeGateError(f"{label} is not valid UTF-8: {exc}") from exc
    try:
        value = json.loads(
            text,
            parse_int=_bounded_int,
            parse_float=_bounded_float,
            parse_constant=_reject_constant,
            object_pairs_hook=_unique_object,
        )
    except (json.JSONDecodeError, ValueError, MemoryError, RecursionError) as exc:
        raise BuildScopeMergeGateError(f"{label} is not valid JSON: {exc}") from exc

    _bound_json_shape(value, label)
    return value


def _bound_json_shape(value: Any, label: str) -> None:
    """Reject pathological decoded JSON trees before field inspection."""

    pending: list[tuple[Any, int]] = [(value, 0)]
    nodes = 0
    while pending:
        item, depth = pending.pop()
        nodes += 1
        if nodes > MAX_JSON_NODES:
            raise BuildScopeMergeGateError(f"{label} contains too many JSON values")
        if depth > MAX_JSON_DEPTH:
            raise BuildScopeMergeGateError(f"{label} is nested too deeply")
        if isinstance(item, dict):
            pending.extend((child, depth + 1) for child in item.values())
        elif isinstance(item, list):
            pending.extend((child, depth + 1) for child in item)


def _validate_repository(repository: str) -> None:
    if (
        len(repository.encode("utf-8")) > MAX_REPOSITORY_BYTES
        or REPOSITORY_PATTERN.fullmatch(repository) is None
    ):
        raise BuildScopeMergeGateError(
            f"repository must be an unescaped owner/name pair: {repository!r}"
        )


def _validate_sha(target_sha: str) -> None:
    if SHA_PATTERN.fullmatch(target_sha) is None:
        raise BuildScopeMergeGateError(
            "target SHA must be a full lowercase 40-character commit SHA"
        )


def _positive_id(value: object, label: str) -> int:
    if (
        isinstance(value, bool)
        or not isinstance(value, int)
        or value <= 0
        or value > MAX_GITHUB_ID
    ):
        raise BuildScopeMergeGateError(f"{label} must be a positive integer")
    return value


def _url_id(value: str, label: str) -> int:
    if not value.isascii() or not value.isdecimal() or value.startswith("0"):
        raise BuildScopeMergeGateError(f"{label} must be a positive decimal ID")
    try:
        parsed = int(value)
    except ValueError as exc:
        raise BuildScopeMergeGateError(f"{label} is not a valid decimal ID") from exc
    return _positive_id(parsed, label)


def _details_url(repository: str, value: object) -> tuple[str, int]:
    if not isinstance(value, str):
        raise BuildScopeMergeGateError("Merge Gate details_url must be a string")
    pattern = re.compile(
        rf"^https://github\.com/{re.escape(repository)}/actions/runs/"
        rf"({_POSITIVE_URL_ID})/job/({_POSITIVE_URL_ID})$"
    )
    match = pattern.fullmatch(value)
    if match is None:
        raise BuildScopeMergeGateError(
            f"Merge Gate details_url is not exact for {repository}: {value!r}"
        )
    return value, _url_id(match.group(1), "workflow run ID in details_url")


def _check_runs(value: Any) -> tuple[list[dict[str, Any]], int]:
    if not isinstance(value, dict):
        raise BuildScopeMergeGateError("check-runs response must be a JSON object")
    total_count = value.get("total_count")
    check_runs = value.get("check_runs")
    if (
        isinstance(total_count, bool)
        or not isinstance(total_count, int)
        or not isinstance(check_runs, list)
        or total_count != len(check_runs)
    ):
        raise BuildScopeMergeGateError(
            "check-runs total_count must be an exact count of check_runs"
        )
    if len(check_runs) > MAX_CHECK_RUNS:
        raise BuildScopeMergeGateError(
            f"check-runs response contains too many runs: {len(check_runs)}"
        )
    entries: list[dict[str, Any]] = []
    for index, item in enumerate(check_runs):
        if not isinstance(item, dict):
            raise BuildScopeMergeGateError(
                f"check-runs entry {index} must be a JSON object"
            )
        entries.append(item)
    return entries, total_count


def select_merge_gate(
    check_runs_json: Path,
    target_sha: str,
    repository: str,
) -> MergeGateSelection:
    """Select and validate the newest exact successful Merge Gate check-run."""

    _validate_sha(target_sha)
    _validate_repository(repository)
    response = _read_json(check_runs_json, "check-runs response")
    entries, _ = _check_runs(response)

    candidates: list[tuple[int, dict[str, Any]]] = []
    for index, entry in enumerate(entries):
        if entry.get("name") != "Merge Gate":
            continue
        if entry.get("head_sha") != target_sha:
            continue
        app = entry.get("app")
        if not isinstance(app, dict) or app.get("slug") != "github-actions":
            continue
        candidates.append(
            (
                _positive_id(entry.get("id"), f"check-run {index} id"),
                entry,
            )
        )

    if not candidates:
        raise BuildScopeMergeGateError(
            "no exact GitHub Actions Merge Gate check-run was found"
        )
    newest_id = max(item[0] for item in candidates)
    newest = [entry for run_id, entry in candidates if run_id == newest_id]
    if len(newest) != 1:
        raise BuildScopeMergeGateError(
            f"multiple exact Merge Gate check-runs share ID {newest_id}"
        )
    selected = newest[0]
    status = selected.get("status")
    conclusion = selected.get("conclusion")
    if status != "completed" or conclusion != "success":
        raise BuildScopeMergeGateError(
            "newest exact Merge Gate is not completed successfully; "
            f"status={status!r} conclusion={conclusion!r}"
        )
    details_url, workflow_run_id = _details_url(repository, selected.get("details_url"))
    return MergeGateSelection(
        check_run_id=newest_id,
        workflow_run_id=workflow_run_id,
        details_url=details_url,
    )


def verify_merge_gate_run(
    run_json: Path,
    target_sha: str,
    repository: str,
    expected_workflow_run_id: int,
) -> str:
    """Validate the independent Actions workflow-run response.

    Returns the exact Actions ``html_url`` after all identity and completion
    checks pass.
    """

    _validate_sha(target_sha)
    _validate_repository(repository)
    expected_id = _positive_id(expected_workflow_run_id, "expected workflow run ID")
    value = _read_json(run_json, "workflow-run response")
    if not isinstance(value, dict):
        raise BuildScopeMergeGateError("workflow-run response must be a JSON object")

    actual_id = _positive_id(value.get("id"), "workflow run id")
    if actual_id != expected_id:
        raise BuildScopeMergeGateError(
            f"workflow run id mismatch: {actual_id} != {expected_id}"
        )
    repository_object = value.get("repository")
    head_repository_object = value.get("head_repository")
    if (
        not isinstance(repository_object, dict)
        or repository_object.get("full_name") != repository
    ):
        raise BuildScopeMergeGateError("workflow run repository does not match")
    if (
        not isinstance(head_repository_object, dict)
        or head_repository_object.get("full_name") != repository
    ):
        raise BuildScopeMergeGateError("workflow run head repository does not match")
    if value.get("head_sha") != target_sha:
        raise BuildScopeMergeGateError("workflow run head SHA does not match")

    expected_fields = {
        "name": "CI Quality Gate (ici)",
        "path": ".github/workflows/ci.yml",
        "event": "push",
        "status": "completed",
        "conclusion": "success",
    }
    for field, expected in expected_fields.items():
        if value.get(field) != expected:
            raise BuildScopeMergeGateError(
                f"workflow run {field} mismatch: {value.get(field)!r} != {expected!r}"
            )
    _positive_id(value.get("run_attempt"), "workflow run attempt")

    html_url = value.get("html_url")
    expected_url = f"https://github.com/{repository}/actions/runs/{expected_id}"
    if html_url != expected_url:
        raise BuildScopeMergeGateError(
            f"workflow run html_url mismatch: {html_url!r} != {expected_url!r}"
        )
    return html_url


def _positive_cli_id(value: str) -> int:
    if not value.isascii() or not value.isdecimal():
        raise argparse.ArgumentTypeError("ID must contain decimal digits")
    parsed = int(value)
    if parsed <= 0 or parsed > MAX_GITHUB_ID:
        raise argparse.ArgumentTypeError("ID is outside the accepted range")
    return parsed


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    select_parser = subparsers.add_parser(
        "select", help="select the newest successful Merge Gate check-run"
    )
    select_parser.add_argument("check_runs_json", type=Path)
    select_parser.add_argument("target_sha")
    select_parser.add_argument("repository")

    verify_parser = subparsers.add_parser(
        "verify", help="verify the fetched Actions workflow-run response"
    )
    verify_parser.add_argument("run_json", type=Path)
    verify_parser.add_argument("target_sha")
    verify_parser.add_argument("repository")
    verify_parser.add_argument("expected_workflow_run_id", type=_positive_cli_id)

    args = parser.parse_args(argv)
    try:
        if args.command == "select":
            selection = select_merge_gate(
                args.check_runs_json,
                args.target_sha,
                args.repository,
            )
            print(
                json.dumps(
                    {
                        "details_url": selection.details_url,
                        "workflow_run_id": selection.workflow_run_id,
                    },
                    separators=(",", ":"),
                    sort_keys=True,
                )
            )
        else:
            html_url = verify_merge_gate_run(
                args.run_json,
                args.target_sha,
                args.repository,
                args.expected_workflow_run_id,
            )
            print(
                json.dumps(
                    {
                        "html_url": html_url,
                        "workflow_run_id": args.expected_workflow_run_id,
                    },
                    separators=(",", ":"),
                    sort_keys=True,
                )
            )
    except BuildScopeMergeGateError as exc:
        parser.exit(1, f"BuildScope Merge Gate audit failed: {exc}{os.linesep}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
