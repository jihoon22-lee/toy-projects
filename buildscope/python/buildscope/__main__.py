"""BuildScope command-line entry point."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from pathlib import Path

from buildscope.snapshot import SnapshotError, dumps_snapshot, load_compilation_database


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="buildscope",
        description="Convert compile_commands.json to a versioned BuildScope snapshot.",
    )
    parser.add_argument("database", type=Path, help="path to compile_commands.json")
    parser.add_argument("-o", "--output", type=Path, help="write the snapshot to this file")
    parser.add_argument("--pretty", action="store_true", help="indent JSON output")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        rendered = dumps_snapshot(load_compilation_database(args.database), pretty=args.pretty)
        if args.output is None:
            sys.stdout.write(rendered)
        else:
            args.output.write_text(rendered, encoding="utf-8")
    except (OSError, SnapshotError) as error:
        print(f"buildscope: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
