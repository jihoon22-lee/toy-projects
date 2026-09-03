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
    entries: list[dict[str, Any]], requested: Sequence[str] | None
) -> list[dict[str, Any]]:
    if not requested:
        return entries
    selected: list[dict[str, Any]] = []
    wanted = set(requested)
    for entry in entries:
        qualified = f"{entry.get('group', '')}/{entry.get('name', '')}"
        if str(entry.get("name", "")) in wanted or qualified in wanted:
            selected.append(entry)
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
    record["checks"].extend(
        {
            "kind": "entry-point",
            "group": entry.get("group", ""),
            "name": entry.get("name", ""),
            "value": entry.get("value", ""),
            "location": entry.get("location", {}),
            "status": status,
            "reason": "configured interpreter is unavailable",
        }
        for entry in prepared.entries
    )
    return record
