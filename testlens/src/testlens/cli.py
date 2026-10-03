"""Command-line interface; policy failures and input failures have distinct exit codes."""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

from . import __version__
from .adapters import Limits
from .core import collect, compare, history, policy_violations
from .io import Document, InputError, atomic_write, clean_text, discover, json_text, load_json
from .report import html_report, text_report
from .validation import load_run, validate

POLICIES = {
    "new-failure",
    "new-test-failure",
    "failure",
    "error",
    "missing",
    "slowdown",
    "incomplete",
}


def positive(value: str) -> int:
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("Must be positive")
    return number


def nonnegative(value: str) -> float:
    number = float(value)
    if not math.isfinite(number) or number < 0:
        raise argparse.ArgumentTypeError("Must be finite and nonnegative")
    return number


def parser() -> argparse.ArgumentParser:
    cli = argparse.ArgumentParser(description="Analyze local test results with original evidence")
    cli.add_argument("--version", action="version", version=f"testlens {__version__}")
    subs = cli.add_subparsers(dest="command", required=True)
    gather = subs.add_parser("collect", help="Collect XML into a versioned run")
    gather.add_argument("inputs", nargs="+")
    gather.add_argument("--project", required=True)
    gather.add_argument("--run-id", required=True)
    gather.add_argument(
        "--dialect",
        choices=["auto", "junit", "pytest", "qt", "ctest-junit", "ctest"],
        default="auto",
    )
    gather.add_argument("--source-root", type=Path)
    gather.add_argument("--scope", default="default")
    gather.add_argument("--executed-at", help="RFC3339 execution time, required for history")
    gather.add_argument("--metadata", action="append", default=[], metavar="KEY=VALUE")
    gather.add_argument(
        "--complete", action="store_true", help="Assert all results for this scope were supplied"
    )
    gather.add_argument("--expected-shard", action="append", default=[])
    gather.add_argument("--manifest", type=Path, help="JSON object with test_ids array")
    gather.add_argument(
        "--allow-partial",
        action="store_true",
        help="Keep preceding valid reports on malformed input",
    )
    gather.add_argument("--max-file-bytes", type=positive, default=64 * 1024 * 1024)
    gather.add_argument("--max-total-bytes", type=positive, default=256 * 1024 * 1024)
    gather.add_argument("--max-files", type=positive, default=1024)
    gather.add_argument("--max-cases", type=positive, default=100_000)
    gather.add_argument("--max-depth", type=positive, default=64)
    gather.add_argument("--max-nodes", type=positive, default=1_000_000)
    gather.add_argument("--max-output-chars", type=positive, default=64 * 1024)
    delta = subs.add_parser("diff", help="Compare runs in the same cohort")
    delta.add_argument("baseline", type=Path)
    delta.add_argument("current", type=Path)
    delta.add_argument("--aliases", type=Path, help="JSON map from old test IDs to new IDs")
    delta.add_argument("--relative-threshold", type=nonnegative, default=0.2)
    delta.add_argument("--absolute-threshold", type=nonnegative, default=0.1)
    hist = subs.add_parser("history", help="Observed failure frequency in one cohort")
    hist.add_argument("inputs", nargs="+")
    hist.add_argument("--last", type=positive, default=20)
    report = subs.add_parser("report", help="Standalone offline interactive HTML")
    report.add_argument("current", type=Path)
    report.add_argument("--baseline", type=Path)
    report.add_argument("--history", nargs="+", dest="history_inputs")
    report.add_argument("--last", type=positive, default=20)
    report.add_argument("--output", type=Path, required=True)
    check = subs.add_parser("validate", help="Validate schema and run invariants")
    check.add_argument("input", type=Path)
    for sub in (gather, delta, hist):
        sub.add_argument(
            "--format", choices=["json", "text"], default="json" if sub is gather else "text"
        )
        sub.add_argument("--output", type=Path)
        sub.add_argument("--verbose", action="store_true")
        sub.add_argument("--fail-on", default="", help="Comma-separated explicit CI policy")
    return cli


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        if args.command == "validate":
            validate(load_json(args.input))
            print("Valid TestLens document")
            return 0
        inputs: list[Path] = []
        document: Document
        if args.command == "collect":
            inputs = discover(args.inputs, ".xml", args.max_files)
            metadata = {}
            for item in args.metadata:
                key, sep, value = item.partition("=")
                if not sep or not key or key in metadata:
                    raise InputError("Metadata requires unique KEY=VALUE pairs")
                metadata[key] = value
            manifest = load_json(args.manifest) if args.manifest else None
            document = collect(
                inputs,
                project=args.project,
                run_id=args.run_id,
                dialect=args.dialect,
                root=args.source_root,
                metadata=metadata,
                scope=args.scope,
                declared_complete=args.complete,
                expected_shards=args.expected_shard,
                manifest=manifest,
                allow_partial=args.allow_partial,
                executed_at=args.executed_at,
                limits=Limits(
                    args.max_file_bytes,
                    args.max_total_bytes,
                    args.max_files,
                    args.max_depth,
                    args.max_nodes,
                    args.max_cases,
                    args.max_output_chars,
                ),
            )
            if args.manifest:
                inputs.append(args.manifest)
        elif args.command == "diff":
            inputs = [args.baseline, args.current]
            aliases = load_json(args.aliases) if args.aliases else None
            if aliases is not None and not all(isinstance(v, str) for v in aliases.values()):
                raise InputError("Aliases must map strings to strings")
            document = compare(
                load_run(args.baseline),
                load_run(args.current),
                args.relative_threshold,
                args.absolute_threshold,
                aliases,
            )
            if args.aliases:
                inputs.append(args.aliases)
        elif args.command == "history":
            inputs = discover(args.inputs, ".json")
            document = history([load_run(p) for p in inputs], args.last)
        else:
            inputs = [args.current]
            run = load_run(args.current)
            baseline = load_run(args.baseline) if args.baseline else None
            delta_doc = compare(baseline, run) if baseline else None
            history_doc = None
            if args.baseline:
                inputs.append(args.baseline)
            if args.history_inputs:
                paths = discover(args.history_inputs, ".json")
                inputs.extend(paths)
                history_doc = history([load_run(p) for p in paths], args.last)
            # Protect XML evidence as well as the supplied normalized input files.
            for loaded in [run] + ([baseline] if baseline else []):
                for source in loaded["sources"]:
                    path = Path(source["path"])
                    if not path.is_absolute() and loaded["source_root"]:
                        path = Path(loaded["source_root"]) / path
                    inputs.append(path)
            atomic_write(args.output, html_report(run, delta_doc, history_doc, baseline), inputs)
            print(f"Report: {args.output}")
            return 0
        validate(document)
        policies = set(filter(None, args.fail_on.split(",")))
        if policies - POLICIES:
            raise InputError(f"Unknown failure policies: {sorted(policies - POLICIES)}")
        # Reject policies that have no interpretation for the command.
        allowed = (
            {"failure", "error", "incomplete"}
            if args.command == "collect"
            else {"incomplete"}
            if args.command == "history"
            else POLICIES
        )
        if policies - allowed:
            raise InputError(
                f"Policies not applicable to {args.command}: {sorted(policies - allowed)}"
            )
        output = (
            json_text(document) if args.format == "json" else text_report(document, args.verbose)
        )
        if args.output:
            atomic_write(args.output, output, inputs)
        else:
            sys.stdout.write(output)
        violations = policy_violations(document, policies)
        if violations:
            print(f"Policy violations: {', '.join(violations)}", file=sys.stderr)
            return 1
        return 0
    except (InputError, OSError, ValueError, RecursionError) as exc:
        print(clean_text(f"testlens: {exc}"), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
