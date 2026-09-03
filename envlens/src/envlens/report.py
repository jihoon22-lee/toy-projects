"""Deterministic text, JSON, and Markdown renderers for envlens results."""

from __future__ import annotations

import json
from collections.abc import Mapping
from typing import Any

MAX_REPORT_BYTES = 16 * 1024 * 1024


class ReportError(ValueError):
    """A stable report rendering failure."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message


def _value(item: Mapping[str, Any], key: str, default: str = "") -> str:
    value = item.get(key, default)
    return str(value) if value is not None else default


def _mapping(value: Any) -> Mapping[str, Any]:
    return value if isinstance(value, Mapping) else {}


def _diff_text(report: Mapping[str, Any]) -> str:
    summary = _mapping(report.get("summary"))
    lines = [f"envlens diff: {_value(report, 'status', 'unknown').upper()}", ""]
    lines.append(
        "Summary: "
        + ", ".join(
            f"{key}={summary.get(key, 0)}"
            for key in (
                "added",
                "removed",
                "upgraded",
                "downgraded",
                "changed",
                "compatibility_issues",
                "dependency_issues",
            )
        )
    )
    for category, marker in (
        ("added", "+"),
        ("removed", "-"),
        ("upgraded", "↑"),
        ("downgraded", "↓"),
        ("changed", "~"),
    ):
        entries = report.get(category)
        if not isinstance(entries, list) or not entries:
            continue
        lines.extend(["", f"{category.title()} ({len(entries)}):"])
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            name = _value(entry, "name", _value(entry, "normalized_name", "<unknown>"))
            if category in {"upgraded", "downgraded", "changed"}:
                version = f"{_value(entry, 'from_version')} -> {_value(entry, 'to_version')}"
            else:
                version = _value(entry, "version", "unknown")
            imports = entry.get("import_names")
            import_suffix = f" imports={','.join(str(item) for item in imports)}" if imports else ""
            lines.append(f"  {marker} {name} {version}{import_suffix}")
    compatibility = report.get("compatibility")
    if isinstance(compatibility, list) and compatibility:
        lines.extend(["", "Compatibility:"])
        for item in compatibility:
            if not isinstance(item, dict):
                continue
            lines.append(
                f"  [{_value(item, 'status', 'unknown')}; "
                f"certainty={_value(item, 'certainty', 'unknown')}] "
                f"{_value(item, 'name', '<unknown>')}: {_value(item, 'reason')}"
            )
    dependencies = report.get("dependencies")
    if isinstance(dependencies, list) and dependencies:
        lines.extend(["", "Dependency issues:"])
        for item in dependencies:
            if not isinstance(item, dict):
                continue
            lines.append(
                f"  [{_value(item, 'kind', 'unknown')}; "
                f"certainty={_value(item, 'certainty', 'unknown')}] "
                f"{_value(item, 'name', '<unknown>')} {_value(item, 'requirement')}: "
                f"{_value(item, 'reason')}"
            )
    changes = report.get("import_name_changes")
    if isinstance(changes, list) and changes:
        lines.extend(["", "Project/import-name changes:"])
        for item in changes:
            if isinstance(item, dict):
                lines.append(
                    f"  {_value(item, 'normalized_project_name')}: "
                    f"{', '.join(str(value) for value in item.get('before', [])) or '<none>'} -> "
                    f"{', '.join(str(value) for value in item.get('after', [])) or '<none>'}"
                )
    return "\n".join(lines) + "\n"


def _runtime_text(report: Mapping[str, Any]) -> str:
    summary = _mapping(report.get("summary"))
    lines = [f"envlens runtime: {_value(summary, 'status', 'unknown').upper()}", ""]
    lines.append(
        f"Summary: interpreters={summary.get('interpreter_count', 0)}, "
        f"checks={summary.get('check_count', 0)}, failures={summary.get('failure_count', 0)}, "
        f"unknown={summary.get('unknown_count', 0)}"
    )
    interpreters = report.get("interpreters", report.get("results", []))
    if isinstance(interpreters, list):
        for interpreter in interpreters:
            if not isinstance(interpreter, dict):
                continue
            lines.extend(
                [
                    "",
                    f"Interpreter {_value(interpreter, 'requested_executable', '<unknown>')} "
                    f"({_value(interpreter, 'status', 'unknown')}):",
                ]
            )
            checks = interpreter.get("checks", [])
            if isinstance(checks, list):
                for check in checks:
                    if not isinstance(check, dict):
                        continue
                    check_name = _value(check, "name", _value(check, "group", "entry-point"))
                    lines.append(
                        f"  [{_value(check, 'status', 'unknown')}] "
                        f"{_value(check, 'kind', 'check')} {check_name}: "
                        f"{_value(check, 'reason')}"
                    )
    project = report.get("project")
    if isinstance(project, dict):
        entries = project.get("entry_points", [])
        if isinstance(entries, list) and entries:
            lines.extend(["", "Entry points (dry inspection):"])
            for entry in entries:
                if isinstance(entry, dict):
                    location = entry.get("location", {})
                    location_text = (
                        f"{location.get('path')}:{location.get('line')}"
                        if isinstance(location, dict)
                        else "unknown location"
                    )
                    lines.append(
                        f"  {entry.get('group', '')}/{entry.get('name', '')} = "
                        f"{entry.get('value', '')} [{location_text}]"
                    )
    return "\n".join(lines) + "\n"


def render_text(report: Mapping[str, Any]) -> str:
    """Render a report as concise issues-first text."""

    if report.get("schema_version") == "envlens.runtime/v1":
        return _runtime_text(report)
    return _diff_text(report)


def _markdown_cell(value: Any) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def render_markdown(report: Mapping[str, Any]) -> str:
    """Render a report as deterministic GitHub-flavored Markdown."""

    if report.get("schema_version") == "envlens.runtime/v1":
        summary = report.get("summary", {})
        lines = [
            f"# EnvLens runtime — {_value(summary, 'status', 'unknown').upper()}",
            "",
            "| Interpreter | Check | Status | Reason |",
            "| --- | --- | --- | --- |",
        ]
        interpreters = report.get("interpreters", [])
        if isinstance(interpreters, list):
            for interpreter in interpreters:
                if not isinstance(interpreter, dict):
                    continue
                checks = interpreter.get("checks", [])
                if not isinstance(checks, list):
                    continue
                for check in checks:
                    if isinstance(check, dict):
                        lines.append(
                            "| "
                            + " | ".join(
                                _markdown_cell(value)
                                for value in (
                                    interpreter.get("requested_executable", ""),
                                    f"{check.get('kind', '')}: "
                                    f"{check.get('name', check.get('group', ''))}",
                                    check.get("status", "unknown"),
                                    check.get("reason", ""),
                                )
                            )
                            + " |"
                        )
        return "\n".join(lines) + "\n"
    lines = [
        f"# EnvLens diff — {_value(report, 'status', 'unknown').upper()}",
        "",
        "| Category | Project | Version | Certainty |",
        "| --- | --- | --- | --- |",
    ]
    for category in ("added", "removed", "upgraded", "downgraded", "changed"):
        entries = report.get(category, [])
        if not isinstance(entries, list):
            continue
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            version = (
                f"{entry.get('from_version', '')} → {entry.get('to_version', '')}"
                if category in {"upgraded", "downgraded", "changed"}
                else entry.get("version", "")
            )
            lines.append(
                "| "
                + " | ".join(
                    _markdown_cell(value)
                    for value in (
                        category,
                        entry.get("name", entry.get("normalized_name", "")),
                        version,
                        entry.get("certainty", "certain"),
                    )
                )
                + " |"
            )
    lines.extend(["", "## Compatibility", ""])
    lines.extend(
        [
            "| Kind | Name | Status | Certainty | Reason |",
            "| --- | --- | --- | --- | --- |",
        ]
    )
    compatibility = report.get("compatibility", [])
    if isinstance(compatibility, list):
        for item in compatibility:
            if isinstance(item, dict):
                lines.append(
                    "| "
                    + " | ".join(
                        _markdown_cell(item.get(key, ""))
                        for key in ("kind", "name", "status", "certainty", "reason")
                    )
                    + " |"
                )
    lines.extend(["", "## Dependency issues", ""])
    dependencies = report.get("dependencies", [])
    if isinstance(dependencies, list) and dependencies:
        lines.extend(
            [
                "| Kind | Name | Requirement | Certainty | Reason |",
                "| --- | --- | --- | --- | --- |",
            ]
        )
        for item in dependencies:
            if isinstance(item, dict):
                lines.append(
                    "| "
                    + " | ".join(
                        _markdown_cell(item.get(key, ""))
                        for key in ("kind", "name", "requirement", "certainty", "reason")
                    )
                    + " |"
                )
    else:
        lines.append("No dependency issues recorded.")
    return "\n".join(lines) + "\n"


def dumps_report(report: Mapping[str, Any], *, pretty: bool = False) -> str:
    """Serialize a report to canonical JSON with one trailing newline."""

    try:
        value = json.dumps(
            report,
            ensure_ascii=True,
            sort_keys=True,
            indent=2 if pretty else None,
            separators=None if pretty else (",", ":"),
            allow_nan=False,
        )
    except (TypeError, ValueError, RecursionError) as error:
        raise ReportError("report-serialization-failed", str(error)) from error
    encoded_size = len(value.encode("utf-8")) + 1
    if encoded_size > MAX_REPORT_BYTES:
        raise ReportError("report-too-large", "report exceeds 16 MiB")
    return value + "\n"


def render_report(report: Mapping[str, Any], *, format: str = "text", pretty: bool = False) -> str:
    """Render a report in ``text``, ``json``, or ``markdown`` format."""

    normalized = format.lower()
    if normalized in {"json", "js"}:
        return dumps_report(report, pretty=pretty)
    if normalized in {"markdown", "md"}:
        return render_markdown(report)
    if normalized in {"text", "txt"}:
        return render_text(report)
    raise ReportError("invalid-format", "format must be text, json, or markdown")


def report_to_text(report: Mapping[str, Any]) -> str:
    """Compatibility alias for :func:`render_text`."""

    return render_text(report)


def report_to_markdown(report: Mapping[str, Any]) -> str:
    """Compatibility alias for :func:`render_markdown`."""

    return render_markdown(report)


def dumps_diff(report: Mapping[str, Any], *, pretty: bool = False) -> str:
    """Compatibility helper for canonical JSON diff reports."""

    return dumps_report(report, pretty=pretty)


def dumps_runtime(report: Mapping[str, Any], *, pretty: bool = False) -> str:
    """Compatibility helper for canonical JSON runtime reports."""

    return dumps_report(report, pretty=pretty)


def render_diff(report: Mapping[str, Any], *, format: str = "text", pretty: bool = False) -> str:
    """Compatibility helper for rendering a diff report."""

    return render_report(report, format=format, pretty=pretty)


def render_runtime(report: Mapping[str, Any], *, format: str = "text", pretty: bool = False) -> str:
    """Compatibility helper for rendering a runtime report."""

    return render_report(report, format=format, pretty=pretty)
