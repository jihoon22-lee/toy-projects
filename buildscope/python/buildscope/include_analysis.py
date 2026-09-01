"""Bounded include explanation with explicit estimated/measured provenance."""

from __future__ import annotations

import os
import re
import stat
import time
from collections import defaultdict
from pathlib import Path
from typing import Any

from buildscope.compiler_replay import (
    IncludeAnalysisError,
    build_trace_command,
    entry_paths,
    run_trace,
)

MAX_EDGES = 100_000
MAX_SOURCE_BYTES = 4 * 1024 * 1024
DEFAULT_MAX_ANALYSIS_UNITS = 512
MAX_ANALYSIS_UNITS = 4096
DEFAULT_ANALYSIS_BUDGET_SECONDS = 120
MAX_ANALYSIS_BUDGET_SECONDS = 600

_TRACE_LINE = re.compile(r"^(\.+) (.+)$")
_INCLUDE_LINE = re.compile(r'^\s*#\s*include\s*(?P<open>[<"])(?P<name>[^>"\r\n]+)[>"]')
_GCC_MISSING_LINE = re.compile(
    r"^(?P<file>.+?):(?P<line>[0-9]+)(?::[0-9]+)?: (?:fatal )?error: "
    r"(?P<name>[^:\r\n]+): No such file or directory$"
)
_CLANG_MISSING_LINE = re.compile(
    r"^(?P<file>.+?):(?P<line>[0-9]+)(?::[0-9]+)?: (?:fatal )?error: "
    r"['<](?P<name>.+?)[>'] file not found$"
)
_VENDOR_PARTS = frozenset(
    {"_deps", "deps", "external", "externals", "third-party", "third_party", "vendor"}
)


def _is_within(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def _display_path(path: Path, project_root: Path) -> str:
    try:
        return path.resolve(strict=False).relative_to(project_root).as_posix()
    except (OSError, RuntimeError, ValueError):
        return path.resolve(strict=False).as_posix()


def _classification(path: Path | None, *, project_root: Path, build_root: Path) -> str:
    if path is None:
        return "missing"
    resolved = path.resolve(strict=False)
    if _is_within(resolved, project_root):
        relative = resolved.relative_to(project_root)
        build_relative = (
            build_root.relative_to(project_root) if _is_within(build_root, project_root) else None
        )
        if (
            build_relative is not None
            and build_relative.parts
            and (
                build_relative.parts[0] in {"build", "out", ".build"}
                or build_relative.parts[0].startswith("cmake-build-")
            )
            and _is_within(resolved, build_root)
        ):
            return "generated"
        if any(part.casefold() in _VENDOR_PARTS for part in relative.parts):
            return "vendor"
        return "project"
    return "system"


def _file_state(value: os.stat_result) -> tuple[int, int, int, int, int]:
    return value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns, value.st_ctime_ns


def _directives(path: Path) -> list[tuple[int, str, str]]:
    try:
        with path.open("rb") as stream:
            before = os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode) or before.st_size > MAX_SOURCE_BYTES:
                return []
            payload = stream.read(MAX_SOURCE_BYTES + 1)
            after = os.fstat(stream.fileno())
        named_after = path.stat()
        if len(payload) > MAX_SOURCE_BYTES or _file_state(before) != _file_state(after):
            return []
        if (after.st_dev, after.st_ino) != (named_after.st_dev, named_after.st_ino):
            return []
        text = payload.decode("utf-8", errors="replace")
    except OSError:
        return []
    found: list[tuple[int, str, str]] = []
    for line_number, line in enumerate(text.splitlines(), 1):
        match = _INCLUDE_LINE.match(line)
        if match is not None:
            found.append(
                (
                    line_number,
                    match.group("name"),
                    "quote" if match.group("open") == '"' else "angle",
                )
            )
    return found


def _include_roots(entry: dict[str, Any], project_root: Path, cwd: Path) -> list[tuple[str, Path]]:
    roots: list[tuple[str, Path]] = []
    for record in entry["normalized"]["include_paths"]:
        raw = Path(record["path"])
        if record["scope"] == "project":
            root = project_root / raw
        else:
            root = raw if raw.is_absolute() else cwd / raw
        roots.append((record["kind"], root.resolve(strict=False)))
    return roots


def _ordered_search_roots(
    parent: Path,
    delimiter: str,
    roots: list[tuple[str, Path]],
) -> list[tuple[str, Path]]:
    ordered: list[tuple[str, Path]] = []
    if delimiter == "quote":
        ordered.append(("current", parent.parent))
        ordered.extend(record for record in roots if record[0] == "quote")
    ordered.extend(record for record in roots if record[0] in {"include", "framework"})
    ordered.extend(record for record in roots if record[0] == "system")
    ordered.extend(record for record in roots if record[0] == "after")
    return ordered


def _search_records(
    parent: Path,
    requested: str,
    delimiter: str,
    roots: list[tuple[str, Path]],
    selected: Path | None,
    project_root: Path,
) -> tuple[list[dict[str, Any]], list[str]]:
    ordered = _ordered_search_roots(parent, delimiter, roots)
    records: list[dict[str, Any]] = []
    alternatives: list[str] = []
    selected_resolved = selected.resolve(strict=False) if selected is not None else None
    selected_seen = False
    for order, (kind, directory) in enumerate(ordered):
        candidate = (directory / requested).resolve(strict=False)
        exists = candidate.is_file()
        matches_selected = (
            exists and selected_resolved is not None and candidate == selected_resolved
        )
        chosen = matches_selected and not selected_seen
        selected_seen = selected_seen or chosen
        if exists and not matches_selected:
            alternatives.append(_display_path(candidate, project_root))
        records.append(
            {
                "candidate": _display_path(candidate, project_root),
                "exists": exists,
                "kind": kind,
                "order": order,
                "selected": chosen,
            }
        )
    if selected_resolved is not None and not selected_seen:
        records.append(
            {
                "candidate": _display_path(selected_resolved, project_root),
                "exists": selected_resolved.is_file(),
                "kind": "compiler",
                "order": len(records),
                "selected": True,
            }
        )
    return records, sorted(set(alternatives))


def _match_directive(
    parent: Path,
    child: Path,
    roots: list[tuple[str, Path]],
    cursors: dict[Path, int],
    project_root: Path,
    directive_cache: dict[Path, list[tuple[int, str, str]]],
) -> tuple[int, str, str]:
    if parent not in directive_cache:
        directive_cache[parent] = _directives(parent)
    directives = directive_cache[parent]
    start = cursors[parent]
    for offset in range(len(directives)):
        index = (start + offset) % len(directives)
        line, requested, delimiter = directives[index]
        searches, _alternatives = _search_records(
            parent, requested, delimiter, roots, child, project_root
        )
        if any(record["selected"] for record in searches) or child.as_posix().endswith(requested):
            cursors[parent] = index + 1
            return line, requested, delimiter
    return 0, child.name, "unknown"


def estimate_entry(entry: dict[str, Any], project_root: Path) -> dict[str, Any]:
    """Explain lexical include resolution without claiming compiler measurement."""

    project_root = project_root.resolve(strict=True)
    cwd, source = entry_paths(entry, project_root)
    roots = _include_roots(entry, project_root, cwd)
    pending = [source]
    visited: set[Path] = set()
    edges: list[dict[str, Any]] = []
    while pending:
        parent = pending.pop()
        if parent in visited:
            continue
        visited.add(parent)
        for line, requested, delimiter in _directives(parent):
            search, _alternatives = _search_records(
                parent, requested, delimiter, roots, None, project_root
            )
            selected = next(
                (
                    Path(record["candidate"]).resolve(strict=False)
                    if Path(record["candidate"]).is_absolute()
                    else (project_root / record["candidate"]).resolve(strict=False)
                    for record in search
                    if record["exists"]
                ),
                None,
            )
            record = _edge_record(
                parent,
                selected,
                line=line,
                requested=requested,
                delimiter=delimiter,
                roots=roots,
                project_root=project_root,
                build_root=cwd,
                evidence="estimated",
            )
            if selected is None:
                record["classification"] = "unresolved"
            edges.append(record)
            if len(edges) > MAX_EDGES:
                raise IncludeAnalysisError("estimated include graph exceeds the edge limit")
            if selected is not None and _is_within(selected, project_root):
                pending.append(selected)
    return {
        "command": [],
        "diagnostics": [],
        "duration_ms": 0,
        "edges": edges,
        "evidence": "estimated",
    }


def _trace_edge(
    match: re.Match[str],
    stack: list[Path | None],
    cwd: Path,
) -> tuple[Path, Path] | None:
    depth = len(match.group(1))
    if depth > 4096 or depth > len(stack) or depth < 1:
        raise IncludeAnalysisError("compiler include trace has an invalid depth")
    raw_path = match.group(2)
    if raw_path.startswith("<") and raw_path.endswith(">"):
        stack[depth:] = [None]
        return None
    lexical = Path(raw_path)
    try:
        child = (lexical if lexical.is_absolute() else cwd / lexical).resolve(strict=True)
    except (OSError, RuntimeError, ValueError) as error:
        raise IncludeAnalysisError("compiler include trace references a stale path") from error
    parent = stack[depth - 1]
    stack[depth:] = [child]
    return (parent, child) if parent is not None else None


def _trace_edges(stderr: str, source: Path, cwd: Path) -> tuple[list[tuple[Path, Path]], list[str]]:
    stack: list[Path | None] = [source]
    edges: list[tuple[Path, Path]] = []
    unexpected: list[str] = []
    trailer = False
    entries = 0
    for raw_line in stderr.splitlines():
        line = raw_line.rstrip("\r")
        if not line:
            continue
        if line == "Multiple include guards may be useful for:":
            if trailer:
                unexpected.append(line)
            trailer = True
            continue
        entries += 1
        if entries > MAX_EDGES:
            raise IncludeAnalysisError("compiler include trace exceeds the edge limit")
        if trailer:
            candidate = Path(line)
            candidate = candidate if candidate.is_absolute() else cwd / candidate
            if not candidate.is_file():
                unexpected.append(line)
            continue
        match = _TRACE_LINE.match(line)
        if match is None:
            unexpected.append(line)
            continue
        edge = _trace_edge(match, stack, cwd)
        if edge is not None:
            edges.append(edge)
    return edges, unexpected


def _edge_record(
    parent: Path,
    child: Path | None,
    *,
    line: int,
    requested: str,
    delimiter: str,
    roots: list[tuple[str, Path]],
    project_root: Path,
    build_root: Path,
    evidence: str,
    location_evidence: str | None = None,
) -> dict[str, Any]:
    searches, alternatives = _search_records(
        parent, requested, delimiter, roots, child, project_root
    )
    return {
        "alternatives": alternatives,
        "classification": _classification(child, project_root=project_root, build_root=build_root),
        "delimiter": delimiter,
        "evidence": evidence,
        "line": line,
        "location_evidence": location_evidence or ("source-scan" if line > 0 else "unavailable"),
        "parent": _display_path(parent, project_root),
        "requested": requested,
        "resolved": _display_path(child, project_root) if child is not None else None,
        "search": searches,
    }


def _missing_edges(
    stderr: str,
    *,
    roots: list[tuple[str, Path]],
    project_root: Path,
    build_root: Path,
    cwd: Path,
) -> list[dict[str, Any]]:
    records = []
    for raw in stderr.splitlines():
        match = _GCC_MISSING_LINE.match(raw) or _CLANG_MISSING_LINE.match(raw)
        if match is None:
            continue
        lexical = Path(match.group("file"))
        parent = (lexical if lexical.is_absolute() else cwd / lexical).resolve(strict=False)
        records.append(
            _edge_record(
                parent,
                None,
                line=int(match.group("line")),
                requested=match.group("name"),
                delimiter="unknown",
                roots=roots,
                project_root=project_root,
                build_root=build_root,
                evidence="compiler-measured",
                location_evidence="compiler-diagnostic",
            )
        )
    return records


def analyze_entry(entry: dict[str, Any], project_root: Path) -> dict[str, Any]:
    """Replay one normalized entry and explain each compiler-measured include edge."""

    project_root = project_root.resolve(strict=True)
    command, cwd, source = build_trace_command(entry, project_root)
    roots = _include_roots(entry, project_root, cwd)
    returncode, stderr, duration_ms = run_trace(command, cwd)
    raw_edges, unexpected = _trace_edges(stderr, source, cwd)
    cursors: dict[Path, int] = defaultdict(int)
    directive_cache: dict[Path, list[tuple[int, str, str]]] = {}
    edges = []
    for parent, child in raw_edges:
        line, requested, delimiter = _match_directive(
            parent, child, roots, cursors, project_root, directive_cache
        )
        edges.append(
            _edge_record(
                parent,
                child,
                line=line,
                requested=requested,
                delimiter=delimiter,
                roots=roots,
                project_root=project_root,
                build_root=cwd,
                evidence="compiler-measured",
            )
        )
    edges.extend(
        _missing_edges(
            stderr,
            roots=roots,
            project_root=project_root,
            build_root=cwd,
            cwd=cwd,
        )
    )
    diagnostics = []
    if returncode != 0:
        diagnostics.append(
            {
                "code": "compiler-trace-failed",
                "message": f"Compiler include trace exited with status {returncode}.",
                "severity": "warning",
            }
        )
    elif unexpected:
        diagnostics.append(
            {
                "code": "compiler-trace-diagnostics",
                "message": "Compiler emitted non-trace diagnostics; measured edges were retained.",
                "severity": "warning",
            }
        )
    return {
        "command": command,
        "diagnostics": diagnostics,
        "duration_ms": duration_ms,
        "edges": edges,
        "evidence": "compiler-measured",
    }


def _unavailable(code: str, message: str) -> dict[str, Any]:
    return {
        "command": [],
        "diagnostics": [{"code": code, "message": message, "severity": "warning"}],
        "duration_ms": 0,
        "edges": [],
        "evidence": "unavailable",
    }


def _analysis_for_entry(
    entry: dict[str, Any],
    project_root: Path,
    mode: str,
) -> dict[str, Any]:
    try:
        if mode == "compiler":
            return analyze_entry(entry, project_root)
        if mode == "estimate":
            return estimate_entry(entry, project_root)
        raise IncludeAnalysisError(f"unsupported include analysis mode: {mode}")
    except IncludeAnalysisError as error:
        return _unavailable("include-analysis-unavailable", str(error))


def _budget_result(
    index: int,
    *,
    max_units: int,
    elapsed: float,
    budget_seconds: int,
) -> dict[str, Any] | None:
    if index >= max_units:
        return _unavailable(
            "include-analysis-unit-limit",
            f"Include analysis stopped at the configured {max_units} unit limit.",
        )
    if elapsed >= budget_seconds:
        return _unavailable(
            "include-analysis-time-budget",
            f"Include analysis stopped at the configured {budget_seconds} second budget.",
        )
    return None


def annotate_snapshot(
    snapshot: dict[str, Any],
    project_root: Path,
    *,
    mode: str = "compiler",
    max_units: int = DEFAULT_MAX_ANALYSIS_UNITS,
    budget_seconds: int = DEFAULT_ANALYSIS_BUDGET_SECONDS,
) -> dict[str, Any]:
    """Attach compiler measurements to every entry and promote the contract to v3."""

    if not 1 <= max_units <= MAX_ANALYSIS_UNITS:
        raise IncludeAnalysisError(
            f"include analysis unit limit must be between 1 and {MAX_ANALYSIS_UNITS}"
        )
    if not 1 <= budget_seconds <= MAX_ANALYSIS_BUDGET_SECONDS:
        raise IncludeAnalysisError(
            "include analysis time budget must be between 1 and "
            f"{MAX_ANALYSIS_BUDGET_SECONDS} seconds"
        )
    started = time.monotonic()
    for index, entry in enumerate(snapshot["entries"]):
        limited = _budget_result(
            index,
            max_units=max_units,
            elapsed=time.monotonic() - started,
            budget_seconds=budget_seconds,
        )
        entry["include_analysis"] = limited or _analysis_for_entry(entry, project_root, mode)
    snapshot["schema_version"] = "buildscope.snapshot/v3"
    return snapshot
