"""Strict versioned JSON and collision-safe atomic persistence."""

from __future__ import annotations

import importlib.resources
import json
import os
import stat
import tempfile
from pathlib import Path
from typing import Any

from .model import DIFF, SEMANTICS, SNAPSHOT, InputError, Limits

MAX_JSON = 64 * 1024 * 1024


def _object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise InputError("duplicate JSON key")
        result[key] = value
    return result


def _validate(value: Any, schema: dict[str, Any], root: dict[str, Any], depth: int = 0) -> None:
    if depth > 48:
        raise InputError("JSON nesting limit exceeded")
    if "$ref" in schema:
        schema = root["$defs"][schema["$ref"].split("/")[-1]]
    expected = schema.get("type")
    types = expected if isinstance(expected, list) else [expected] if expected else []
    mapping: dict[str, type[Any]] = {
        "object": dict,
        "array": list,
        "string": str,
        "integer": int,
        "boolean": bool,
        "null": type(None),
    }
    if types and not any(type(value) is mapping[t] for t in types):
        raise InputError("JSON type does not match schema")
    if "const" in schema and value != schema["const"]:
        raise InputError("unsupported schema or constant")
    if "enum" in schema and value not in schema["enum"]:
        raise InputError("unknown enumerated value")
    if isinstance(value, dict):
        if len(value) > schema.get("maxProperties", 100000):
            raise InputError("JSON object limit exceeded")
        if any(key not in value for key in schema.get("required", [])):
            raise InputError("required JSON property missing")
        properties = schema.get("properties", {})
        additional = schema.get("additionalProperties", {})
        for key, child in value.items():
            if key in properties:
                _validate(child, properties[key], root, depth + 1)
            elif additional is False:
                raise InputError("unexpected JSON property")
            else:
                _validate(
                    child, additional if isinstance(additional, dict) else {}, root, depth + 1
                )
    elif isinstance(value, list):
        if len(value) > schema.get("maxItems", 100000):
            raise InputError("JSON array limit exceeded")
        for child in value:
            _validate(child, schema.get("items", {}), root, depth + 1)
    elif isinstance(value, str) and len(value) > schema.get("maxLength", 65536):
        raise InputError("JSON string limit exceeded")
    elif type(value) is int and (
        value < schema.get("minimum", -(2**63)) or value > schema.get("maximum", 2**63 - 1)
    ):
        raise InputError("JSON integer outside supported range")
    elif type(value) not in (str, int, bool, type(None), list, dict):
        raise InputError("unsupported JSON value")


def validate(document: dict[str, Any]) -> None:
    name = document.get("schema")
    if name not in (SNAPSHOT, DIFF):
        raise InputError("unsupported document schema")
    filename = (
        "servicelens-snapshot-v1.schema.json"
        if name == SNAPSHOT
        else "servicelens-diff-v1.schema.json"
    )
    resource = importlib.resources.files("servicelens").joinpath("schemas").joinpath(filename)
    if resource.is_file():
        schema = json.loads(resource.read_text(encoding="utf-8"))
    else:
        schema = json.loads((Path(__file__).parents[2] / "schemas" / filename).read_text())
    _validate(document, schema, schema)
    if name == SNAPSHOT:
        from .report import issues

        if document["semantics"] != SEMANTICS:
            raise InputError("unsupported interpretation semantics")
        Limits(**document["context"]["limits"])

        if (
            any(d["severity"] in ("error", "unknown") for d in issues(document))
            and not document["partial"]
        ):
            raise InputError("partial=false contradicts error/unknown evidence")
        for key, unit in document["units"].items():
            if unit["resolution"]["requested"] != key:
                raise InputError("unit identity does not match map key")
            if not document["partial"] and any(
                setting["status"] == "unknown" for setting in unit["settings"].values()
            ):
                raise InputError("partial=false contradicts unknown setting evidence")


def load(path: str | Path) -> dict[str, Any]:
    fd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
    with os.fdopen(fd, "rb") as stream:
        before = os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_size > MAX_JSON:
            raise InputError("snapshot must be a bounded regular file")
        content = stream.read(MAX_JSON + 1)
        after = os.fstat(stream.fileno())
    if len(content) > MAX_JSON or (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (
        after.st_size,
        after.st_mtime_ns,
        after.st_ctime_ns,
    ):
        raise InputError("snapshot too large or changed while reading")
    try:
        data = json.loads(
            content,
            object_pairs_hook=_object,
            parse_constant=lambda _: (_ for _ in ()).throw(InputError("non-finite JSON")),
        )
    except (ValueError, RecursionError, UnicodeError) as exc:
        raise InputError("invalid snapshot JSON") from exc
    if not isinstance(data, dict):
        raise InputError("JSON root must be an object")
    validate(data)
    return data


def save(document: dict[str, Any], output: str | Path, *, inputs: list[Path] | None = None) -> None:
    validate(document)
    target = Path(output).absolute()
    if target.is_symlink():
        raise InputError("output symlinks are refused")
    for source in inputs or []:
        if source.resolve() == target.resolve() or (
            source.exists() and target.exists() and os.path.samefile(source, target)
        ):
            raise InputError("output aliases an input file")
    fd, temporary = tempfile.mkstemp(prefix=".servicelens-", dir=target.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            count = 0
            for chunk in json.JSONEncoder(ensure_ascii=True, sort_keys=True, indent=2).iterencode(
                document
            ):
                count += len(chunk.encode("utf-8"))
                if count >= MAX_JSON:
                    raise InputError("serialized document byte limit exceeded")
                stream.write(chunk)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, target)
        directory = os.open(target.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
