"""Offline, deterministic Python environment snapshots and checks."""

from envlens.diff import DiffError, compare_snapshots, diff_snapshots, load_snapshot
from envlens.io import write_snapshot
from envlens.project import (
    ProjectError,
    inspect_entry_points,
    inspect_project,
    inspect_pyproject,
    load_pyproject,
)
from envlens.report import (
    ReportError,
    dumps_diff,
    dumps_report,
    dumps_runtime,
    render_diff,
    render_markdown,
    render_report,
    render_runtime,
    render_text,
)
from envlens.runtime import RuntimeCheckError, run_runtime_checks, run_smoke
from envlens.snapshot import SnapshotError, collect_snapshot, dumps_snapshot

__all__ = [
    "DiffError",
    "ProjectError",
    "ReportError",
    "RuntimeCheckError",
    "SnapshotError",
    "collect_snapshot",
    "compare_snapshots",
    "diff_snapshots",
    "dumps_diff",
    "dumps_report",
    "dumps_runtime",
    "dumps_snapshot",
    "inspect_entry_points",
    "inspect_project",
    "inspect_pyproject",
    "load_pyproject",
    "load_snapshot",
    "render_diff",
    "render_markdown",
    "render_report",
    "render_runtime",
    "render_text",
    "run_runtime_checks",
    "run_smoke",
    "write_snapshot",
]
__version__ = "0.1.0"
