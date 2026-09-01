"""Command-line interface for BuildScope configuration diffs."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from pathlib import Path

from buildscope._io import SnapshotIoError, write_atomic_text
from buildscope.diff import DiffError, compare_databases, dumps_diff


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="buildscope diff",
        description="Compare two compile databases using the BuildScope semantic policy.",
    )
    parser.add_argument("before", type=Path, help="baseline compile_commands.json")
    parser.add_argument("after", type=Path, help="current compile_commands.json")
    parser.add_argument("--project-root", type=Path, help="shared project root for both inputs")
    parser.add_argument("--before-project-root", type=Path, help="baseline project root")
    parser.add_argument("--after-project-root", type=Path, help="current project root")
    parser.add_argument("--before-label", default="before", help="stable baseline label")
    parser.add_argument("--after-label", default="after", help="stable current label")
    parser.add_argument(
        "--suppress",
        action="append",
        default=[],
        metavar="CATEGORY[:GLOB]",
        help="mark matching source/category changes as suppressed (repeatable)",
    )
    parser.add_argument("-o", "--output", type=Path, help="write the diff report to this file")
    parser.add_argument("--pretty", action="store_true", help="indent JSON output")
    return parser


def _root(explicit: Path | None, shared: Path | None) -> Path | None:
    return explicit if explicit is not None else shared


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        before, after = args.before.absolute(), args.after.absolute()
        report = compare_databases(
            before,
            after,
            before_project_root=_root(args.before_project_root, args.project_root),
            after_project_root=_root(args.after_project_root, args.project_root),
            before_label=args.before_label,
            after_label=args.after_label,
            suppressions=args.suppress,
        )
        rendered = dumps_diff(report, pretty=args.pretty)
        if args.output is None:
            sys.stdout.write(rendered)
        else:
            write_atomic_text(args.output, rendered, protected=(before, after))
        return 1 if report["summary"]["visible_units"] else 0
    except (DiffError, OSError, SnapshotIoError) as error:
        print(f"buildscope diff: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
