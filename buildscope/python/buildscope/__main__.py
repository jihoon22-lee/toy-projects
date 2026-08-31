"""BuildScope command-line entry point."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from pathlib import Path

from buildscope._io import write_atomic_text
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
        choices=("v1", "v2"),
        default="v2",
        help="emit the normalized v2 contract or the raw v1 compatibility projection",
    )
    parser.add_argument("--pretty", action="store_true", help="indent JSON output")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        database = args.database.absolute()
        rendered = dumps_snapshot(
            snapshot_for_schema(
                load_compilation_database(database, project_root=args.project_root),
                args.schema_version,
            ),
            pretty=args.pretty,
        )
        if args.output is None:
            sys.stdout.write(rendered)
        else:
            write_atomic_text(args.output, rendered, protected=database)
    except (OSError, SnapshotError) as error:
        print(f"buildscope: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
