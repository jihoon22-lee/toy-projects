"""Normalize raw interpreter probes into the public snapshot contract."""

from __future__ import annotations

import json
import math
import re
from collections.abc import Mapping
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from envlens.probe import ProbeError, collect_probe
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


def _redact_if_enabled(value: str, homes: tuple[str, ...], enabled: bool) -> str:
    return redact_text(value, homes) if enabled else value


def _normalize_error(value: Any, homes: tuple[str, ...], *, redact: bool) -> dict[str, str]:
    error = _object(value, "distribution error")
    return {
        "code": _string(error.get("code"), "error.code", allow_empty=False),
        "field": _string(error.get("field"), "error.field", allow_empty=False),
        "type": _string(error.get("type"), "error.type", allow_empty=False),
        "message": _redact_if_enabled(
            _string(error.get("message"), "error.message"), homes, redact
        ),
    }


def _normalize_distribution(value: Any, homes: tuple[str, ...], *, redact: bool) -> dict[str, Any]:
    raw = _object(value, "distribution")
    name = _redact_if_enabled(_string(raw.get("name"), "distribution.name"), homes, redact)
    version = _redact_if_enabled(_string(raw.get("version"), "distribution.version"), homes, redact)
    metadata = _object(raw.get("metadata"), "distribution.metadata")
    requirements = sorted(
        _redact_if_enabled(_string(item, "requires_dist item", allow_empty=False), homes, redact)
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
                "group": _redact_if_enabled(
                    _string(entry.get("group"), "entry_point.group"), homes, redact
                ),
                "name": _redact_if_enabled(
                    _string(entry.get("name"), "entry_point.name"), homes, redact
                ),
                "value": _redact_if_enabled(
                    _string(entry.get("value"), "entry_point.value"), homes, redact
                ),
            }
        )
    entry_points.sort(key=lambda item: (item["group"], item["name"], item["value"]))
    errors = [
        _normalize_error(item, homes, redact=redact)
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
    normalized: dict[str, Any] = {
        "name": name,
        "normalized_name": normalize_project_name(name),
        "version": version,
        "metadata": {
            "requires_python": _redact_if_enabled(
                _string(
                    metadata.get("requires_python"),
                    "distribution.metadata.requires_python",
                ),
                homes,
                redact,
            ),
            "requires_dist": requirements,
        },
        "entry_points": entry_points,
        "location": _redact_if_enabled(
            _string(raw.get("location"), "distribution.location"), homes, redact
        ),
        "status": "ok" if not errors else "error",
        "errors": errors,
    }
    # These fields were added after the v1 snapshot skeleton.  They remain
    # optional so snapshots made by older probes continue to normalize and
    # compare.  Import names are evidence, not a claim that every namespace
    # package can be recovered from installed metadata.
    if "import_names" in raw:
        import_names = sorted(
            {
                _redact_if_enabled(
                    _string(item, "distribution.import_names item", allow_empty=False),
                    homes,
                    redact,
                )
                for item in _array(
                    raw.get("import_names"),
                    "distribution.import_names",
                    maximum=MAX_COLLECTION_ITEMS,
                )
            }
        )
        normalized["import_names"] = import_names
    wheel_tag_value = (
        metadata.get("wheel_tags") if "wheel_tags" in metadata else raw.get("wheel_tags")
    )
    if "wheel_tags" in metadata or "wheel_tags" in raw:
        wheel_tags = sorted(
            {
                _redact_if_enabled(
                    _string(item, "distribution.metadata.wheel_tags item", allow_empty=False),
                    homes,
                    redact,
                )
                for item in _array(
                    wheel_tag_value,
                    "distribution.metadata.wheel_tags",
                    maximum=MAX_COLLECTION_ITEMS,
                )
            }
        )
        normalized["metadata"]["wheel_tags"] = wheel_tags
    return normalized


def _validate_collection_options(
    timeout_seconds: int, captured_at: datetime | None, redact: bool
) -> None:
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


def _normalize_identity(identity: dict[str, Any]) -> dict[str, object]:
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
    return public_identity


def _normalize_distributions(
    value: Any, homes: tuple[str, ...], *, redact: bool
) -> list[dict[str, Any]]:
    distributions = [
        _normalize_distribution(item, homes, redact=redact)
        for item in _array(value, "distributions", maximum=MAX_DISTRIBUTIONS)
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
    return distributions


def collect_snapshot(
    interpreter: str | Path | None = None,
    *,
    timeout_seconds: int = 10,
    captured_at: datetime | None = None,
    redact: bool = True,
) -> dict[str, object]:
    """Collect a deterministic, offline snapshot from one target interpreter."""

    _validate_collection_options(timeout_seconds, captured_at, redact)
    try:
        raw, resolved, requested = collect_probe(interpreter, timeout_seconds=timeout_seconds)
    except ProbeError as error:
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
    distributions = _normalize_distributions(raw.get("distributions"), homes, redact=redact)
    error_count = sum(len(item["errors"]) for item in distributions)
    sysconfig = _object(raw.get("sysconfig"), "sysconfig")
    paths = _scalar_mapping(sysconfig.get("paths"), "sysconfig.paths")
    variables = _scalar_mapping(sysconfig.get("variables"), "sysconfig.variables")
    public_identity = _normalize_identity(identity)
    return {
        "schema_version": "envlens.snapshot/v1",
        "producer": {"name": "envlens", "version": "0.1.0"},
        "captured_at": _timestamp(captured_at),
        "redaction": {"policy": "envlens-redaction/v1", "enabled": redact},
        "source": {
            "kind": "python-interpreter",
            "requested_executable": _redact_if_enabled(requested, homes, redact),
            "resolved_executable": _redact_if_enabled(str(resolved), homes, redact),
            "identity": redact_value(public_identity, homes) if redact else public_identity,
            "sysconfig": {
                "paths": redact_value(paths, homes) if redact else paths,
                "variables": redact_value(variables, homes) if redact else variables,
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
