"""envlens command-line interface."""

from __future__ import annotations

import argparse
import os
import sys
from datetime import datetime
from pathlib import Path
from typing import Any

from envlens import __version__
from envlens.diff import DiffError, compare_snapshots, load_snapshot
from envlens.io import write_report, write_snapshot
from envlens.probe import ProbeError, resolve_interpreter
from envlens.project import ProjectError, inspect_pyproject
from envlens.report import ReportError, render_report
from envlens.runtime import RuntimeCheckError, run_runtime_checks
from envlens.snapshot import SnapshotError, collect_snapshot, dumps_snapshot


def _parse_captured_at(value: str) -> datetime:
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError as error:
        raise argparse.ArgumentTypeError("expected an ISO-8601 timestamp") from error
    if parsed.tzinfo is None or parsed.utcoffset() is None:
        raise argparse.ArgumentTypeError("timestamp must include a UTC offset")
    return parsed


def _same_file_if_present(left: Path, right: Path) -> bool:
    try:
        return left.exists() and os.path.samefile(left, right)
    except OSError:
        return False


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="envlens", description=__doc__)
    parser.add_argument("--version", action="version", version=f"envlens {__version__}")
    commands = parser.add_subparsers(dest="command", required=True)
    snapshot = commands.add_parser("snapshot", help="capture one Python environment")
    snapshot.add_argument("--interpreter", type=Path, default=Path(sys.executable))
    snapshot.add_argument("--output", type=Path, default=Path("-"))
    snapshot.add_argument("--timeout-seconds", type=int, default=10)
    snapshot.add_argument("--captured-at", type=_parse_captured_at)
    snapshot.add_argument("--pretty", action="store_true")
    diff = commands.add_parser("diff", help="compare two offline environment snapshots")
    diff.add_argument("--before", "--left", dest="before", type=Path, required=True)
    diff.add_argument("--after", "--right", dest="after", type=Path, required=True)
    diff.add_argument(
        "--project",
        "--pyproject",
        dest="project",
        type=Path,
        help="optional pyproject.toml for project compatibility/dependency checks",
    )
    diff.add_argument("--output", type=Path, default=Path("-"))
    diff.add_argument(
        "--format",
        choices=("text", "json", "markdown", "md"),
        default="text",
        help="report format (default: text)",
    )
    diff.add_argument("--pretty", action="store_true", help="indent JSON reports")
    runtime = commands.add_parser(
        "runtime",
        aliases=["smoke", "runtime-check"],
        help="compile and smoke-test a project with configured interpreters",
    )
    runtime.add_argument("--project-root", type=Path, default=Path("."))
    runtime.add_argument("--pyproject", type=Path)
    runtime.add_argument(
        "--interpreter",
        "-i",
        dest="interpreters",
        action="append",
        type=Path,
        help="interpreter executable (repeat for a matrix; default: current interpreter)",
    )
    runtime.add_argument(
        "--import",
        "--import-module",
        dest="imports",
        action="append",
        default=[],
        help="module to import explicitly (repeat for multiple modules)",
    )
    runtime.add_argument(
        "--compile-path",
        "--compile",
        dest="compile_paths",
        action="append",
        type=Path,
        help="Python file/directory to compile (default: project root)",
    )
    runtime.add_argument(
        "--entry-point",
        "--run-entry-point",
        dest="entry_points",
        action="append",
        default=[],
        help="entry-point name or group/name to inspect or execute",
    )
    runtime.add_argument(
        "--execute-entry-points",
        "--execute",
        action="store_true",
        help="opt in to executing selected/all inspectable entry points",
    )
    runtime.add_argument("--timeout-seconds", type=int, default=10)
    runtime.add_argument("--output", type=Path, default=Path("-"))
    runtime.add_argument(
        "--format",
        choices=("text", "json", "markdown", "md"),
        default="text",
        help="report format (default: text)",
    )
    runtime.add_argument("--pretty", action="store_true", help="indent JSON reports")
    return parser


def _load_snapshot_argument(path: Path) -> dict[str, Any]:
    if path == Path("-"):
        return load_snapshot(sys.stdin)
    return load_snapshot(path)


def _emit_report(report: dict[str, Any], *, output: Path, format: str, pretty: bool) -> None:
    rendered = render_report(report, format=format, pretty=pretty)
    if output == Path("-"):
        sys.stdout.write(rendered)
    else:
        write_report(rendered, output)


def _report_exit_code(report: dict[str, Any]) -> int:
    status = report.get("status")
    if status is None and isinstance(report.get("summary"), dict):
        status = report["summary"].get("status")
    return 0 if status in {None, "unchanged", "passed"} else 1


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "snapshot":
            resolved, _ = resolve_interpreter(args.interpreter)
            if args.output != Path("-") and _same_file_if_present(args.output, resolved):
                raise SnapshotError(
                    "invalid-output", "output must not replace the selected interpreter"
                )
            snapshot = collect_snapshot(
                args.interpreter,
                timeout_seconds=args.timeout_seconds,
                captured_at=args.captured_at,
            )
            if args.output == Path("-"):
                sys.stdout.write(dumps_snapshot(snapshot, pretty=args.pretty))
            else:
                write_snapshot(snapshot, args.output, pretty=args.pretty)
            return 0
        if args.command == "diff":
            before = _load_snapshot_argument(args.before)
            after = _load_snapshot_argument(args.after)
            project = inspect_pyproject(args.project) if args.project is not None else None
            report = compare_snapshots(before, after, project=project)
            _emit_report(
                report,
                output=args.output,
                format=args.format,
                pretty=args.pretty,
            )
            return _report_exit_code(report)
        if args.command in {"runtime", "smoke", "runtime-check"}:
            report = run_runtime_checks(
                args.project_root,
                interpreters=args.interpreters,
                imports=args.imports,
                compile_paths=args.compile_paths,
                entry_points=args.entry_points,
                pyproject=args.pyproject,
                timeout_seconds=args.timeout_seconds,
                execute_entry_points=args.execute_entry_points,
            )
            _emit_report(
                report,
                output=args.output,
                format=args.format,
                pretty=args.pretty,
            )
            return _report_exit_code(report)
        raise SnapshotError("invalid-command", f"unsupported command {args.command!r}")
    except (
        ProbeError,
        SnapshotError,
        DiffError,
        ProjectError,
        RuntimeCheckError,
        ReportError,
    ) as error:
        parser.exit(2, f"envlens: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
