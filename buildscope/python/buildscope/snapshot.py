"""Bounded, shell-free compilation database ingestion for snapshot v2."""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any

from buildscope import __version__
from buildscope._command import looks_windows_path
from buildscope._io import SnapshotIoError, read_bounded_regular
from buildscope._paths import normalize_lexical
from buildscope.normalize import annotate_entry_sets, normalize_entry

SCHEMA_VERSION_V1 = "buildscope.snapshot/v1"
SCHEMA_VERSION = "buildscope.snapshot/v2"
SCHEMA_VERSION_V3 = "buildscope.snapshot/v3"
MAX_DATABASE_BYTES = 64 * 1024 * 1024
MAX_SNAPSHOT_BYTES = 256 * 1024 * 1024
MAX_ENTRIES = 100_000
MAX_FIELD_CHARS = 1024 * 1024


class SnapshotError(ValueError):
    """Raised when an input cannot be represented by the public snapshot contract."""


def _required_string(entry: dict[str, Any], key: str, index: int) -> str:
    value = entry.get(key)
    if not isinstance(value, str) or not value or "\0" in value or len(value) > MAX_FIELD_CHARS:
        raise SnapshotError(f"entry[{index}].{key} must be a non-empty string")
    return value


def _output(entry: dict[str, Any], index: int) -> str | None:
    value = entry.get("output")
    if value is None:
        return None
    if not isinstance(value, str) or not value or "\0" in value or len(value) > MAX_FIELD_CHARS:
        raise SnapshotError(f"entry[{index}].output must be a non-empty string")
    return value


def _snapshot_entry(
    entry: Any,
    index: int,
    *,
    project_root: str,
    database_parent: str,
) -> dict[str, Any]:
    if not isinstance(entry, dict):
        raise SnapshotError(f"entry[{index}] must be an object")
    validated = {
        "arguments": entry.get("arguments"),
        "command": entry.get("command"),
        "directory": _required_string(entry, "directory", index),
        "file": _required_string(entry, "file", index),
        "output": _output(entry, index),
    }
    try:
        return normalize_entry(
            validated,
            index=index,
            project_root=project_root,
            database_parent=database_parent,
        )
    except ValueError as error:
        raise SnapshotError(str(error)) from error


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise SnapshotError(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise SnapshotError(f"non-standard JSON constant is forbidden: {value}")


def _project_root(path: Path, project_root: Path | str | None) -> str:
    if project_root is None:
        return str(path.parent)
    raw = os.fspath(project_root)
    if not raw or "\0" in raw or len(raw) > MAX_FIELD_CHARS:
        raise SnapshotError("project root must be a non-empty bounded path")
    if os.name != "nt" and looks_windows_path(raw):
        return normalize_lexical(raw, raw, "windows")
    return str(Path(raw).resolve(strict=False))


def load_compilation_database(
    path: Path, *, project_root: Path | str | None = None
) -> dict[str, Any]:
    """Read and normalize a compile database without executing its commands."""

    try:
        path, raw = read_bounded_regular(Path(path), MAX_DATABASE_BYTES)
    except SnapshotIoError as error:
        raise SnapshotError(f"cannot read compilation database: {error}") from error
    try:
        root = _project_root(path, project_root)
    except (OSError, RuntimeError) as error:
        raise SnapshotError(f"cannot resolve compilation database paths: {error}") from error
    try:
        payload = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_reject_duplicate_keys,
            parse_constant=_reject_constant,
        )
    except SnapshotError:
        raise
    except (RecursionError, UnicodeError, json.JSONDecodeError) as error:
        raise SnapshotError(f"cannot read compilation database: {error}") from error
    if not isinstance(payload, list):
        raise SnapshotError("compilation database root must be an array")
    if len(payload) > MAX_ENTRIES:
        raise SnapshotError(f"compilation database exceeds {MAX_ENTRIES} entry limit")

    entries = [
        _snapshot_entry(
            entry,
            index,
            project_root=root,
            database_parent=str(path.parent),
        )
        for index, entry in enumerate(payload)
    ]
    annotate_entry_sets(entries)
    entries.sort(
        key=lambda entry: (
            entry["normalized"]["source"]["path"],
            entry["normalized"]["configuration"],
            entry["state"]["entry_index"],
        )
    )
    return {
        "entries": entries,
        "producer": {"name": "buildscope", "version": __version__},
        "schema_version": SCHEMA_VERSION,
        "source": {
            "entry_count": len(entries),
            "path": str(path),
            "project_root": root,
        },
    }


def dumps_snapshot(snapshot: dict[str, Any], *, pretty: bool = False) -> str:
    """Serialize deterministically for process and file consumers."""

    if pretty:
        rendered = (
            json.dumps(snapshot, allow_nan=False, ensure_ascii=False, indent=2, sort_keys=True)
            + "\n"
        )
    else:
        rendered = (
            json.dumps(
                snapshot,
                allow_nan=False,
                ensure_ascii=False,
                separators=(",", ":"),
                sort_keys=True,
            )
            + "\n"
        )
    if len(rendered.encode("utf-8")) > MAX_SNAPSHOT_BYTES:
        raise SnapshotError(f"snapshot exceeds {MAX_SNAPSHOT_BYTES} byte limit")
    return rendered


def snapshot_for_schema(snapshot: dict[str, Any], schema: str) -> dict[str, Any]:
    """Project the normalized snapshot onto a supported public contract."""

    if schema == "v3":
        if snapshot.get("schema_version") != SCHEMA_VERSION_V3:
            raise SnapshotError("v3 snapshots require include analysis")
        return snapshot
    if schema == "v2":
        if snapshot.get("schema_version") == SCHEMA_VERSION_V3:
            for entry in snapshot["entries"]:
                entry.pop("include_analysis", None)
            snapshot["schema_version"] = SCHEMA_VERSION
        return snapshot
    if schema != "v1":
        raise SnapshotError(f"unsupported snapshot schema: {schema}")
    entries = []
    for entry in snapshot["entries"]:
        arguments = entry["arguments"]
        entries.append(
            {
                "arguments": arguments,
                "command": None if arguments is not None else entry["command"],
                "directory": entry["directory"],
                "file": entry["file"],
                "output": entry["output"],
            }
        )
    return {
        "entries": entries,
        "producer": dict(snapshot["producer"]),
        "schema_version": SCHEMA_VERSION_V1,
        "source": {
            "entry_count": len(entries),
            "path": snapshot["source"]["path"],
        },
    }
