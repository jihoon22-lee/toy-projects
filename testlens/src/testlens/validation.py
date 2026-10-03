"""Public schema and cross-field validation for persisted documents."""

from __future__ import annotations

import json
import re
from collections import Counter
from datetime import datetime
from importlib.resources import files
from pathlib import Path

from jsonschema import Draft202012Validator, FormatChecker
from jsonschema.exceptions import ValidationError

from .core import CRITICAL, aggregate, identity
from .io import Document, InputError, load_json


def validate_timestamp(value: str) -> None:
    if not re.fullmatch(
        r"\d{4}-\d{2}-\d{2}[Tt]\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:[Zz]|[+-]\d{2}:\d{2})", value
    ):
        raise InputError(f"Invalid RFC3339 timestamp: {value}")
    try:
        parsed = datetime.fromisoformat(value.upper().replace("Z", "+00:00"))
        if parsed.tzinfo is None:
            raise ValueError("Timezone required")
    except ValueError as exc:
        raise InputError(f"Invalid RFC3339 timestamp: {value}") from exc


def validate(document: Document, expected: str | None = None) -> None:
    schema_name = document.get("schema")
    names = {f"testlens.{kind}/v1": kind for kind in ("run", "diff", "history")}
    if (
        not isinstance(schema_name, str)
        or schema_name not in names
        or expected
        and schema_name != f"testlens.{expected}/v1"
    ):
        raise InputError(f"Unsupported schema: {schema_name}")
    name = f"{names[schema_name]}-v1.schema.json"
    installed = files("testlens").joinpath("schemas").joinpath(name)
    schema = (
        json.loads(installed.read_text(encoding="utf-8"))
        if installed.is_file()
        else load_json(Path(__file__).resolve().parents[2] / "schemas" / name)
    )
    try:
        Draft202012Validator(schema, format_checker=FormatChecker()).validate(document)
    except ValidationError as exc:
        raise InputError(
            f"Schema violation at {'/'.join(map(str, exc.path))}: {exc.message}"
        ) from exc
    if schema_name == "testlens.run/v1":
        validate_timestamp(document["collected_at"])
        if document["executed_at"] is not None:
            validate_timestamp(document["executed_at"])
        tests = document["tests"]
        if len({t["id"] for t in tests}) != len(tests):
            raise InputError("Duplicate normalized test IDs")
        source_ids = {s["id"] for s in document["sources"]}
        if len(source_ids) != len(document["sources"]):
            raise InputError("Duplicate source IDs")
        if any(s["id"] != s["sha256"] for s in document["sources"]):
            raise InputError("Source digest and ID disagree")
        records = [record for test in tests for record in test["attempts"]]
        for record in records:
            if record["source_id"] not in source_ids or record["id"] != identity(
                record, document["project"]
            ):
                raise InputError("Test evidence identity or source is inconsistent")
        computed, aggregate_issues = aggregate(records)
        if sorted(tests, key=lambda t: t["id"]) != computed:
            raise InputError("Test summaries do not agree with underlying observations")
        if document["summary"] != dict(Counter(t["status"] for t in tests)):
            raise InputError("Run summary does not agree with tests")
        if document["complete"] and (
            not document["declared_complete"]
            or any(t["status"] == "unknown" or not t["name"] for t in tests)
            or bool(aggregate_issues)
            or (
                bool(document["expected_shards"])
                and set(document["expected_shards"]) != set(document["observed_shards"])
            )
            or any(d["code"] in CRITICAL for d in document["diagnostics"])
        ):
            raise InputError("Invalid completion claim")

    elif schema_name == "testlens.history/v1":
        validate_timestamp(document["period"]["start"])
        validate_timestamp(document["period"]["end"])
        for test in document["tests"]:
            for observation in test["observations"]:
                validate_timestamp(observation["executed_at"])


def load_run(path: Path) -> Document:
    document = load_json(path)
    validate(document, "run")
    return document
