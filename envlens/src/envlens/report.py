"""Deterministic text, JSON, and Markdown renderers for envlens results."""

from __future__ import annotations

import json
from collections.abc import Iterable, Mapping
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


def _summary_line(summary: Mapping[str, Any]) -> str:
    keys = (
        "added",
        "removed",
        "upgraded",
        "downgraded",
        "changed",
        "compatibility_issues",
        "dependency_issues",
    )
    return "Summary: " + ", ".join(f"{key}={summary.get(key, 0)}" for key in keys)


def _change_line(category: str, marker: str, entry: Mapping[str, Any]) -> str:
    name = _value(entry, "name", _value(entry, "normalized_name", "<unknown>"))
    if category in {"upgraded", "downgraded", "changed"}:
        version = f"{_value(entry, 'from_version')} -> {_value(entry, 'to_version')}"
    else:
        version = _value(entry, "version", "unknown")
    imports = entry.get("import_names")
    import_suffix = f" imports={','.join(str(item) for item in imports)}" if imports else ""
    return f"  {marker} {name} {version}{import_suffix}"


def _change_lines(report: Mapping[str, Any]) -> list[str]:
    lines: list[str] = []
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
        lines.extend(
            _change_line(category, marker, entry) for entry in entries if isinstance(entry, dict)
        )
    return lines


def _compatibility_lines(report: Mapping[str, Any]) -> list[str]:
    entries = report.get("compatibility")
    if not isinstance(entries, list) or not entries:
        return []
    lines = ["", "Compatibility:"]
    lines.extend(
        f"  [{_value(item, 'status', 'unknown')}; "
        f"certainty={_value(item, 'certainty', 'unknown')}] "
        f"{_value(item, 'name', '<unknown>')}: {_value(item, 'reason')}"
        for item in entries
        if isinstance(item, dict)
    )
    return lines


def _dependency_lines(report: Mapping[str, Any]) -> list[str]:
    entries = report.get("dependencies")
    if not isinstance(entries, list) or not entries:
        return []
    lines = ["", "Dependency issues:"]
    lines.extend(
        f"  [{_value(item, 'kind', 'unknown')}; "
        f"certainty={_value(item, 'certainty', 'unknown')}] "
        f"{_value(item, 'name', '<unknown>')} {_value(item, 'requirement')}: "
        f"{_value(item, 'reason')}"
        for item in entries
        if isinstance(item, dict)
    )
    return lines


def _import_change_lines(report: Mapping[str, Any]) -> list[str]:
    entries = report.get("import_name_changes")
    if not isinstance(entries, list) or not entries:
        return []
    lines = ["", "Project/import-name changes:"]
    lines.extend(
        f"  {_value(item, 'normalized_project_name')}: "
        f"{', '.join(str(value) for value in item.get('before', [])) or '<none>'} -> "
        f"{', '.join(str(value) for value in item.get('after', [])) or '<none>'}"
        for item in entries
        if isinstance(item, dict)
    )
    return lines


def _diff_text(report: Mapping[str, Any]) -> str:
    summary = _mapping(report.get("summary"))
    lines = [
        f"envlens diff: {_value(report, 'status', 'unknown').upper()}",
        "",
        _summary_line(summary),
    ]
    lines.extend(_change_lines(report))
    lines.extend(_compatibility_lines(report))
    lines.extend(_dependency_lines(report))
    lines.extend(_import_change_lines(report))
    return "\n".join(lines) + "\n"


def _check_line(check: Mapping[str, Any]) -> str:
    check_name = _value(check, "name", _value(check, "group", "entry-point"))
    return (
        f"  [{_value(check, 'status', 'unknown')}] "
        f"{_value(check, 'kind', 'check')} {check_name}: "
        f"{_value(check, 'reason')}"
    )


def _interpreter_lines(interpreter: Mapping[str, Any]) -> list[str]:
    lines = [
        "",
        f"Interpreter {_value(interpreter, 'requested_executable', '<unknown>')} "
        f"({_value(interpreter, 'status', 'unknown')}):",
    ]
    checks = interpreter.get("checks", [])
    if isinstance(checks, list):
        lines.extend(_check_line(check) for check in checks if isinstance(check, dict))
    return lines


def _entry_point_text_lines(project: Mapping[str, Any]) -> list[str]:
    entries = project.get("entry_points", [])
    if not isinstance(entries, list) or not entries:
        return []
    lines = ["", "Entry points (dry inspection):"]
    lines.extend(_entry_point_text_line(entry) for entry in entries if isinstance(entry, dict))
    return lines


def _entry_point_text_line(entry: Mapping[str, Any]) -> str:
    location = entry.get("location", {})
    location_text = (
        f"{location.get('path')}:{location.get('line')}"
        if isinstance(location, dict)
        else "unknown location"
    )
    return (
        f"  {entry.get('group', '')}/{entry.get('name', '')} = "
        f"{entry.get('value', '')} [{location_text}]"
    )


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
            if isinstance(interpreter, dict):
                lines.extend(_interpreter_lines(interpreter))
    project = report.get("project")
    if isinstance(project, dict):
        lines.extend(_entry_point_text_lines(project))
    return "\n".join(lines) + "\n"


def render_text(report: Mapping[str, Any]) -> str:
    """Render a report as concise issues-first text."""

    if report.get("schema_version") == "envlens.runtime/v1":
        return _runtime_text(report)
    return _diff_text(report)


def _markdown_cell(value: Any) -> str:
    return str(value).replace("|", "\\|").replace("\n", " ")


def _markdown_row(values: Iterable[Any]) -> str:
    return "| " + " | ".join(_markdown_cell(value) for value in values) + " |"


def _runtime_check_row(interpreter: Mapping[str, Any], check: Mapping[str, Any]) -> str:
    return _markdown_row(
        (
            interpreter.get("requested_executable", ""),
            f"{check.get('kind', '')}: {check.get('name', check.get('group', ''))}",
            check.get("status", "unknown"),
            check.get("reason", ""),
        )
    )


def _runtime_interpreter_rows(interpreter: Mapping[str, Any]) -> list[str]:
    checks = interpreter.get("checks", [])
    if not isinstance(checks, list):
        return []
    return [_runtime_check_row(interpreter, check) for check in checks if isinstance(check, dict)]


def _runtime_markdown(report: Mapping[str, Any]) -> str:
    summary = _mapping(report.get("summary"))
    lines = [
        f"# EnvLens runtime — {_value(summary, 'status', 'unknown').upper()}",
        "",
        "| Interpreter | Check | Status | Reason |",
        "| --- | --- | --- | --- |",
    ]
    interpreters = report.get("interpreters", [])
    if isinstance(interpreters, list):
        for interpreter in interpreters:
            if isinstance(interpreter, dict):
                lines.extend(_runtime_interpreter_rows(interpreter))
    return "\n".join(lines) + "\n"


def _diff_change_row(category: str, entry: Mapping[str, Any]) -> str:
    version = (
        f"{entry.get('from_version', '')} → {entry.get('to_version', '')}"
        if category in {"upgraded", "downgraded", "changed"}
        else entry.get("version", "")
    )
    return _markdown_row(
        (
            category,
            entry.get("name", entry.get("normalized_name", "")),
            version,
            entry.get("certainty", "certain"),
        )
    )


def _diff_change_rows(report: Mapping[str, Any]) -> list[str]:
    lines: list[str] = []
    for category in ("added", "removed", "upgraded", "downgraded", "changed"):
        entries = report.get(category, [])
        if isinstance(entries, list):
            lines.extend(
                _diff_change_row(category, entry) for entry in entries if isinstance(entry, dict)
            )
    return lines


def _compatibility_markdown_rows(report: Mapping[str, Any]) -> list[str]:
    entries = report.get("compatibility", [])
    if not isinstance(entries, list):
        return []
    return [
        _markdown_row(
            item.get(key, "") for key in ("kind", "name", "status", "certainty", "reason")
        )
        for item in entries
        if isinstance(item, dict)
    ]


def _dependency_markdown_lines(report: Mapping[str, Any]) -> list[str]:
    dependencies = report.get("dependencies", [])
    if not isinstance(dependencies, list) or not dependencies:
        return ["No dependency issues recorded."]
    lines = [
        "| Kind | Name | Requirement | Certainty | Reason |",
        "| --- | --- | --- | --- | --- |",
    ]
    lines.extend(
        _markdown_row(
            item.get(key, "") for key in ("kind", "name", "requirement", "certainty", "reason")
        )
        for item in dependencies
        if isinstance(item, dict)
    )
    return lines


def _diff_markdown(report: Mapping[str, Any]) -> str:
    lines = [
        f"# EnvLens diff — {_value(report, 'status', 'unknown').upper()}",
        "",
        "| Category | Project | Version | Certainty |",
        "| --- | --- | --- | --- |",
    ]
    lines.extend(_diff_change_rows(report))
    lines.extend(
        [
            "",
            "## Compatibility",
            "",
            "| Kind | Name | Status | Certainty | Reason |",
            "| --- | --- | --- | --- | --- |",
        ]
    )
    lines.extend(_compatibility_markdown_rows(report))
    lines.extend(["", "## Dependency issues", ""])
    lines.extend(_dependency_markdown_lines(report))
    return "\n".join(lines) + "\n"


def render_markdown(report: Mapping[str, Any]) -> str:
    """Render a report as deterministic GitHub-flavored Markdown."""

    if report.get("schema_version") == "envlens.runtime/v1":
        return _runtime_markdown(report)
    return _diff_markdown(report)


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
