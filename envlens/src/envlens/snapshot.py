"""Normalize raw interpreter probes into the public snapshot contract."""

from __future__ import annotations

import json
import math
import re
from collections.abc import Mapping
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from envlens import probe
from envlens.redaction import redact_environment, redact_text, redact_value

MAX_DISTRIBUTIONS = 10_000
MAX_ENVIRONMENT_VARIABLES = 4_096
MAX_COLLECTION_ITEMS = 100_000
MAX_STRING_LENGTH = 65_536
NORMALIZE_NAME_RE = re.compile(r"[-_.]+")


class SnapshotError(ValueError):
    """A stable, user-facing environment snapshot failure."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message


def normalize_project_name(name: str) -> str:
    """Return the PEP 503 normalized distribution name."""

    return NORMALIZE_NAME_RE.sub("-", name).lower()


def _string(value: Any, label: str, *, allow_empty: bool = True) -> str:
    if not isinstance(value, str) or (not allow_empty and not value):
        raise SnapshotError("invalid-probe-schema", f"{label} must be a string")
    if len(value) > MAX_STRING_LENGTH:
        raise SnapshotError("probe-field-too-large", f"{label} exceeds 65536 characters")
    return value


def _object(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise SnapshotError("invalid-probe-schema", f"{label} must be an object")
    return value


def _array(value: Any, label: str, *, maximum: int) -> list[Any]:
    if not isinstance(value, list):
        raise SnapshotError("invalid-probe-schema", f"{label} must be an array")
    if len(value) > maximum:
        raise SnapshotError("probe-field-too-large", f"{label} exceeds {maximum} items")
    return value


def _scalar_mapping(value: Any, label: str) -> dict[str, object]:
    mapping = _object(value, label)
    if len(mapping) > MAX_ENVIRONMENT_VARIABLES:
        raise SnapshotError("probe-field-too-large", f"{label} exceeds 4096 fields")
    normalized: dict[str, object] = {}
    for raw_name, item in mapping.items():
        name = _string(raw_name, f"{label} key", allow_empty=False)
        if isinstance(item, str):
            normalized[name] = _string(item, f"{label}.{name}")
        elif (
            item is None
            or isinstance(item, (bool, int))
            or (isinstance(item, float) and math.isfinite(item))
        ):
            normalized[name] = item
        else:
            raise SnapshotError("invalid-probe-schema", f"{label}.{name} is not a JSON scalar")
    return dict(sorted(normalized.items()))


def _timestamp(value: datetime | None) -> str:
    if value is not None and not isinstance(value, datetime):
        raise SnapshotError("invalid-captured-at", "captured_at must be a datetime")
    current = datetime.now(timezone.utc) if value is None else value
    if current.tzinfo is None or current.utcoffset() is None:
        raise SnapshotError("invalid-captured-at", "captured_at must be timezone-aware")
    return (
        current.astimezone(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    )


def _normalize_error(value: Any, homes: tuple[str, ...]) -> dict[str, str]:
    error = _object(value, "distribution error")
    return {
        "code": _string(error.get("code"), "error.code", allow_empty=False),
        "field": _string(error.get("field"), "error.field", allow_empty=False),
        "type": _string(error.get("type"), "error.type", allow_empty=False),
        "message": redact_text(_string(error.get("message"), "error.message"), homes),
    }


def _normalize_distribution(value: Any, homes: tuple[str, ...]) -> dict[str, Any]:
    raw = _object(value, "distribution")
    name = redact_text(_string(raw.get("name"), "distribution.name"), homes)
    version = redact_text(_string(raw.get("version"), "distribution.version"), homes)
    metadata = _object(raw.get("metadata"), "distribution.metadata")
    requirements = sorted(
        redact_text(_string(item, "requires_dist item", allow_empty=False), homes)
        for item in _array(
            metadata.get("requires_dist"),
            "distribution.metadata.requires_dist",
            maximum=MAX_COLLECTION_ITEMS,
        )
    )
    entry_points: list[dict[str, str]] = []
    for item in _array(
        raw.get("entry_points"),
        "distribution.entry_points",
        maximum=MAX_COLLECTION_ITEMS,
    ):
        entry = _object(item, "entry point")
        entry_points.append(
            {
                "group": redact_text(_string(entry.get("group"), "entry_point.group"), homes),
                "name": redact_text(_string(entry.get("name"), "entry_point.name"), homes),
                "value": redact_text(_string(entry.get("value"), "entry_point.value"), homes),
            }
        )
    entry_points.sort(key=lambda item: (item["group"], item["name"], item["value"]))
    errors = [
        _normalize_error(item, homes)
        for item in _array(raw.get("errors"), "distribution.errors", maximum=MAX_COLLECTION_ITEMS)
    ]
    if not name:
        errors.append(
            {
                "code": "missing-metadata",
                "field": "name",
                "type": "MetadataError",
                "message": "distribution metadata has no Name",
            }
        )
    if not version:
        errors.append(
            {
                "code": "missing-metadata",
                "field": "version",
                "type": "MetadataError",
                "message": "distribution metadata has no Version",
            }
        )
    errors.sort(key=lambda item: (item["code"], item["field"], item["type"], item["message"]))
    return {
        "name": name,
        "normalized_name": normalize_project_name(name),
        "version": version,
        "metadata": {
            "requires_python": redact_text(
                _string(
                    metadata.get("requires_python"),
                    "distribution.metadata.requires_python",
                ),
                homes,
            ),
            "requires_dist": requirements,
        },
        "entry_points": entry_points,
        "location": redact_text(_string(raw.get("location"), "distribution.location"), homes),
        "status": "ok" if not errors else "error",
        "errors": errors,
    }


def collect_snapshot(
    interpreter: str | Path | None = None,
    *,
    timeout_seconds: int = 10,
    captured_at: datetime | None = None,
    redact: bool = True,
) -> dict[str, object]:
    """Collect a deterministic, offline snapshot from one target interpreter."""

    if (
        isinstance(timeout_seconds, bool)
        or not isinstance(timeout_seconds, int)
        or timeout_seconds < 1
    ):
        raise SnapshotError("invalid-timeout", "timeout_seconds must be a positive integer")
    if not isinstance(redact, bool):
        raise SnapshotError("invalid-redact", "redact must be a boolean")
    if captured_at is not None and not isinstance(captured_at, datetime):
        raise SnapshotError("invalid-captured-at", "captured_at must be a datetime")
    try:
        raw, resolved, requested = probe.collect_probe(interpreter, timeout_seconds=timeout_seconds)
    except probe.ProbeError as error:
        raise SnapshotError(error.code, error.message) from error

    identity = _object(raw.get("identity"), "identity")
    target_home = _string(identity.get("user_home"), "identity.user_home")
    host_home = str(Path.home())
    homes = (target_home, host_home) if redact else ()
    raw_environment = _object(raw.get("environment"), "environment")
    if len(raw_environment) > MAX_ENVIRONMENT_VARIABLES:
        raise SnapshotError("probe-field-too-large", "environment exceeds 4096 variables")
    environment = {
        _string(name, "environment name", allow_empty=False): _string(
            value, f"environment[{name!r}]"
        )
        for name, value in raw_environment.items()
    }
    distributions = [
        _normalize_distribution(item, homes)
        for item in _array(
            raw.get("distributions"),
            "distributions",
            maximum=MAX_DISTRIBUTIONS,
        )
    ]
    distributions.sort(
        key=lambda item: (
            item["normalized_name"],
            item["name"],
            item["version"],
            item["location"],
            json.dumps(
                item,
                ensure_ascii=True,
                separators=(",", ":"),
                sort_keys=True,
                allow_nan=False,
            ),
        )
    )
    error_count = sum(len(item["errors"]) for item in distributions)
    sysconfig = _object(raw.get("sysconfig"), "sysconfig")
    paths = _scalar_mapping(sysconfig.get("paths"), "sysconfig.paths")
    variables = _scalar_mapping(sysconfig.get("variables"), "sysconfig.variables")
    public_identity: dict[str, object] = {
        key: _string(identity.get(key), f"identity.{key}")
        for key in (
            "implementation",
            "version",
            "cache_tag",
            "platform",
            "machine",
            "reported_executable",
            "prefix",
            "base_prefix",
            "exec_prefix",
            "compiler",
        )
    }
    version_info = _array(identity.get("version_info"), "identity.version_info", maximum=5)
    if (
        len(version_info) != 5
        or any(isinstance(item, bool) or not isinstance(item, int) for item in version_info[:3])
        or not isinstance(version_info[3], str)
        or isinstance(version_info[4], bool)
        or not isinstance(version_info[4], int)
    ):
        raise SnapshotError(
            "invalid-probe-schema", "identity.version_info must match sys.version_info"
        )
    public_identity["version_info"] = version_info
    return {
        "schema_version": "envlens.snapshot/v1",
        "producer": {"name": "envlens", "version": "0.1.0"},
        "captured_at": _timestamp(captured_at),
        "redaction": {"policy": "envlens-redaction/v1", "enabled": redact},
        "source": {
            "kind": "python-interpreter",
            "requested_executable": redact_text(requested, homes),
            "resolved_executable": redact_text(str(resolved), homes),
            "identity": redact_value(public_identity, homes),
            "sysconfig": {
                "paths": redact_value(paths, homes),
                "variables": redact_value(variables, homes),
            },
        },
        "environment": {
            "variables": (
                redact_environment(environment, homes)
                if redact
                else dict(sorted(environment.items()))
            )
        },
        "distributions": distributions,
        "collection": {
            "status": "complete" if error_count == 0 else "partial",
            "distribution_count": len(distributions),
            "error_count": error_count,
        },
    }


def dumps_snapshot(snapshot: Mapping[str, object], *, pretty: bool = False) -> str:
    """Serialize a snapshot canonically as UTF-8-safe JSON with one newline."""

    separators = None if pretty else (",", ":")
    return (
        json.dumps(
            snapshot,
            # Surrogate-escaped environment bytes are legal on POSIX. Escaping
            # non-ASCII data keeps every returned string UTF-8 encodable.
            ensure_ascii=True,
            indent=2 if pretty else None,
            separators=separators,
            sort_keys=True,
            allow_nan=False,
        )
        + "\n"
    )
