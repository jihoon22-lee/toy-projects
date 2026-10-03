"""Generate portable documentation schemas. Runtime C++ validation is authoritative."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "schemas"
TEXT = {"type": "string"}
BOOL = {"type": "boolean"}
UINT = {"type": "integer", "minimum": 0, "maximum": 18446744073709551615}
DIGEST = {"type": "string", "pattern": "^(|[a-f0-9]{64})$"}
PATH = {"type": "string", "minLength": 1, "maxLength": 4096}
NAME = {"type": "string", "minLength": 1, "maxLength": 128}
FORMAT = {"enum": ["auto", "iso", "syslog", "jsonl", "raw"]}
MULTILINE = {"enum": ["fold-continuations", "separate-lines"]}
MAXRECORD = {"type": "integer", "minimum": 1, "maximum": 1048576}


def obj(properties, required=None):
    return {"type": "object", "properties": properties,
            "required": list(properties) if required is None else required,
            "additionalProperties": False}


def array(item, limit):
    return {"type": "array", "items": item, "maxItems": limit}


RULE = obj({"name": NAME, "pattern": {"type": "string", "maxLength": 1024},
            "whole_line": BOOL, "priority": {"type": "integer"},
            "style": {"type": "string", "minLength": 1, "maxLength": 64}})
ENTRY1 = {"source_path": PATH, "line_number": {"type": "integer", "minimum": 1},
          "bookmarked": BOOL, "annotation": {"type": "string", "maxLength": 4096}}
ENTRY2 = {**ENTRY1, "source_identity": {"type": "string", "maxLength": 128},
          "generation": UINT, "record_fingerprint": DIGEST}
TRIAGE1 = obj({"schema": {"const": "loglens.triage/v1"}, "rules": array(RULE, 128),
              "entries": array(obj(ENTRY1), 8192)})
TRIAGE2 = obj({"schema": {"const": "loglens.triage/v2"}, "rules": array(RULE, 128),
              "entries": array(obj(ENTRY2), 8192)})
SOURCE1 = {"path": PATH, "format": FORMAT, "multiline": MULTILINE,
           "max_record_bytes": MAXRECORD, "format_plugin": PATH}
SOURCE2 = {**SOURCE1, "identity": {"type": "string", "maxLength": 128},
           "modified": {"type": "string", "maxLength": 128}, "fingerprint": DIGEST,
           "fingerprint_bytes": {"type": "integer", "minimum": 0, "maximum": 16777216},
           "size": UINT, "generation": UINT, "plugin_fingerprint": DIGEST}
WINDOW = {"anyOf": [{"type": "null"}, obj({"begin_ms": UINT, "end_ms": UINT})]}
VIEW = obj({"search": {"type": "string", "maxLength": 4096},
            "whole_file_search": {"type": "string", "maxLength": 4096},
            "investigation_tab": {"type": "integer", "minimum": 0, "maximum": 4},
            "settings_open": BOOL, "follow": BOOL, "tail_mode": BOOL,
            "tail_records": {"type": "integer", "minimum": 1, "maximum": 1000000},
            "selected": WINDOW, "baseline": WINDOW, "comparison": WINDOW,
            "layout": {"type": "string", "maxLength": 65536},
            "geometry": {"type": "string", "maxLength": 65536},
            "table_header": {"type": "string", "maxLength": 65536}}, [])
SESSION_BASE = {"name": {"type": "string", "maxLength": 128},
                "filter": {"type": "string", "maxLength": 4096}, "level": TEXT}
SESSION1 = obj({"schema": {"const": "loglens.session/v1"}, **SESSION_BASE,
                "source": obj(SOURCE1, ["path"])}, ["schema", "source"])
SESSION2 = obj({"schema": {"const": "loglens.session/v2"}, **SESSION_BASE,
                "source": obj(SOURCE2, ["path"]), "view": VIEW,
                "triage": {"oneOf": [TRIAGE1, TRIAGE2]}}, ["schema", "source"])
PROFILE = obj({"name": NAME, "format": FORMAT, "multiline": MULTILINE,
               "max_record_bytes": MAXRECORD})
PROFILES = obj({"schema": {"const": "loglens.source-profiles/v1"}, "profiles": array(PROFILE, 128)})
QUERIES = obj({"schema": {"const": "loglens.saved-queries/v1"},
               "queries": array(obj({"name": NAME, "expression": {"type": "string", "minLength": 1, "maxLength": 4096}}), 128)})
PLUGIN = obj({"kind": {"const": "loglens.format/v1"}, "name": NAME,
              "pattern": {"type": "string", "minLength": 1, "maxLength": 8192},
              "fields": obj({key: {"type": "integer", "minimum": 1, "maximum": 32} for key in ["timestamp", "level", "source", "message"]}, ["message"])})

PLUGIN["additionalProperties"] = True
PLUGIN["properties"]["fields"]["additionalProperties"] = True

for filename, document in [("session-v1", SESSION1), ("session-v2", SESSION2),
                           ("triage-v1", TRIAGE1), ("triage-v2", TRIAGE2),
                           ("source-profiles-v1", PROFILES), ("saved-queries-v1", QUERIES),
                           ("format-v1", PLUGIN)]:
    document = {"$schema": "https://json-schema.org/draft/2020-12/schema", **document}
    (ROOT / f"{filename}.schema.json").write_text(json.dumps(document, indent=2) + "\n")
