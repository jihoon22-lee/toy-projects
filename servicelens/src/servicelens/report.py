"""Offline comparison and reports: analysis is independent from CI failure policy."""

from __future__ import annotations

import json
from typing import Any

from .analysis import REDACTED
from .model import DIFF


def issues(snapshot: dict[str, Any]) -> list[dict[str, Any]]:
    return list(snapshot["diagnostics"]) + [
        d for u in snapshot["units"].values() for d in u["diagnostics"]
    ]


def check(snapshot: dict[str, Any]) -> dict[str, Any]:
    diagnostics = issues(snapshot)
    return {
        "errors": sum(d["severity"] == "error" for d in diagnostics),
        "unknown": sum(d["severity"] == "unknown" for d in diagnostics),
        "warnings": sum(d["severity"] == "warning" for d in diagnostics),
        "partial": snapshot["partial"],
        "diagnostics": diagnostics,
    }


def _has_redacted(value: Any) -> bool:
    if isinstance(value, dict):
        return any(_has_redacted(v) for v in value.values())
    if isinstance(value, list):
        return any(_has_redacted(v) for v in value)
    return bool(value == REDACTED)


def diff(before: dict[str, Any], after: dict[str, Any]) -> dict[str, Any]:
    """Compare semantic configuration and provenance, retaining list order."""
    changes: list[dict[str, Any]] = []
    uncertain: list[dict[str, Any]] = []
    if before["semantics"] != after["semantics"] or before["context"] != after["context"]:
        uncertain.append({"unit": None, "key": "context", "reason": "capture-context-differs"})
    for name in sorted(before["units"].keys() | after["units"].keys()):
        old, new = before["units"].get(name), after["units"].get(name)
        if old is None or new is None:
            changes.append(
                {
                    "unit": name,
                    "key": None,
                    "kind": "added" if old is None else "removed",
                    "before": old,
                    "after": new,
                }
            )
            continue
        for key in sorted(old["settings"].keys() | new["settings"].keys()):
            a, b = old["settings"].get(key), new["settings"].get(key)
            if a is None or b is None:
                changes.append(
                    {
                        "unit": name,
                        "key": key,
                        "kind": "added" if a is None else "removed",
                        "before": a,
                        "after": b,
                    }
                )
                continue
            if _has_redacted(a["value"]) or _has_redacted(b["value"]):
                uncertain.append(
                    {"unit": name, "key": key, "reason": "redacted-values-not-comparable"}
                )
            elif a["value"] != b["value"]:
                changes.append(
                    {"unit": name, "key": key, "kind": "value-changed", "before": a, "after": b}
                )
            if a["origins"] != b["origins"]:
                changes.append(
                    {
                        "unit": name,
                        "key": key,
                        "kind": "origin-changed",
                        "before": a["origins"],
                        "after": b["origins"],
                    }
                )
            if a["status"] != b["status"]:
                changes.append(
                    {
                        "unit": name,
                        "key": key,
                        "kind": "evidence-changed",
                        "before": a["status"],
                        "after": b["status"],
                    }
                )
        for key in ("resolution", "ledger", "dependencies", "environment", "commands"):
            a, b = old[key], new[key]
            if a != b:
                # Redacted inputs contain no raw secret; do not reveal raw values from a
                # non-redacted peer when mixed snapshots are compared.
                changes.append(
                    {
                        "unit": name,
                        "key": key,
                        "kind": "structure-changed",
                        "before": None,
                        "after": None,
                    }
                )
        if old["diagnostics"] != new["diagnostics"]:
            changes.append(
                {
                    "unit": name,
                    "key": "diagnostics",
                    "kind": "diagnostics-changed",
                    "before": old["diagnostics"],
                    "after": new["diagnostics"],
                }
            )
    if before["partial"] or after["partial"]:
        uncertain.append({"unit": None, "key": "coverage", "reason": "partial-capture"})
    # Never export an unmasked peer's values through a default/redacted comparison.
    if before["redacted"] or after["redacted"]:
        for change in changes:
            if change["kind"] not in ("origin-changed", "evidence-changed"):
                change["before"] = None
                change["after"] = None
    return {
        "schema": DIFF,
        "changed": bool(changes),
        "unknown": bool(uncertain),
        "changes": changes,
        "uncertain": uncertain,
    }


def graph(snapshot: dict[str, Any]) -> str:
    edges = [edge for unit in snapshot["units"].values() for edge in unit["dependencies"]]
    out = [
        "digraph services {",
        '  graph [label="Static dependency evidence; runtime state unknown"];',
    ]
    for name in sorted(snapshot["units"]):
        out.append(f"  {json.dumps(name)};")
    for edge in edges:
        style = "dashed" if edge["relation"] in ("Before", "After") else "solid"
        out.append(
            f"  {json.dumps(edge['source'])} -> {json.dumps(edge['target'])} "
            f"[label={json.dumps(edge['relation'])}, style={style}];"
        )
    out.append("}")
    return "\n".join(out) + "\n"


def text(snapshot: dict[str, Any], *, key: str | None = None) -> str:
    summary = check(snapshot)
    out = [
        f"ServiceLens · {snapshot['requested']}",
        "Scope: static disk configuration; daemon state not collected",
        f"Coverage: {'partial' if snapshot['partial'] else 'supported subset'} · "
        f"{summary['errors']} errors · {summary['unknown']} unknown · "
        f"{summary['warnings']} warnings",
        f"Privacy: {'redacted' if snapshot['redacted'] else 'unredacted (explicit opt-in)'}",
    ]
    unit = snapshot["units"].get(snapshot["requested"])
    if unit:
        resolution = unit["resolution"]
        out.extend(
            [
                f"Source: {resolution['selected'] or 'not found'}",
                f"Canonical: {resolution['canonical']} · masked={resolution['masked']}",
            ]
        )
        if key:
            setting = unit["settings"].get(key)
            out.append("")
            out.append(
                f"{key}: {json.dumps(setting, ensure_ascii=False) if setting else 'not declared'}"
            )
            for entry in unit["ledger"]:
                if entry["section"] + "." + entry["key"] == key:
                    out.append(
                        f"  {entry['path']}:{entry['line']} [{entry['action']}] {entry['value']}"
                    )
        else:
            for command_type, commands in sorted(unit["commands"].items()):
                for command in commands:
                    out.append(
                        f"{command_type}: {command['prefixes']}{command['executable']} "
                        f"{' '.join(command['arguments'])} [{command['status']}]"
                    )
            out.append(
                f"Environment: {', '.join(sorted(unit['environment'])) or '(no static values)'}"
            )
            out.append(
                f"Dependencies: {len(unit['dependencies'])} edges; {unit['dependency_scope']}"
            )
            for dropin in resolution["dropins"]:
                state = (
                    "mask" if dropin["masked"] else "applied" if dropin["selected"] else "shadowed"
                )
                out.append(f"  Drop-in [{state}]: {dropin['path']}")
    for issue in issues(snapshot):
        location = f" {issue['path']}:{issue['line']}" if issue["path"] else ""
        out.append(f"[{issue['severity']}] {issue['code']}{location}: {issue['message']}")
    # Input filenames and explicit unredacted values must not inject terminal
    # controls. Keep our layout, escape controls within each constructed line.
    return (
        "\n".join(
            "".join(c if c.isprintable() else f"\\x{ord(c):02x}" for c in line) for line in out
        )
        + "\n"
    )
