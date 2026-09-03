"""envlens command-line interface."""

from __future__ import annotations

import argparse
import os
import sys
from datetime import datetime
from pathlib import Path

from envlens import __version__
from envlens.io import write_snapshot
from envlens.probe import ProbeError, resolve_interpreter
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
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
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
    except (ProbeError, SnapshotError) as error:
        parser.exit(2, f"envlens: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
