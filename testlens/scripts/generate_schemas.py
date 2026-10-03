"""Regenerate the public JSON schemas; no external dependencies."""

import json
from pathlib import Path

S = {"type": "string"}
B = {"type": "boolean"}
N = {"type": "number", "minimum": 0}
INTEGER = {"type": "integer", "minimum": 0}
NULL = {"type": "null"}
HASH = {"type": "string", "pattern": "^[a-f0-9]{64}$"}
STATE = {"enum": ["passed", "failed", "error", "skipped", "not-run", "unknown"]}
GRAN = {"enum": ["runner-target", "test-case"]}


def nullable(schema):
    return {"anyOf": [schema, NULL]}


def arr(schema):
    return {"type": "array", "items": schema}


def obj(properties):
    return {
        "type": "object",
        "properties": properties,
        "required": list(properties),
        "additionalProperties": False,
    }


def mapping(schema):
    return {"type": "object", "additionalProperties": schema}


OUTPUT = obj({"text": S, "truncated": B})
MESSAGE = obj({"kind": S, "type": S, "message": S, "text": S, "truncated": B})
ATTEMPT = obj(
    {
        "id": HASH,
        "name": S,
        "suite": S,
        "classname": S,
        "status": STATE,
        "duration_seconds": nullable(N),
        "granularity": GRAN,
        "source_id": HASH,
        "locator": S,
        "source_file": nullable(S),
        "source_line": nullable(S),
        "source_file_normalized": nullable(S),
        "output": OUTPUT,
        "messages": arr(MESSAGE),
        "properties": mapping(S),
        "attempt": nullable({"type": "integer", "minimum": 1}),
        "shard": nullable(S),
        "expected_outcome": {"enum": [None, "xfail", "xpass"]},
    }
)
TEST = obj(
    {
        "id": HASH,
        "name": S,
        "suite": S,
        "classname": S,
        "granularity": GRAN,
        "status": STATE,
        "duration_seconds": nullable(N),
        "any_failure": B,
        "ambiguous": B,
        "attempts": {**arr(ATTEMPT), "minItems": 1},
    }
)
DIAGNOSTIC = {
    "type": "object",
    "properties": {"code": S, "message": S, "source_id": HASH, "suite": S},
    "required": ["code", "message"],
    "additionalProperties": False,
}
SOURCE = obj(
    {
        "id": HASH,
        "path": S,
        "sha256": HASH,
        "bytes": INTEGER,
        "dialect": {"enum": ["junit", "pytest", "qt", "ctest", "ctest-junit"]},
    }
)
PRODUCER = obj({"name": {"const": "testlens"}, "version": S})
DATE = {"type": "string", "format": "date-time"}
RUN = obj(
    {
        "schema": {"const": "testlens.run/v1"},
        "producer": PRODUCER,
        "run_id": {"type": "string", "minLength": 1},
        "project": {"type": "string", "minLength": 1},
        "collected_at": DATE,
        "executed_at": nullable(DATE),
        "metadata": mapping(S),
        "scope": {"type": "string", "minLength": 1},
        "complete": {
            **B,
            "description": (
                "Caller-declared coverage without critical collection diagnostics or "
                "unknown/unnamed test results. A named not-run observation does not itself "
                "make coverage incomplete. "
                "The validator additionally checks these cross-field semantics."
            ),
        },
        "declared_complete": {
            **B,
            "description": "The caller's --complete assertion before collection-quality checks.",
        },
        "source_root": nullable(S),
        "expected_shards": arr(S),
        "observed_shards": arr(S),
        "manifest_digest": nullable(HASH),
        "sources": arr(SOURCE),
        "tests": arr(TEST),
        "diagnostics": arr(DIAGNOSTIC),
        "summary": mapping(INTEGER),
    }
)
CHANGE = obj(
    {
        "id": HASH,
        "name": S,
        "kind": {
            "enum": [
                "new-test-failure",
                "added",
                "missing",
                "new-failure",
                "recovered",
                "persistent-failure",
                "status-changed",
                "unchanged",
            ]
        },
        "before": nullable(STATE),
        "after": nullable(STATE),
        "before_duration_seconds": nullable(N),
        "after_duration_seconds": nullable(N),
        "delta_seconds": nullable({"type": "number"}),
        "relative_change": nullable({"type": "number"}),
        "slowdown": B,
        "alias_from": nullable(HASH),
        "before_evidence": arr(ATTEMPT),
        "after_evidence": arr(ATTEMPT),
    }
)
DIFF = obj(
    {
        "schema": {"const": "testlens.diff/v1"},
        "producer": PRODUCER,
        "baseline_run_id": S,
        "current_run_id": S,
        "complete": B,
        "missing_interpretation": {"const": "absent-from-observed-results"},
        "thresholds": obj({"relative": N, "absolute_seconds": N}),
        "changes": arr(CHANGE),
        "summary": mapping(INTEGER),
    }
)
OBS = obj(
    {
        "run_id": S,
        "executed_at": DATE,
        "status": STATE,
        "any_failure": B,
        "duration_seconds": nullable(N),
        "run_complete": B,
    }
)
HISTORY = obj(
    {
        "schema": {"const": "testlens.history/v1"},
        "producer": PRODUCER,
        "run_ids": arr(S),
        "window": {"type": "integer", "minimum": 1},
        "period": obj({"start": DATE, "end": DATE}),
        "complete": B,
        "tests": arr(
            obj(
                {
                    "id": HASH,
                    "name": S,
                    "observations": arr(OBS),
                    "executed_count": INTEGER,
                    "failure_count": INTEGER,
                    "any_failure_count": INTEGER,
                    "failure_rate": nullable({"type": "number", "minimum": 0, "maximum": 1}),
                    "any_failure_rate": nullable({"type": "number", "minimum": 0, "maximum": 1}),
                    "excluded_count": INTEGER,
                    "missing_count": INTEGER,
                }
            )
        ),
        "interpretation": S,
    }
)

if __name__ == "__main__":
    folder = Path(__file__).resolve().parents[1] / "schemas"
    for name, schema in [("run", RUN), ("diff", DIFF), ("history", HISTORY)]:
        schema["$schema"] = "https://json-schema.org/draft/2020-12/schema"
        schema["title"] = f"testlens.{name}/v1"
        (folder / f"{name}-v1.schema.json").write_text(json.dumps(schema, indent=2) + "\n")
