"""Offline reports with escaped evidence and explicit input quality."""

from __future__ import annotations

import html
import json
import re
from importlib.resources import files
from pathlib import Path

from .io import Document, clean_text, digest, read_bytes


def evidence_status(run: Document) -> list[Document]:
    result = []
    for source in run["sources"]:
        path = Path(source["path"])
        if not path.is_absolute() and run["source_root"]:
            path = Path(run["source_root"]) / path
        try:
            status = (
                "verified"
                if digest(read_bytes(path, 64 * 1024 * 1024)) == source["sha256"]
                else "changed"
            )
        except (OSError, ValueError):
            status = "unavailable"
        result.append({**source, "verification": status})
    return result


def text_report(document: Document, verbose: bool = False) -> str:
    lines = [document["schema"]]
    if "complete" in document:
        lines.append(
            "Input coverage: "
            + ("declared complete" if document["complete"] else "partial or unconfirmed")
        )
    kind = document["schema"].split(".")[1].split("/")[0]
    if kind == "run":
        lines.extend(
            [
                f"Run: {document['run_id']} | Project: {document['project']}",
                f"Results: {document['summary']}",
            ]
        )
        for test in document["tests"]:
            if verbose or test["status"] != "passed":
                lines.append(
                    f"{test['status']:10} {test['suite']} :: {test['name']} [{test['id'][:12]}]"
                )
                for attempt in test["attempts"]:
                    for message in attempt["messages"]:
                        lines.append(f"  {message['message']} {message['text']}")
                    lines.append(f"  evidence: {attempt['source_id'][:12]} {attempt['locator']}")
        for diagnostic in document["diagnostics"]:
            lines.append(f"[{diagnostic['code']}] {diagnostic['message']}")
    elif kind == "diff":
        lines.append(f"{document['baseline_run_id']} → {document['current_run_id']}")
        order = {
            name: idx
            for idx, name in enumerate(
                [
                    "new-failure",
                    "new-test-failure",
                    "persistent-failure",
                    "recovered",
                    "missing",
                    "status-changed",
                    "added",
                    "unchanged",
                ]
            )
        }
        for change in sorted(document["changes"], key=lambda c: (order[c["kind"]], c["name"])):
            if verbose or change["kind"] != "unchanged" or change["slowdown"]:
                suffix = (
                    f" duration Δ={change['delta_seconds']:+.3f}s"
                    if change["delta_seconds"] is not None
                    else ""
                )
                if change["slowdown"]:
                    suffix += " [slowdown]"
                lines.append(
                    f"{change['kind']:20} {change['name']}: "
                    f"{change['before']} → {change['after']}{suffix}"
                )
        lines.append("Missing means absent from observed results; deletion is not inferred.")
    else:
        lines.append(
            f"{len(document['run_ids'])} runs | "
            f"{document['period']['start']} → {document['period']['end']}"
        )
        lines.append(document["interpretation"])
        for test in sorted(document["tests"], key=lambda t: (-(t["failure_rate"] or 0), t["name"])):
            lines.append(
                f"{test['name']}: failures {test['failure_count']}/{test['executed_count']}; "
                f"any-attempt failures {test['any_failure_count']}; "
                f"excluded {test['excluded_count']}; absent {test['missing_count']}"
            )
    return clean_text("\n".join(lines)) + "\n"


def html_report(
    run: Document,
    diff: Document | None = None,
    history: Document | None = None,
    baseline: Document | None = None,
) -> str:
    payload = {
        "run": run,
        "diff": diff,
        "history": history,
        "sources": evidence_status(run),
        "baseline_sources": evidence_status(baseline) if baseline else [],
    }
    encoded = (
        json.dumps(payload, ensure_ascii=False, allow_nan=False)
        .replace("<", "\\u003c")
        .replace(">", "\\u003e")
        .replace("&", "\\u0026")
    )
    template = files("testlens").joinpath("report.html").read_text(encoding="utf-8")
    replacements = {"__TITLE__": html.escape(run["project"] + " — TestLens"), "__DATA__": encoded}
    return re.sub(r"__TITLE__|__DATA__", lambda match: replacements[match.group()], template)
