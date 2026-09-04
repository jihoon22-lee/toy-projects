"""Small runtime orchestration records shared by the public runtime API."""

from __future__ import annotations

import os
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from envlens.probe import ProbeError, resolve_interpreter


@dataclass
class PreparedRuntime:
    root: Path
    project_info: dict[str, Any]
    entries: list[dict[str, Any]]
    modules: list[str]
    source_files: list[Path]
    interpreters: list[str | Path]
    project_check: dict[str, Any] | None = None


def interpreter_record(
    requested: str, *, status: str, resolved: Path | None = None
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "requested_executable": requested,
        "status": status,
        "checks": [],
    }
    if resolved is not None:
        result["resolved_executable"] = str(resolved)
    return result


def resolve_for_runtime(value: str | Path) -> tuple[str, Path | None]:
    requested = str(value)
    path = Path(requested)
    if not path.exists():
        return "missing-interpreter", None
    if not path.is_file() or not os.access(path, os.X_OK):
        return "invalid-interpreter", None
    try:
        resolved, _requested = resolve_interpreter(path)
    except ProbeError:
        return "invalid-interpreter", None
    return "ready", resolved


def select_entry_points(
    entries: list[dict[str, Any]],
    requested: Sequence[str] | None,
    *,
    location_path: str | Path | None = None,
) -> list[dict[str, Any]]:
    if not requested:
        return entries
    selected: list[dict[str, Any]] = []
    wanted = {str(value) for value in requested}
    matched: set[str] = set()
    for entry in entries:
        qualified = f"{entry.get('group', '')}/{entry.get('name', '')}"
        name = str(entry.get("name", ""))
        matches = {name, qualified}.intersection(wanted)
        if matches:
            selected.append(entry)
            matched.update(matches)
    missing = sorted(wanted - matched)
    for selector in missing:
        group, separator, name = selector.partition("/")
        if not separator:
            group, name = "", selector
        selected.append(
            {
                "group": group,
                "name": name,
                "value": "",
                "target": None,
                "status": "missing",
                "requested": selector,
                "error_code": "missing-entry-point",
                "reason": f"requested entry point {selector!r} was not found",
                "location": {
                    "path": str(location_path) if location_path is not None else "",
                    "line": 0,
                },
            }
        )
    return selected


def unavailable_record(
    requested: str,
    status: str,
    prepared: PreparedRuntime,
) -> dict[str, Any]:
    record = interpreter_record(requested, status=status)
    record["checks"].extend(
        {
            "kind": "import",
            "name": module,
            "status": status,
            "reason": "configured interpreter is unavailable",
        }
        for module in prepared.modules
    )
    record["checks"].append(
        {
            "kind": "compileall",
            "name": str(prepared.root),
            "status": status,
            "files": len(prepared.source_files),
            "reason": "configured interpreter is unavailable",
        }
    )
    record["checks"].extend(_unavailable_entry_check(entry, status) for entry in prepared.entries)
    if prepared.project_check is not None:
        record["checks"].append(dict(prepared.project_check))
    return record


def missing_entry_check(entry: dict[str, Any]) -> dict[str, Any]:
    return {
        "kind": "entry-point",
        "group": entry.get("group", ""),
        "name": entry.get("name", ""),
        "value": entry.get("value", ""),
        "location": entry.get("location", {}),
        "status": "failed",
        "action": "selection",
        "requested": entry.get("requested", entry.get("name", "")),
        "error_code": "missing-entry-point",
        "reason": entry.get(
            "reason",
            f"requested entry point {entry.get('name', '')!r} was not found",
        ),
    }


def _unavailable_entry_check(entry: dict[str, Any], status: str) -> dict[str, Any]:
    if entry.get("status") == "missing":
        return missing_entry_check(entry)
    return {
        "kind": "entry-point",
        "group": entry.get("group", ""),
        "name": entry.get("name", ""),
        "value": entry.get("value", ""),
        "location": entry.get("location", {}),
        "status": status,
        "reason": "configured interpreter is unavailable",
    }
