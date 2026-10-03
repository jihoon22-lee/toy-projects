from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from .analysis import inspect
from .model import SNAPSHOT, VERSION, InputError, Limits
from .report import check, diff, graph, text
from .resolver import SYSTEM_PATHS
from .storage import load, save


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(
        description="Explain systemd configuration on disk; never control services"
    )
    result.add_argument("--version", action="version", version=VERSION)
    commands = result.add_subparsers(dest="command", required=True)
    for name in ("inspect", "explain", "graph", "snapshot"):
        command = commands.add_parser(name)
        command.add_argument("unit")
        command.add_argument("--root", default="/", help="rootfs containing system unit load paths")
        command.add_argument(
            "--unit-path",
            action="append",
            help="replace default load paths (ordered highest first)",
        )
        command.add_argument(
            "--include-default-dependencies",
            action="store_true",
            help="include supported system-service default and Type=dbus edges",
        )
        command.add_argument("--depth", type=int, default=3)
        command.add_argument("--max-files", type=int, default=4096)
        command.add_argument("--max-bytes", type=int, default=64 * 1024 * 1024)
        command.add_argument("--max-nodes", type=int, default=256)
        command.add_argument(
            "--show-values",
            action="store_true",
            help="explicitly include private values in this output",
        )
        if name == "snapshot":
            command.add_argument("--output", required=True)
        else:
            command.add_argument(
                "--format",
                choices=("text", "json", "dot") if name == "graph" else ("text", "json"),
                default="dot" if name == "graph" else "text",
            )
        if name == "explain":
            command.add_argument(
                "--key", required=True, help="section.directive, e.g. Service.ExecStart"
            )
    compare = commands.add_parser("diff")
    compare.add_argument("before")
    compare.add_argument("after")
    compare.add_argument("--format", choices=("text", "json"), default="text")
    compare.add_argument("--fail-on", default="", help="comma-separated changed,unknown")
    checker = commands.add_parser("check")
    checker.add_argument("snapshot")
    checker.add_argument("--format", choices=("text", "json"), default="text")
    checker.add_argument("--fail-on", default="error", help="comma-separated error,warning,unknown")
    return result


def _snapshot(path: str) -> dict[str, Any]:
    document = load(path)
    if document["schema"] != SNAPSHOT:
        raise InputError("expected a snapshot document")
    return document


def _policy(value: str, choices: set[str]) -> set[str]:
    values = set(value.split(",")) - {""}
    if values - choices:
        raise InputError("unknown --fail-on category")
    return values


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        if args.command == "diff":
            policy = _policy(args.fail_on, {"changed", "unknown"})
            document = diff(_snapshot(args.before), _snapshot(args.after))
            if args.format == "json":
                print(json.dumps(document, ensure_ascii=True, sort_keys=True, indent=2))
            else:
                print(
                    f"Changes: {len(document['changes'])} · "
                    f"Unknown comparisons: {len(document['uncertain'])}"
                )
                for change in document["changes"]:
                    print(f"[{change['kind']}] {change['unit']} {change['key'] or ''}")
                for item in document["uncertain"]:
                    print(f"[unknown] {item['unit'] or ''} {item['key']}: {item['reason']}")
            return int(any(document[key] for key in policy))
        if args.command == "check":
            policy = _policy(args.fail_on, {"error", "warning", "unknown"})
            result = check(_snapshot(args.snapshot))
            print(
                json.dumps(result, sort_keys=True, indent=2)
                if args.format == "json"
                else f"Errors: {result['errors']} · Unknown: {result['unknown']} · "
                f"Warnings: {result['warnings']}"
            )
            counts = {
                "error": result["errors"],
                "warning": result["warnings"],
                "unknown": result["unknown"] or result["partial"],
            }
            return int(any(counts[key] for key in policy))
        limits = Limits(
            depth=args.depth, files=args.max_files, total_bytes=args.max_bytes, nodes=args.max_nodes
        )
        snapshot = inspect(
            args.unit,
            root=args.root,
            paths=tuple(args.unit_path) if args.unit_path else SYSTEM_PATHS,
            limits=limits,
            redact=not args.show_values,
            include_defaults=args.include_default_dependencies,
        )
        if args.command == "snapshot":
            inputs = [Path(args.root) / item["path"].lstrip("/") for item in snapshot["files"]]
            save(snapshot, args.output, inputs=inputs)
            print(f"Saved {args.output}")
        elif args.command == "graph" and args.format == "dot":
            print(graph(snapshot), end="")
        elif args.format == "json":
            print(json.dumps(snapshot, ensure_ascii=True, sort_keys=True, indent=2))
        else:
            print(text(snapshot, key=args.key if args.command == "explain" else None), end="")
        return 0
    except (OSError, InputError, UnicodeError) as exc:
        # Do not echo hostile or potentially secret input values in exceptions.
        print(
            f"servicelens: {type(exc).__name__}: input or output could not be processed",
            file=sys.stderr,
        )
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
