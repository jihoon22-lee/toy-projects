"""Run construction and comparison without inferred successes or retries."""

from __future__ import annotations

import json
import math
from collections import Counter, defaultdict
from dataclasses import replace
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from . import __version__
from .adapters import Limits, read_report
from .io import Document, InputError, digest

DEFAULT_LIMITS = Limits()
FAILURES = {"failed", "error"}
EXECUTED = {"passed", "failed", "error"}
CRITICAL = {
    "unsupported-status",
    "empty-report",
    "count-mismatch",
    "suite-error",
    "missing-name",
    "invalid-attempt",
    "unsupported-retry",
    "incomplete-dashboard",
    "input-error",
    "duplicate-identity",
    "missing-shard",
    "unexpected-test",
    "missing-expected-test",
}


def stamp() -> str:
    return datetime.now(timezone.utc).isoformat()


def header(kind: str) -> Document:
    return {
        "schema": f"testlens.{kind}/v1",
        "producer": {"name": "testlens", "version": __version__},
    }


def normalize_path(value: str | None, root: Path | None) -> str | None:
    if value is None:
        return None
    if root is not None:
        try:
            return Path(value).resolve().relative_to(root.resolve()).as_posix()
        except ValueError:
            pass
    return value.replace("\\", "/")


def identity(record: Document, project: str) -> str:
    # Preserve test names and parameter IDs exactly. Never depend on checkout or input location.
    parts = [project, record["granularity"], record["suite"], record["classname"], record["name"]]
    return digest(json.dumps(parts, ensure_ascii=False, separators=(",", ":")).encode())


def aggregate(records: list[Document]) -> tuple[list[Document], list[Document]]:
    grouped: dict[str, list[Document]] = defaultdict(list)
    for record in records:
        grouped[record["id"]].append(record)
    tests = []
    diagnostics = []
    for key, observations in sorted(grouped.items()):
        attempts = [r["attempt"] for r in observations]
        # Explicit attempts must be unique and contiguous: otherwise final state is unknown.
        explicit = all(isinstance(a, int) for a in attempts)
        retry = explicit and sorted(attempts) == list(range(1, len(attempts) + 1))
        conflict = len(observations) > 1 and not retry
        if explicit and not retry:
            conflict = True
        if conflict:
            diagnostics.append(
                {"code": "duplicate-identity", "message": f"Ambiguous observations for {key}"}
            )
        ordered = sorted(observations, key=lambda r: r["attempt"] or 0)
        last = ordered[-1]
        tests.append(
            {
                "id": key,
                "name": last["name"],
                "suite": last["suite"],
                "classname": last["classname"],
                "granularity": last["granularity"],
                "status": "unknown" if conflict else last["status"],
                "duration_seconds": None if conflict else last["duration_seconds"],
                "any_failure": any(r["status"] in FAILURES for r in ordered),
                "ambiguous": conflict,
                "attempts": ordered,
            }
        )
    return tests, diagnostics


def collect(
    paths: list[Path],
    *,
    project: str,
    run_id: str,
    dialect: str = "auto",
    root: Path | None = None,
    metadata: Document | None = None,
    scope: str = "default",
    declared_complete: bool = False,
    expected_shards: list[str] | None = None,
    manifest: Document | None = None,
    limits: Limits = DEFAULT_LIMITS,
    allow_partial: bool = False,
    executed_at: str | None = None,
) -> Document:
    if not project.strip() or not run_id.strip() or not scope.strip():
        raise InputError("project, run-id and scope must not be empty")
    if len(paths) > limits.files:
        raise InputError("Input file budget exceeded")
    sources: list[Document] = []
    observations: list[Document] = []
    diagnostics: list[Document] = []
    seen: set[str] = set()
    total = 0
    for path in paths:
        try:
            remaining = limits.total_bytes - total
            if remaining <= 0:
                raise InputError("Total input byte budget exceeded")
            file_limits = replace(limits, file_bytes=min(limits.file_bytes, remaining))
            source, records, issues = read_report(path, dialect, file_limits)
            total += source["bytes"]
            if total > limits.total_bytes:
                raise InputError("Total input byte budget exceeded")
            if source["id"] in seen:
                diagnostics.append({"code": "duplicate-input", "message": str(path)})
                continue
            if len(observations) + len(records) > limits.cases:
                raise InputError("Total testcase budget exceeded")
            seen.add(source["id"])
            source["path"] = normalize_path(str(path), root)
            sources.append(source)
            diagnostics.extend(issues)
            for record in records:
                record["id"] = identity(record, project)
                record["source_file_normalized"] = normalize_path(record["source_file"], root)
            observations.extend(records)
        except InputError as exc:
            if not allow_partial:
                raise
            diagnostics.append({"code": "input-error", "message": f"{path}: {exc}"})
            break  # Budgets cannot be bypassed by partial mode.
    tests, issues = aggregate(observations)
    diagnostics.extend(issues)
    present_shards = sorted({r["shard"] for r in observations if r["shard"] is not None})
    if expected_shards and set(expected_shards) != set(present_shards):
        diagnostics.append(
            {
                "code": "missing-shard",
                "message": f"Expected {sorted(expected_shards)}, observed {present_shards}",
            }
        )
    manifest_digest = None
    if manifest is not None:
        expected = manifest.get("test_ids")
        if not isinstance(expected, list) or not all(isinstance(x, str) for x in expected):
            raise InputError("Manifest must contain a test_ids array of normalized IDs")
        manifest_digest = digest(json.dumps(sorted(expected)).encode())
        actual = {t["id"] for t in tests}
        for key in sorted(set(expected) - actual):
            diagnostics.append({"code": "missing-expected-test", "message": key})
        for key in sorted(actual - set(expected)):
            diagnostics.append({"code": "unexpected-test", "message": key})
    complete = declared_complete and not any(d["code"] in CRITICAL for d in diagnostics)
    return {
        **header("run"),
        "run_id": run_id,
        "project": project,
        "collected_at": stamp(),
        "executed_at": executed_at,
        "metadata": metadata or {},
        "scope": scope,
        "complete": complete,
        "declared_complete": declared_complete,
        "source_root": str(root.resolve()) if root else None,
        "expected_shards": expected_shards or [],
        "observed_shards": present_shards,
        "manifest_digest": manifest_digest,
        "sources": sources,
        "tests": tests,
        "diagnostics": diagnostics,
        "summary": dict(Counter(t["status"] for t in tests)),
    }


def cohort(run: Document) -> tuple[Any, ...]:
    metadata = run["metadata"]
    # Runner/dialect differences are explicit; CTest dashboard and CTest JUnit may be compared
    # only when normalized identities actually match (otherwise they appear added/missing).
    return (
        run["project"],
        run["scope"],
        metadata.get("branch"),
        metadata.get("platform"),
        metadata.get("environment"),
        metadata.get("runner"),
        tuple(
            sorted(
                {
                    "runner-target" if s["dialect"] in {"ctest", "ctest-junit"} else "test-case"
                    for s in run["sources"]
                }
            )
        ),
    )


def compare(
    before: Document,
    after: Document,
    relative: float = 0.2,
    absolute: float = 0.1,
    aliases: dict[str, str] | None = None,
) -> Document:
    if cohort(before) != cohort(after):
        raise InputError("Runs have different project/scope/environment/runner/granularity cohorts")
    aliases = aliases or {}
    if len(set(aliases.values())) != len(aliases):
        raise InputError("Aliases must map one-to-one")
    old = {aliases.get(t["id"], t["id"]): t for t in before["tests"]}
    if len(old) != len(before["tests"]):
        raise InputError("Alias mapping collides with an existing test")
    new = {t["id"]: t for t in after["tests"]}
    if set(aliases) - {t["id"] for t in before["tests"]} or set(aliases.values()) - set(new):
        raise InputError("Aliases must reference existing baseline and current IDs")
    changes: list[Document] = []
    for key in sorted(old.keys() | new.keys()):
        left, right = old.get(key), new.get(key)
        a, b = left["status"] if left else None, right["status"] if right else None
        if left is None:
            kind = "new-test-failure" if b in FAILURES else "added"
        elif right is None:
            kind = "missing"
        elif a == "passed" and b in FAILURES:
            kind = "new-failure"
        elif a in FAILURES and b == "passed":
            kind = "recovered"
        elif a in FAILURES and b in FAILURES:
            kind = "persistent-failure"
        elif a != b:
            kind = "status-changed"
        else:
            kind = "unchanged"
        ltime = left["duration_seconds"] if left else None
        rtime = right["duration_seconds"] if right else None
        delta: float | None = None
        ratio: float | None = None
        if ltime is not None and rtime is not None and a in EXECUTED and b in EXECUTED:
            delta = float(rtime) - float(ltime)
            if ltime > 0:
                candidate = delta / float(ltime)
                ratio = candidate if math.isfinite(candidate) else None
        slowdown = (
            delta is not None and ratio is not None and delta >= absolute and ratio >= relative
        )
        changes.append(
            {
                "id": key,
                "name": (right or left or {})["name"],
                "kind": kind,
                "before": a,
                "after": b,
                "before_duration_seconds": ltime,
                "after_duration_seconds": rtime,
                "delta_seconds": delta,
                "relative_change": ratio,
                "slowdown": slowdown,
                "alias_from": left["id"] if left and left["id"] != key else None,
                "before_evidence": left["attempts"] if left else [],
                "after_evidence": right["attempts"] if right else [],
            }
        )
    return {
        **header("diff"),
        "baseline_run_id": before["run_id"],
        "current_run_id": after["run_id"],
        "complete": before["complete"] and after["complete"],
        "missing_interpretation": "absent-from-observed-results",
        "thresholds": {"relative": relative, "absolute_seconds": absolute},
        "changes": changes,
        "summary": dict(Counter(c["kind"] for c in changes)),
    }


def history(runs: list[Document], last: int = 20) -> Document:
    unique: dict[str, Document] = {}
    for run in runs:
        key = run["run_id"]
        if key in unique:
            original = dict(unique[key])
            incoming = dict(run)
            original.pop("collected_at", None)
            incoming.pop("collected_at", None)
            if original != incoming:
                raise InputError(f"Conflicting duplicate run ID: {key}")
        unique[key] = run
    selected = list(unique.values())
    if not selected:
        raise InputError("History requires at least one run")
    if len({cohort(r) for r in selected}) != 1:
        raise InputError("History requires a single comparable cohort; filter inputs first")
    if any(not r["executed_at"] for r in selected):
        raise InputError("History requires explicit executed_at for every run")

    def time_key(run: Document) -> tuple[datetime, str]:
        return datetime.fromisoformat(run["executed_at"].upper().replace("Z", "+00:00")), run[
            "run_id"
        ]

    selected = sorted(selected, key=time_key)[-last:]
    grouped: dict[str, list[Document]] = defaultdict(list)
    names = {}
    for run in selected:
        for test in run["tests"]:
            names[test["id"]] = test["name"]
            grouped[test["id"]].append(
                {
                    "run_id": run["run_id"],
                    "executed_at": run["executed_at"],
                    "status": test["status"],
                    "any_failure": test["any_failure"],
                    "duration_seconds": test["duration_seconds"],
                    "run_complete": run["complete"],
                }
            )
    tests = []
    for key, observations in sorted(grouped.items()):
        executed = [o for o in observations if o["status"] in EXECUTED]
        failures = sum(o["status"] in FAILURES for o in executed)
        any_failures = sum(o["any_failure"] for o in executed)
        tests.append(
            {
                "id": key,
                "name": names[key],
                "observations": observations,
                "executed_count": len(executed),
                "failure_count": failures,
                "any_failure_count": any_failures,
                "failure_rate": failures / len(executed) if executed else None,
                "any_failure_rate": any_failures / len(executed) if executed else None,
                "excluded_count": len(observations) - len(executed),
                "missing_count": len(selected) - len(observations),
            }
        )
    return {
        **header("history"),
        "run_ids": [r["run_id"] for r in selected],
        "window": last,
        "period": {"start": selected[0]["executed_at"], "end": selected[-1]["executed_at"]},
        "complete": all(r["complete"] for r in selected),
        "tests": tests,
        "interpretation": "Observed failure frequency, not a flaky-test diagnosis",
    }


def policy_violations(document: Document, policies: set[str]) -> list[str]:
    found = set()
    if not document.get("complete", True):
        found.add("incomplete")
    if document["schema"] == "testlens.run/v1":
        if any(t["status"] in FAILURES for t in document["tests"]):
            found.add("failure")
        if any(t["status"] == "error" for t in document["tests"]):
            found.add("error")
    elif document["schema"] == "testlens.diff/v1":
        for change in document["changes"]:
            found.add(change["kind"])
            if change["after"] in FAILURES:
                found.add("failure")
            if change["after"] == "error":
                found.add("error")
            if change["slowdown"]:
                found.add("slowdown")
    return sorted(found & policies)
