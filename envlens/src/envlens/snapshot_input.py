"""Bounded loading and validation for ``envlens.snapshot/v1`` inputs."""

from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any, TextIO, cast

from envlens.snapshot import MAX_COLLECTION_ITEMS, MAX_DISTRIBUTIONS, MAX_STRING_LENGTH

MAX_INPUT_BYTES = 16 * 1024 * 1024
MAX_REQUIREMENTS = 100_000


class DiffError(ValueError):
    """A bounded, user-facing snapshot diff failure."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message


def _object(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise DiffError("invalid-snapshot", f"{label} must be an object")
    return value


def _string(value: Any, label: str, *, allow_empty: bool = True) -> str:
    if not isinstance(value, str) or (not allow_empty and not value):
        raise DiffError("invalid-snapshot", f"{label} must be a string")
    if len(value) > MAX_STRING_LENGTH:
        raise DiffError("snapshot-field-too-large", f"{label} exceeds 65536 characters")
    return value


def _array(value: Any, label: str, maximum: int = MAX_COLLECTION_ITEMS) -> list[Any]:
    if not isinstance(value, list):
        raise DiffError("invalid-snapshot", f"{label} must be an array")
    if len(value) > maximum:
        raise DiffError("snapshot-field-too-large", f"{label} exceeds {maximum} items")
    return value


def _reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise DiffError("invalid-snapshot-json", f"duplicate key {key!r}")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise DiffError("invalid-snapshot-json", f"non-finite number {value}")


def load_snapshot(source: str | os.PathLike[str] | TextIO) -> dict[str, Any]:
    """Load one bounded snapshot from a path or text stream.

    A stream is read at most ``MAX_INPUT_BYTES + 1`` bytes.  ``-`` is not
    treated specially here; the CLI maps it to stdin explicitly so library
    callers cannot accidentally consume an unrelated process stream.
    """

    if hasattr(source, "read"):
        stream = cast(TextIO, source)
        try:
            text = stream.read(MAX_INPUT_BYTES + 1)
        except (OSError, ValueError) as error:
            raise DiffError("snapshot-read-failed", str(error)) from error
        if not isinstance(text, str):
            raise DiffError("snapshot-read-failed", "snapshot stream must return text")
        if len(text.encode("utf-8", errors="surrogatepass")) > MAX_INPUT_BYTES:
            raise DiffError("snapshot-too-large", "snapshot exceeds 16 MiB")
    else:
        path = Path(source)
        try:
            stat = path.stat()
        except OSError as error:
            raise DiffError("snapshot-read-failed", str(error)) from error
        if not path.is_file():
            raise DiffError("snapshot-read-failed", "snapshot must be a regular file")
        if stat.st_size > MAX_INPUT_BYTES:
            raise DiffError("snapshot-too-large", "snapshot exceeds 16 MiB")
        try:
            data = path.read_bytes()
        except OSError as error:
            raise DiffError("snapshot-read-failed", str(error)) from error
        if len(data) > MAX_INPUT_BYTES:
            raise DiffError("snapshot-too-large", "snapshot exceeds 16 MiB")
        try:
            text = data.decode("utf-8")
        except UnicodeError as error:
            raise DiffError("invalid-snapshot-json", "snapshot is not UTF-8") from error
    try:
        value = json.loads(
            text,
            object_pairs_hook=_reject_duplicates,
            parse_constant=_reject_constant,
        )
    except DiffError:
        raise
    except (json.JSONDecodeError, RecursionError) as error:
        raise DiffError("invalid-snapshot-json", str(error)) from error
    return validate_snapshot(value)


def validate_snapshot(snapshot: Any) -> dict[str, Any]:
    """Validate the bounded fields needed by the diff consumer.

    Unknown additive fields are retained.  This lets a newer producer add
    metadata while keeping the E2 consumer compatible with older v1 files.
    """

    value = _object(snapshot, "snapshot")
    if value.get("schema_version") != "envlens.snapshot/v1":
        raise DiffError("unsupported-snapshot", "expected envlens.snapshot/v1")
    distributions = _array(value.get("distributions"), "snapshot.distributions", MAX_DISTRIBUTIONS)
    for index, raw in enumerate(distributions):
        distribution = _object(raw, f"snapshot.distributions[{index}]")
        _string(distribution.get("name"), f"distribution[{index}].name")
        normalized = distribution.get("normalized_name")
        if normalized is not None:
            _string(normalized, f"distribution[{index}].normalized_name")
        _string(distribution.get("version"), f"distribution[{index}].version")
        metadata = _object(distribution.get("metadata"), f"distribution[{index}].metadata")
        _string(metadata.get("requires_python"), f"distribution[{index}].requires_python")
        _array(
            metadata.get("requires_dist"),
            f"distribution[{index}].requires_dist",
            MAX_REQUIREMENTS,
        )
        _array(
            distribution.get("entry_points"),
            f"distribution[{index}].entry_points",
            MAX_COLLECTION_ITEMS,
        )
        _array(
            distribution.get("errors"),
            f"distribution[{index}].errors",
            MAX_COLLECTION_ITEMS,
        )
        if "import_names" in distribution:
            _array(
                distribution.get("import_names"),
                f"distribution[{index}].import_names",
                MAX_COLLECTION_ITEMS,
            )
        if "wheel_tags" in metadata:
            _array(
                metadata.get("wheel_tags"),
                f"distribution[{index}].wheel_tags",
                MAX_COLLECTION_ITEMS,
            )
    source = _object(value.get("source"), "snapshot.source")
    identity = _object(source.get("identity"), "snapshot.source.identity")
    _string(identity.get("version"), "snapshot.source.identity.version")
    return value
