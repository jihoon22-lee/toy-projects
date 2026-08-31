#!/usr/bin/env python3
"""Run and gate DiskMap's deterministic generated-source benchmark."""

from __future__ import annotations

import argparse
import json
import platform
import subprocess
import sys
from pathlib import Path
from typing import Any

SCHEMA = "diskmap-scan-benchmark-summary/v1"


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser()
    result.add_argument("--binary", type=Path, required=True)
    result.add_argument("--entries", type=positive_int, default=1_000_000)
    result.add_argument("--cancel-after", type=positive_int, default=10_000)
    result.add_argument("--timeout-seconds", type=positive_int, default=60)
    result.add_argument("--min-entries-per-s", type=float, default=100_000.0)
    result.add_argument("--max-rss-mib", type=float, default=1536.0)
    result.add_argument("--max-elapsed-ms", type=float, default=30_000.0)
    result.add_argument("--max-cancel-ms", type=float, default=2_000.0)
    result.add_argument("--skip-budgets", action="store_true")
    result.add_argument("--output-dir", type=Path, required=True)
    return result


def run_sample(
    binary: Path, entries: int, timeout: int, cancel_after: int = 0
) -> dict[str, Any]:
    command = [str(binary), "--entries", str(entries), "--generation", "73"]
    if cancel_after:
        command.extend(["--cancel-after", str(cancel_after)])
    completed = subprocess.run(
        command,
        check=False,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"benchmark exited {completed.returncode}: {completed.stderr.strip()}"
        )
    if completed.stderr.strip():
        raise RuntimeError(f"benchmark emitted stderr: {completed.stderr.strip()}")
    payload = json.loads(completed.stdout)
    if payload.get("schema") != "diskmap-scan-benchmark/v1":
        raise RuntimeError("benchmark returned an unsupported schema")
    if payload.get("correctness") is not True:
        raise RuntimeError("benchmark correctness check failed")
    return payload


def failures(
    args: argparse.Namespace, full: dict[str, Any], cancelled: dict[str, Any]
) -> list[str]:
    if args.skip_budgets:
        return []
    checks = (
        (
            float(full["entries_per_s"]) < args.min_entries_per_s,
            "entries_per_s below budget",
        ),
        (float(full["peak_rss_mib"]) > args.max_rss_mib, "peak_rss_mib above budget"),
        (float(full["elapsed_ms"]) > args.max_elapsed_ms, "elapsed_ms above budget"),
        (
            float(cancelled["elapsed_ms"]) > args.max_cancel_ms,
            "cancel elapsed_ms above budget",
        ),
    )
    return [message for failed, message in checks if failed]


def markdown(summary: dict[str, Any]) -> str:
    full = summary["samples"]["full"]
    cancelled = summary["samples"]["cancelled"]
    full_row = (
        f"| full | {full['entries_requested']} | {full['elapsed_ms']:.3f} ms | "
        f"{full['entries_per_s']:.3f} entries/s | {full['peak_rss_mib']:.3f} MiB | PASS |"
    )
    cancelled_row = (
        f"| cancellation | {cancelled['entries_generated']} / "
        f"{cancelled['entries_requested']} | {cancelled['elapsed_ms']:.3f} ms | "
        f"{cancelled['entries_per_s']:.3f} entries/s | "
        f"{cancelled['peak_rss_mib']:.3f} MiB | PASS |"
    )
    return "\n".join(
        (
            "# DiskMap generated-source benchmark",
            "",
            f"**Status:** `{summary['status'].upper()}`",
            "",
            "| Scenario | Entries | Elapsed | Throughput | Peak RSS | Correctness |",
            "|---|---:|---:|---:|---:|:---:|",
            full_row,
            cancelled_row,
            "",
            f"Failures: `{summary['failures']}`",
            "",
        )
    )


def main() -> int:
    args = parser().parse_args()
    binary = args.binary.resolve(strict=True)
    if args.cancel_after > args.entries:
        raise SystemExit("--cancel-after cannot exceed --entries")

    full = run_sample(binary, args.entries, args.timeout_seconds)
    cancelled = run_sample(
        binary, args.entries, args.timeout_seconds, args.cancel_after
    )
    budget_failures = failures(args, full, cancelled)
    summary = {
        "schema": SCHEMA,
        "status": "fail" if budget_failures else "pass",
        "environment": {
            "python": platform.python_version(),
            "platform": platform.platform(),
        },
        "policy": {
            "budgets_enforced": not args.skip_budgets,
            "min_entries_per_s": args.min_entries_per_s,
            "max_rss_mib": args.max_rss_mib,
            "max_elapsed_ms": args.max_elapsed_ms,
            "max_cancel_ms": args.max_cancel_ms,
        },
        "samples": {"full": full, "cancelled": cancelled},
        "failures": budget_failures,
    }
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    (args.output_dir / "summary.md").write_text(markdown(summary), encoding="utf-8")
    print(json.dumps(summary, sort_keys=True))
    return 1 if budget_failures else 0


if __name__ == "__main__":
    sys.exit(main())
