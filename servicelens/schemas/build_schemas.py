"""Regenerate checked-in JSON schemas using only the standard library."""

import json
from pathlib import Path


def obj(properties, optional=()):
    return {
        "type": "object",
        "properties": properties,
        "required": [k for k in properties if k not in optional],
        "additionalProperties": False,
    }


def array(items, maximum=100000):
    return {"type": "array", "items": items, "maxItems": maximum}


def ref(name):
    return {"$ref": "#/$defs/" + name}


string = {"type": "string", "maxLength": 65536}
nullable = {"type": ["string", "null"], "maxLength": 65536}
integer = {"type": "integer", "minimum": 0}
boolean = {"type": "boolean"}
status = {"enum": ["known", "unknown"], "type": "string"}
origin = obj({"path": string, "line": {"type": ["integer", "null"], "minimum": 1}})
issue = obj(
    {
        "code": string,
        "severity": {"type": "string", "enum": ["error", "warning", "unknown"]},
        "message": string,
        "path": nullable,
        "line": {"type": ["integer", "null"], "minimum": 1},
    }
)
environment = obj({"value": string, "status": status, "origin": origin, "via": string})
edge = obj(
    {"source": string, "target": string, "relation": string, "origin": origin, "status": status}
)
setting = obj(
    {
        "kind": {"type": "string", "enum": ["unknown", "scalar", "dependency", "commands", "list"]},
        "value": {"type": ["string", "array"], "items": string, "maxItems": 100000},
        "status": status,
        "origins": array(origin),
        "redacted": boolean,
    },
    ("redacted",),
)
ledger = obj(
    {
        "section": string,
        "key": string,
        "value": string,
        "path": string,
        "line": {"type": "integer", "minimum": 1},
        "end_line": {"type": "integer", "minimum": 1},
        "id": integer,
        "action": {
            "type": "string",
            "enum": [
                "unsupported",
                "ignored-extension",
                "invalid",
                "replace",
                "ignored-empty-dependency",
                "union",
                "append",
                "reset",
            ],
        },
        "status": status,
    }
)
resolution = obj(
    {
        "requested": string,
        "canonical": string,
        "selected": nullable,
        "masked": boolean,
        "candidates": array(obj({"path": string, "resolved": string, "selected": boolean})),
        "dropins": array(
            obj({"path": string, "name": string, "selected": boolean, "masked": boolean})
        ),
        "names": array(string),
    }
)
command = obj(
    {
        "executable": string,
        "prefixes": string,
        "arguments": array(string),
        "status": status,
        "existence": {"type": "string", "enum": ["unknown", "present", "missing-at-capture"]},
    }
)
unit = obj(
    {
        "resolution": resolution,
        "settings": {"type": "object", "additionalProperties": setting},
        "ledger": array(ledger),
        "environment": {"type": "object", "additionalProperties": environment},
        "environment_history": array(obj({"name": string, **environment["properties"]})),
        "commands": {"type": "object", "additionalProperties": array(command)},
        "dependencies": array(edge, 8192),
        "diagnostics": array(issue),
        "dependency_scope": string,
    }
)
limits = obj(
    {
        name: {"type": "integer", "minimum": 1}
        for name in (
            "files",
            "file_bytes",
            "total_bytes",
            "line_bytes",
            "directives",
            "nodes",
            "edges",
            "depth",
            "symlink_hops",
            "directory_entries",
        )
    }
)
snapshot = obj(
    {
        "schema": {"const": "servicelens.snapshot/v1"},
        "version": string,
        "semantics": {"const": "systemd-255-subset-v1"},
        "requested": string,
        "context": obj(
            {
                "scope": {"const": "system"},
                "mode": {"const": "static-disk"},
                "load_paths": array(string),
                "limits": limits,
            }
        ),
        "redacted": boolean,
        "partial": boolean,
        "units": {"type": "object", "additionalProperties": unit, "maxProperties": 100000},
        "files": array(obj({"path": string, "bytes": integer})),
        "diagnostics": array(issue),
    }
)
diff = obj(
    {
        "schema": {"const": "servicelens.diff/v1"},
        "changed": boolean,
        "unknown": boolean,
        "changes": array(
            obj(
                {
                    "unit": string,
                    "key": nullable,
                    "kind": {
                        "type": "string",
                        "enum": [
                            "added",
                            "removed",
                            "value-changed",
                            "origin-changed",
                            "evidence-changed",
                            "structure-changed",
                            "diagnostics-changed",
                        ],
                    },
                    "before": {},
                    "after": {},
                }
            )
        ),
        "uncertain": array(obj({"unit": nullable, "key": string, "reason": string})),
    }
)
for name, schema in (("snapshot", snapshot), ("diff", diff)):
    schema["$schema"] = "https://json-schema.org/draft/2020-12/schema"
    schema["title"] = "ServiceLens " + name + " v1"
    Path(__file__).with_name(f"servicelens-{name}-v1.schema.json").write_text(
        json.dumps(schema, indent=2) + "\n", encoding="utf-8"
    )
