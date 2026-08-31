"""Bounded, shell-free compilation database ingestion for the v1 snapshot."""

from __future__ import annotations

import json
import os
import stat
from pathlib import Path
from typing import Any

from buildscope import __version__

SCHEMA_VERSION = "buildscope.snapshot/v1"
MAX_DATABASE_BYTES = 64 * 1024 * 1024
MAX_ENTRIES = 100_000


class SnapshotError(ValueError):
    """Raised when an input cannot be represented by the public snapshot contract."""


def _required_string(entry: dict[str, Any], key: str, index: int) -> str:
    value = entry.get(key)
    if not isinstance(value, str) or not value:
        raise SnapshotError(f"entry[{index}].{key} must be a non-empty string")
    return value


def _arguments(entry: dict[str, Any], index: int) -> list[str] | None:
    value = entry.get("arguments")
    if value is None:
        return None
    if not isinstance(value, list) or not value or not all(isinstance(item, str) for item in value):
        raise SnapshotError(f"entry[{index}].arguments must be a non-empty string array")
    return list(value)


def _command(entry: dict[str, Any], index: int) -> str | None:
    value = entry.get("command")
    if value is None:
        return None
    if not isinstance(value, str) or not value:
        raise SnapshotError(f"entry[{index}].command must be a non-empty string")
    return value


def _output(entry: dict[str, Any], index: int) -> str | None:
    value = entry.get("output")
    if value is None:
        return None
    if not isinstance(value, str) or not value:
        raise SnapshotError(f"entry[{index}].output must be a non-empty string")
    return value


def _snapshot_entry(entry: Any, index: int) -> dict[str, Any]:
    if not isinstance(entry, dict):
        raise SnapshotError(f"entry[{index}] must be an object")
    arguments = _arguments(entry, index)
    command = _command(entry, index)
    if (arguments is None) == (command is None):
        raise SnapshotError(f"entry[{index}] must contain exactly one of arguments or command")
    return {
        "arguments": arguments,
        "command": command,
        "directory": _required_string(entry, "directory", index),
        "file": _required_string(entry, "file", index),
        "output": _output(entry, index),
    }


def load_compilation_database(path: Path) -> dict[str, Any]:
    """Read a compile database without executing or normalizing its commands."""

    path = Path(path)
    try:
        with path.open("rb") as database:
            metadata = os.fstat(database.fileno())
            if not stat.S_ISREG(metadata.st_mode):
                raise SnapshotError("compilation database must be a regular file")
            if metadata.st_size > MAX_DATABASE_BYTES:
                raise SnapshotError(f"compilation database exceeds {MAX_DATABASE_BYTES} byte limit")
            raw = database.read(MAX_DATABASE_BYTES + 1)
        if len(raw) > MAX_DATABASE_BYTES:
            raise SnapshotError(f"compilation database exceeds {MAX_DATABASE_BYTES} byte limit")
        payload = json.loads(raw.decode("utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise SnapshotError(f"cannot read compilation database: {error}") from error
    if not isinstance(payload, list):
        raise SnapshotError("compilation database root must be an array")
    if len(payload) > MAX_ENTRIES:
        raise SnapshotError(f"compilation database exceeds {MAX_ENTRIES} entry limit")

    entries = [_snapshot_entry(entry, index) for index, entry in enumerate(payload)]
    entries.sort(
        key=lambda entry: (
            entry["file"],
            entry["directory"],
            entry["command"] or "\0".join(entry["arguments"] or []),
        )
    )
    return {
        "entries": entries,
        "producer": {"name": "buildscope", "version": __version__},
        "schema_version": SCHEMA_VERSION,
        "source": {"entry_count": len(entries), "path": str(path)},
    }


def dumps_snapshot(snapshot: dict[str, Any], *, pretty: bool = False) -> str:
    """Serialize deterministically for process and file consumers."""

    if pretty:
        return json.dumps(snapshot, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    return json.dumps(snapshot, ensure_ascii=False, separators=(",", ":"), sort_keys=True) + "\n"
