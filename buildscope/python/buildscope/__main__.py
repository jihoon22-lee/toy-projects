"""BuildScope command-line entry point."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from pathlib import Path

from buildscope._io import write_atomic_text
from buildscope.include_analysis import IncludeAnalysisError, annotate_snapshot
from buildscope.snapshot import (
    SnapshotError,
    dumps_snapshot,
    load_compilation_database,
    snapshot_for_schema,
)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="buildscope",
        description="Convert compile_commands.json to a versioned BuildScope snapshot.",
    )
    parser.add_argument("database", type=Path, help="path to compile_commands.json")
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path.cwd(),
        help="root used for project/vendor/system path classification (default: current directory)",
    )
    parser.add_argument("-o", "--output", type=Path, help="write the snapshot to this file")
    parser.add_argument(
        "--schema-version",
        choices=("v1", "v2", "v3"),
        default=None,
        help="emit v1/v2, or v3 with include explanations (default: v2, or v3 with analysis)",
    )
    parser.add_argument(
        "--include-analysis",
        choices=("estimate", "compiler"),
        help="attach estimated or safely compiler-measured include explanations",
    )
    parser.add_argument(
        "--analysis-max-units",
        type=int,
        default=512,
        help="maximum translation units to inspect (1..4096; default: 512)",
    )
    parser.add_argument(
        "--analysis-time-budget",
        type=int,
        default=120,
        help="overall include-analysis budget in seconds (1..600; default: 120)",
    )
    parser.add_argument("--pretty", action="store_true", help="indent JSON output")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        database = args.database.absolute()
        schema = args.schema_version or ("v3" if args.include_analysis else "v2")
        if args.include_analysis and schema != "v3":
            raise SnapshotError("--include-analysis requires --schema-version v3")
        if schema == "v3" and args.include_analysis is None:
            args.include_analysis = "estimate"
        snapshot = load_compilation_database(database, project_root=args.project_root)
        if args.include_analysis:
            snapshot = annotate_snapshot(
                snapshot,
                Path(args.project_root),
                mode=args.include_analysis,
                max_units=args.analysis_max_units,
                budget_seconds=args.analysis_time_budget,
            )
        rendered = dumps_snapshot(
            snapshot_for_schema(
                snapshot,
                schema,
            ),
            pretty=args.pretty,
        )
        if args.output is None:
            sys.stdout.write(rendered)
        else:
            write_atomic_text(args.output, rendered, protected=database)
    except (IncludeAnalysisError, OSError, SnapshotError) as error:
        print(f"buildscope: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
