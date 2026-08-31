#!/usr/bin/env python3
"""Run and aggregate the opt-in LogLens large-file benchmark.

The runner deliberately uses only Python 3.10's standard library.  The input
log and per-process output are kept in a scratch directory; the artifact
directory receives reports and small JSON samples only, so a one-gigabyte log
cannot be uploaded accidentally.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import re
import shutil
import signal
import statistics
import subprocess
import sys
import tempfile
import time
from collections.abc import Iterable, Mapping, Sequence
from pathlib import Path
from typing import Any

SCHEMA = "loglens-benchmark/v1"
TOOLCHAIN_SCHEMA = "loglens-benchmark-toolchain/v1"
DEFAULT_BYTES = 1 << 30
DEFAULT_RECORDS = 1_000_000
DEFAULT_REPETITIONS = 3
DEFAULT_TIMEOUT_SECONDS = 180
DEFAULT_CAPACITIES = (8192, 16384, 32768, 65536, 131072, 262144)
CANONICAL = (DEFAULT_BYTES, DEFAULT_RECORDS)
METRIC_NAMES = (
    "first_result_ms",
    "first_paint_ms",
    "load_ms",
    "throughput_mib_s",
    "records_per_s",
    "peak_rss_mib",
)
UPPER_BOUND_BUDGETS = {
    "first_result_ms": "first_result_ms",
    "load_ms": "load_ms",
}
LOWER_BOUND_BUDGETS = {
    "throughput_mib_s": "throughput_mib_s",
    "records_per_s": "records_per_s",
}


class RunnerError(RuntimeError):
    """A controlled runner failure that should still produce a report."""


def positive_int(text: str, option: str) -> int:
    if not re.fullmatch(r"[0-9]+", text):
        raise argparse.ArgumentTypeError(f"{option} must be a positive decimal integer")
    value = int(text, 10)
    if value <= 0:
        raise argparse.ArgumentTypeError(f"{option} must be positive")
    return value


def capacities(text: str) -> list[int]:
    values = [positive_int(item.strip(), "--capacities") for item in text.split(",")]
    if not values or len(set(values)) != len(values):
        raise argparse.ArgumentTypeError(
            "--capacities must contain unique positive integers"
        )
    return values


def components(text: str) -> list[str]:
    values = [item.strip() for item in text.split(",") if item.strip()]
    if not values or any(item not in {"core", "gui"} for item in values):
        raise argparse.ArgumentTypeError("--components must contain only core and gui")
    if len(set(values)) != len(values):
        raise argparse.ArgumentTypeError("--components must not repeat a component")
    return values


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--scratch", type=Path)
    parser.add_argument("--artifact-dir", type=Path, required=True)
    parser.add_argument("--qt-major", type=int, choices=(5, 6), required=True)
    parser.add_argument("--generator", type=Path)
    parser.add_argument("--core", type=Path)
    parser.add_argument("--gui", type=Path)
    parser.add_argument("--components", type=components, default=["core", "gui"])
    parser.add_argument(
        "--bytes",
        type=lambda value: positive_int(value, "--bytes"),
        default=DEFAULT_BYTES,
    )
    parser.add_argument(
        "--records",
        type=lambda value: positive_int(value, "--records"),
        default=DEFAULT_RECORDS,
    )
    parser.add_argument(
        "--capacities",
        type=capacities,
        default=list(DEFAULT_CAPACITIES),
        help="comma-separated retained-record capacities",
    )
    parser.add_argument(
        "--repetitions",
        type=lambda value: positive_int(value, "--repetitions"),
        default=DEFAULT_REPETITIONS,
    )
    parser.add_argument(
        "--timeout-seconds",
        type=lambda value: positive_int(value, "--timeout-seconds"),
        default=DEFAULT_TIMEOUT_SECONDS,
    )
    parser.add_argument(
        "--skip-budgets",
        action="store_true",
        help="record but do not enforce performance budgets (correctness remains enforced)",
    )
    parser.add_argument("--commit", default=os.environ.get("GITHUB_SHA", "unknown"))
    args = parser.parse_args(argv)
    if (
        args.scratch is not None
        and args.scratch.resolve() == args.artifact_dir.resolve()
    ):
        parser.error("--scratch and --artifact-dir must be different directories")
    return args


def json_no_constants(value: str) -> None:
    raise ValueError(f"non-finite JSON constant {value}")


def load_json(path: Path) -> Any:
    try:
        with path.open("r", encoding="utf-8") as stream:
            return json.load(stream, parse_constant=json_no_constants)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise RunnerError(f"cannot read valid JSON from {path}: {error}") from error


def write_json(path: Path, value: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        with path.open("w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True, allow_nan=False)
            stream.write("\n")
    except OSError as error:
        raise RunnerError(f"cannot write {path}: {error}") from error


def tail(text: str, limit: int = 4096) -> str:
    if len(text) <= limit:
        return text
    return "…" + text[-limit:]


class ProcessResult:
    def __init__(
        self,
        returncode: int | None,
        timed_out: bool,
        duration_ms: float,
        stdout: str,
        stderr: str,
    ) -> None:
        self.returncode = returncode
        self.timed_out = timed_out
        self.duration_ms = duration_ms
        self.stdout = stdout
        self.stderr = stderr


def terminate_process_group(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    if os.name == "posix":
        try:
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
        except (OSError, ProcessLookupError):
            pass
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(os.getpgid(process.pid), signal.SIGKILL)
            except (OSError, ProcessLookupError):
                pass
    else:
        process.kill()


def run_process(command: Sequence[str], timeout_seconds: int) -> ProcessResult:
    started = time.monotonic()
    try:
        process = subprocess.Popen(
            list(command),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            errors="replace",
            start_new_session=os.name == "posix",
        )
    except OSError as error:
        return ProcessResult(
            None, False, (time.monotonic() - started) * 1000.0, "", str(error)
        )
    try:
        stdout, stderr = process.communicate(timeout=timeout_seconds)
        return ProcessResult(
            process.returncode,
            False,
            (time.monotonic() - started) * 1000.0,
            stdout,
            stderr,
        )
    except subprocess.TimeoutExpired as error:
        terminate_process_group(process)
        stdout, stderr = process.communicate()
        if not stdout and error.output:
            stdout = (
                error.output
                if isinstance(error.output, str)
                else error.output.decode("utf-8", "replace")
            )
        if not stderr and error.stderr:
            stderr = (
                error.stderr
                if isinstance(error.stderr, str)
                else error.stderr.decode("utf-8", "replace")
            )
        return ProcessResult(
            process.returncode,
            True,
            (time.monotonic() - started) * 1000.0,
            stdout or "",
            stderr or "",
        )


def executable(explicit: Path | None, build_dir: Path, name: str) -> Path:
    if explicit is not None:
        candidate = explicit.expanduser()
        if not candidate.is_absolute():
            candidate = Path.cwd() / candidate
        if not candidate.is_file() or not os.access(candidate, os.X_OK):
            raise RunnerError(f"benchmark executable is not runnable: {candidate}")
        return candidate.resolve()
    direct = build_dir / name
    if direct.is_file() and os.access(direct, os.X_OK):
        return direct.resolve()
    matches = sorted(
        path
        for path in build_dir.rglob(name)
        if path.is_file() and os.access(path, os.X_OK)
    )
    if not matches:
        raise RunnerError(f"cannot find {name} below {build_dir}")
    return matches[0].resolve()


def sha256_and_size(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    try:
        with path.open("rb") as stream:
            while True:
                chunk = stream.read(1024 * 1024)
                if not chunk:
                    break
                size += len(chunk)
                digest.update(chunk)
    except OSError as error:
        raise RunnerError(f"cannot hash generated input {path}: {error}") from error
    return size, digest.hexdigest()


def command_version(command: Sequence[str]) -> str:
    try:
        result = subprocess.run(
            list(command),
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return f"unavailable: {error}"
    return result.stdout.strip()


def toolchain(qt_major: int) -> dict[str, Any]:
    commands: dict[str, str] = {
        "python": sys.version,
        "cmake": command_version(("cmake", "--version")),
        "compiler": command_version(("c++", "--version")),
        "pkg_config": command_version(("pkg-config", "--version")),
    }
    qmake = "qmake6" if qt_major == 6 else "qmake"
    commands["qmake"] = command_version((qmake, "-v"))
    commands["qt_version"] = command_version((qmake, "-query", "QT_VERSION"))
    memory = None
    meminfo = Path("/proc/meminfo")
    if meminfo.is_file():
        try:
            for line in meminfo.read_text(encoding="utf-8").splitlines():
                if line.startswith("MemTotal:"):
                    memory = line.split(":", 1)[1].strip()
                    break
        except OSError:
            memory = None
    return {
        "schema": TOOLCHAIN_SCHEMA,
        "qt_major": qt_major,
        "python_version": platform.python_version(),
        "platform": platform.platform(),
        "machine": platform.machine(),
        "cpu_count": os.cpu_count(),
        "memory_total": memory,
        "commands": commands,
    }


def parse_generator_stdout(
    stdout: str, expected_bytes: int, expected_records: int
) -> None:
    lines = [line.strip() for line in stdout.splitlines() if line.strip()]
    if not lines:
        raise RunnerError("generator produced no JSON summary")
    try:
        value = json.loads(lines[-1], parse_constant=json_no_constants)
    except (ValueError, json.JSONDecodeError) as error:
        raise RunnerError(f"generator summary is not valid JSON: {error}") from error
    if not isinstance(value, dict):
        raise RunnerError("generator summary must be an object")
    if value.get("schema") != "loglens-benchmark-generator/v1":
        raise RunnerError("generator summary has an unexpected schema")
    if value.get("bytes") != expected_bytes or value.get("records") != expected_records:
        raise RunnerError(
            "generator summary dimensions do not match the requested input"
        )
    if value.get("newlines") != expected_records:
        raise RunnerError("generator summary newline count does not match records")


def generate_input(
    generator: Path,
    input_path: Path,
    expected_bytes: int,
    expected_records: int,
    timeout_seconds: int,
) -> tuple[int, str, str]:
    input_path.parent.mkdir(parents=True, exist_ok=True)
    result = run_process(
        (
            str(generator),
            "--output",
            str(input_path),
            "--bytes",
            str(expected_bytes),
            "--records",
            str(expected_records),
        ),
        timeout_seconds,
    )
    if result.timed_out:
        raise RunnerError(f"generator timed out after {timeout_seconds}s")
    if result.returncode != 0:
        raise RunnerError(
            f"generator failed with exit code {result.returncode}: {tail(result.stderr)}"
        )
    parse_generator_stdout(result.stdout, expected_bytes, expected_records)
    if not input_path.is_file():
        raise RunnerError(f"generator did not create {input_path}")
    actual_bytes, digest = sha256_and_size(input_path)
    if actual_bytes != expected_bytes:
        raise RunnerError(
            f"generated input has {actual_bytes} bytes, expected {expected_bytes}"
        )
    return actual_bytes, digest, f"generation_ms={result.duration_ms:.3f}"


def mapping(value: Any, name: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise RunnerError(f"raw result field {name} must be an object")
    return value


def finite_number(value: Any, name: str, allow_none: bool = False) -> float | None:
    if value is None and allow_none:
        return None
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise RunnerError(f"raw result field {name} must be a finite number")
    number = float(value)
    if not math.isfinite(number) or number < 0:
        raise RunnerError(f"raw result field {name} must be finite and non-negative")
    return number


def nonnegative_int(value: Any, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise RunnerError(f"raw result field {name} must be a non-negative integer")
    return value


def validate_raw(
    raw: Any,
    component: str,
    qt_major: int,
    capacity: int,
    expected_bytes: int,
    expected_records: int,
) -> tuple[dict[str, Any], list[str]]:
    root = mapping(raw, "root")
    errors: list[str] = []
    if root.get("schema") != SCHEMA:
        errors.append(f"unexpected schema {root.get('schema')!r}")
    if root.get("component") != component:
        errors.append(f"component is {root.get('component')!r}, expected {component!r}")
    raw_qt = root.get("qt_major")
    if component == "gui" and raw_qt != qt_major:
        errors.append(f"GUI qt_major is {raw_qt!r}, expected {qt_major}")
    if component == "core" and raw_qt not in (0, qt_major):
        errors.append(f"core qt_major is {raw_qt!r}, expected 0 or {qt_major}")
    config = mapping(root.get("configuration"), "configuration")
    if config.get("capacity") != capacity:
        errors.append(f"capacity is {config.get('capacity')!r}, expected {capacity}")
    raw_input = mapping(root.get("input"), "input")
    if raw_input.get("bytes") != expected_bytes:
        errors.append(
            f"input bytes are {raw_input.get('bytes')!r}, expected {expected_bytes}"
        )
    if raw_input.get("expected_records") != expected_records:
        errors.append(
            f"expected_records is {raw_input.get('expected_records')!r}, expected {expected_records}"
        )
    metrics = mapping(root.get("metrics"), "metrics")
    for name in METRIC_NAMES:
        try:
            finite_number(
                metrics.get(name),
                f"metrics.{name}",
                allow_none=name == "first_paint_ms",
            )
        except RunnerError as error:
            errors.append(str(error))
    try:
        nonnegative_int(metrics.get("source_chunks"), "metrics.source_chunks")
    except RunnerError as error:
        errors.append(str(error))
    retention = mapping(root.get("retention"), "retention")
    expected_retained = min(capacity, expected_records)
    expected_oldest = expected_records - expected_retained + 1
    expected_retention = {
        "seen": expected_records,
        "retained": expected_retained,
        "dropped": expected_records - expected_retained,
        "oldest_line": expected_oldest,
        "newest_line": expected_records,
    }
    for name, expected in expected_retention.items():
        try:
            actual = nonnegative_int(retention.get(name), f"retention.{name}")
        except RunnerError as error:
            errors.append(str(error))
            continue
        if actual != expected:
            errors.append(f"retention.{name} is {actual}, expected {expected}")
    checks = mapping(root.get("checks"), "checks")
    if checks.get("correctness") is not True:
        errors.append("raw checks.correctness is not true")
    if root.get("error") not in ("", None):
        errors.append(f"raw result error: {root.get('error')}")
    return dict(root), errors


def budget_failures(
    component: str, metrics: Mapping[str, Any], budgets: Mapping[str, float]
) -> list[str]:
    failures: list[str] = []
    for metric, limit in UPPER_BOUND_BUDGETS.items():
        value = metrics.get(metric)
        if (
            not isinstance(value, (int, float))
            or isinstance(value, bool)
            or value > budgets[limit]
        ):
            failures.append(f"{metric}>{budgets[limit]}")
    for metric, limit in LOWER_BOUND_BUDGETS.items():
        value = metrics.get(metric)
        if (
            not isinstance(value, (int, float))
            or isinstance(value, bool)
            or value < budgets[limit]
        ):
            failures.append(f"{metric}<{budgets[limit]}")
    rss_budget_name = f"{component}_peak_rss_mib"
    rss = metrics.get("peak_rss_mib")
    if (
        not isinstance(rss, (int, float))
        or isinstance(rss, bool)
        or rss > budgets[rss_budget_name]
    ):
        failures.append(f"peak_rss_mib>{budgets[rss_budget_name]}")
    if component == "gui":
        value = metrics.get("first_paint_ms")
        if (
            not isinstance(value, (int, float))
            or isinstance(value, bool)
            or value > budgets["first_paint_ms"]
        ):
            failures.append(f"first_paint_ms>{budgets['first_paint_ms']}")
    return failures


def raw_result_path(
    raw_dir: Path, component: str, capacity: int, repetition: int
) -> Path:
    return raw_dir / f"{component}-capacity-{capacity}-run-{repetition}.json"


def run_sample(
    executable_path: Path,
    component: str,
    capacity: int,
    repetition: int,
    input_path: Path,
    raw_path: Path,
    expected_bytes: int,
    expected_records: int,
    qt_major: int,
    timeout_seconds: int,
    enforce_budgets: bool,
    budgets: Mapping[str, float],
) -> dict[str, Any]:
    command = (
        str(executable_path),
        "--input",
        str(input_path),
        "--output",
        str(raw_path),
        "--capacity",
        str(capacity),
        "--expected-records",
        str(expected_records),
        "--expected-bytes",
        str(expected_bytes),
    )
    result = run_process(command, timeout_seconds)
    sample: dict[str, Any] = {
        "component": component,
        "qt_major": qt_major,
        "capacity": capacity,
        "repetition": repetition,
        "exit_code": result.returncode,
        "timeout": result.timed_out,
        "duration_ms": result.duration_ms,
        "raw_file": None,
        "result": None,
        "validation_errors": [],
        "checks": {"correctness": False, "budget_pass": False},
        "stdout_tail": tail(result.stdout),
        "stderr_tail": tail(result.stderr),
    }
    validation_errors: list[str] = []
    raw: dict[str, Any] | None = None
    if result.timed_out:
        validation_errors.append(f"process exceeded {timeout_seconds}s timeout")
    if result.returncode != 0:
        validation_errors.append(f"process exited with code {result.returncode}")
    if raw_path.is_file():
        try:
            raw, raw_errors = validate_raw(
                load_json(raw_path),
                component,
                qt_major,
                capacity,
                expected_bytes,
                expected_records,
            )
            validation_errors.extend(raw_errors)
        except RunnerError as error:
            validation_errors.append(str(error))
    else:
        validation_errors.append(f"process did not write {raw_path.name}")
    if raw is not None:
        sample["result"] = raw
        sample["raw_file"] = f"samples/{raw_path.name}"
        sample["checks"]["correctness"] = not validation_errors
        if enforce_budgets:
            sample["budget_failures"] = budget_failures(
                component, mapping(raw.get("metrics"), "metrics"), budgets
            )
        else:
            sample["budget_failures"] = []
    else:
        sample["budget_failures"] = []
    sample["checks"]["budget_pass"] = not validation_errors and (
        not enforce_budgets or not sample["budget_failures"]
    )
    sample["validation_errors"] = validation_errors
    return sample


def nearest_rank_p95(values: Sequence[float]) -> float:
    if not values:
        raise RunnerError("cannot calculate p95 for an empty sample set")
    ordered = sorted(values)
    rank = max(1, math.ceil(len(ordered) * 0.95))
    return ordered[rank - 1]


def summary_stats(values: Iterable[float]) -> dict[str, float]:
    ordered = list(values)
    if not ordered:
        raise RunnerError("cannot calculate statistics for an empty sample set")
    return {
        "min": min(ordered),
        "median": statistics.median(ordered),
        "p95": nearest_rank_p95(ordered),
        "max": max(ordered),
    }


def aggregate_samples(
    samples: Sequence[Mapping[str, Any]],
    components_to_run: Sequence[str],
    capacities_to_run: Sequence[int],
    repetitions: int,
    enforce_budgets: bool,
) -> list[dict[str, Any]]:
    aggregates: list[dict[str, Any]] = []
    for component in components_to_run:
        for capacity in capacities_to_run:
            group = [
                sample
                for sample in samples
                if sample.get("component") == component
                and sample.get("capacity") == capacity
            ]
            metrics: dict[str, Any] = {}
            for metric in METRIC_NAMES:
                values: list[float] = []
                for sample in group:
                    result = sample.get("result")
                    if not isinstance(result, dict):
                        continue
                    raw_metrics = result.get("metrics")
                    if (
                        not isinstance(raw_metrics, dict)
                        or raw_metrics.get(metric) is None
                    ):
                        continue
                    value = raw_metrics[metric]
                    if isinstance(value, (int, float)) and not isinstance(value, bool):
                        values.append(float(value))
                metrics[metric] = summary_stats(values) if values else None
            correctness = len(group) == repetitions and all(
                sample.get("checks", {}).get("correctness") is True for sample in group
            )
            budget_pass = len(group) == repetitions and all(
                sample.get("checks", {}).get("budget_pass") is True for sample in group
            )
            failures = sorted(
                {
                    failure
                    for sample in group
                    for failure in sample.get("validation_errors", [])
                }
            )
            failures.extend(
                sorted(
                    {
                        failure
                        for sample in group
                        for failure in sample.get("budget_failures", [])
                    }
                )
            )
            aggregates.append(
                {
                    "component": component,
                    "capacity": capacity,
                    "repetitions": len(group),
                    "expected_repetitions": repetitions,
                    "metrics": metrics,
                    "correctness": correctness,
                    "budget_pass": budget_pass if enforce_budgets else correctness,
                    "failures": sorted(set(failures)),
                }
            )
    return aggregates


def recommendation(
    aggregates: Sequence[Mapping[str, Any]],
    components_to_run: Sequence[str],
    capacities_to_run: Sequence[int],
    enforce_budgets: bool,
) -> dict[str, Any]:
    if not enforce_budgets:
        return {
            "recommended_default_capacity": None,
            "eligible_capacities": [],
            "rule": "not selected because performance budgets were not enforced",
        }
    eligible: list[int] = []
    objective: dict[int, float] = {}
    for capacity in capacities_to_run:
        rows = [
            row
            for row in aggregates
            if row.get("capacity") == capacity
            and row.get("component") in components_to_run
        ]
        if len(rows) != len(components_to_run) or not all(
            row.get("correctness") is True and row.get("budget_pass") is True
            for row in rows
        ):
            continue
        load_values = [
            row.get("metrics", {}).get("load_ms", {}).get("median") for row in rows
        ]
        if any(not isinstance(value, (int, float)) for value in load_values):
            continue
        eligible.append(capacity)
        objective[capacity] = max(float(value) for value in load_values)
    if not eligible:
        return {
            "recommended_default_capacity": None,
            "eligible_capacities": [],
            "rule": "smallest eligible capacity within 10% of best median load time",
        }
    best = min(objective.values())
    near_best = [
        capacity for capacity in eligible if objective[capacity] <= best * 1.10
    ]
    selected = min(near_best)
    return {
        "recommended_default_capacity": selected,
        "eligible_capacities": sorted(eligible),
        "objective_median_load_ms": {
            str(key): objective[key] for key in sorted(objective)
        },
        "rule": "smallest eligible capacity within 10% of best median load time",
    }


def markdown(document: Mapping[str, Any]) -> str:
    benchmark = mapping(document["benchmark"], "benchmark")
    input_info = mapping(benchmark["input"], "benchmark.input")
    policy = mapping(document["policy"], "policy")
    decision = mapping(document["decision"], "decision")
    status = decision.get("status", "fail").upper()
    lines = [
        "# LogLens large-file benchmark",
        "",
        f"**Status:** `{status}`  ",
        f"**Qt:** `{document.get('environment', {}).get('qt_major')}`  ",
        f"**Commit:** `{benchmark.get('commit')}`",
        "",
        "## Input",
        "",
        f"- Bytes: `{input_info.get('bytes')}`",
        f"- Records: `{input_info.get('records')}`",
        f"- SHA-256: `{input_info.get('sha256')}`",
        "",
        "## Policy",
        "",
        f"- Repetitions: `{policy.get('repetitions')}`",
        f"- Timeout per process: `{policy.get('timeout_seconds')}s`",
        f"- Performance budgets enforced: `{policy.get('enforce_budgets')}`",
        "",
        "## Results",
        "",
        "| Component | Capacity | Runs | Correctness | Budget | First result median | First paint median | Load median | Throughput median | RSS median |",
        "|---|---:|---:|:---:|:---:|---:|---:|---:|---:|---:|",
    ]
    for row in document.get("aggregates", []):
        metrics = row.get("metrics", {})

        def median(name: str, row_metrics: Mapping[str, Any] = metrics) -> str:
            value = row_metrics.get(name)
            if not isinstance(value, dict):
                return "—"
            return f"{value.get('median', 0.0):.3f}"

        lines.append(
            f"| {row.get('component')} | {row.get('capacity')} | "
            f"{row.get('repetitions')}/{row.get('expected_repetitions')} | "
            f"{'PASS' if row.get('correctness') else 'FAIL'} | "
            f"{'PASS' if row.get('budget_pass') else 'FAIL'} | "
            f"{median('first_result_ms')} ms | {median('first_paint_ms')} ms | "
            f"{median('load_ms')} ms | {median('throughput_mib_s')} MiB/s | "
            f"{median('peak_rss_mib')} MiB |"
        )
    lines.extend(
        [
            "",
            "## Capacity decision",
            "",
            f"- Recommended default: `{decision.get('recommended_default_capacity') or 'none'}`",
            f"- Eligible capacities: `{decision.get('eligible_capacities', [])}`",
            f"- Rule: {decision.get('rule')}",
        ]
    )
    errors = document.get("errors", [])
    if errors:
        lines.extend(["", "## Errors", ""])
        lines.extend(f"- {error}" for error in errors)
    return "\n".join(lines) + "\n"


def validate_artifact_dir(artifact_dir: Path) -> None:
    allowed_suffixes = {".json", ".md", ".txt"}
    for path in artifact_dir.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix.lower() not in allowed_suffixes:
            raise RunnerError(f"artifact contains a non-report file: {path}")
        if path.stat().st_size >= DEFAULT_BYTES:
            raise RunnerError(f"artifact contains an unexpectedly large file: {path}")


def write_outputs(document: dict[str, Any], artifact_dir: Path) -> None:
    artifact_dir.mkdir(parents=True, exist_ok=True)
    write_json(artifact_dir / "summary.json", document)
    (artifact_dir / "summary.md").write_text(markdown(document), encoding="utf-8")
    validate_artifact_dir(artifact_dir)


def failure_document(
    args: argparse.Namespace, tc: Mapping[str, Any], errors: list[str]
) -> dict[str, Any]:
    enforce = not args.skip_budgets and (args.bytes, args.records) == CANONICAL
    return {
        "schema": SCHEMA,
        "benchmark": {
            "name": "large-file",
            "commit": args.commit,
            "format": "plain-iso",
            "input": {"bytes": args.bytes, "records": args.records, "sha256": None},
        },
        "environment": {"qt_major": args.qt_major, "toolchain_file": "toolchain.json"},
        "policy": {
            "repetitions": args.repetitions,
            "timeout_seconds": args.timeout_seconds,
            "capacities": args.capacities,
            "enforce_budgets": enforce,
            "budgets": budgets(),
        },
        "samples": [],
        "aggregates": [],
        "decision": {
            "status": "fail",
            "recommended_default_capacity": None,
            "eligible_capacities": [],
            "rule": "smallest eligible capacity within 10% of best median load time",
        },
        "toolchain": dict(tc),
        "errors": errors,
    }


def budgets() -> dict[str, float]:
    return {
        "first_result_ms": 5000.0,
        "first_paint_ms": 5000.0,
        "load_ms": 60000.0,
        "throughput_mib_s": 25.0,
        "records_per_s": 25000.0,
        "core_peak_rss_mib": 256.0,
        "gui_peak_rss_mib": 512.0,
    }


def execute(args: argparse.Namespace) -> int:
    artifact_dir = args.artifact_dir.resolve()
    artifact_dir.mkdir(parents=True, exist_ok=True)
    tc = toolchain(args.qt_major)
    write_json(artifact_dir / "toolchain.json", tc)
    toolchain_text = "\n".join(
        [
            f"qt_major={args.qt_major}",
            f"python_version={tc.get('python_version')}",
            f"platform={tc.get('platform')}",
            f"machine={tc.get('machine')}",
            f"cpu_count={tc.get('cpu_count')}",
            f"memory_total={tc.get('memory_total')}",
            "",
            *[f"[{key}]\n{value}" for key, value in tc.get("commands", {}).items()],
            "",
        ]
    )
    (artifact_dir / "toolchain.txt").write_text(toolchain_text, encoding="utf-8")
    scratch = (
        args.scratch.resolve()
        if args.scratch
        else Path(tempfile.mkdtemp(prefix="loglens-benchmark-"))
    )
    scratch.mkdir(parents=True, exist_ok=True)
    input_path = scratch / "input" / "plain-iso-1GiB.log"
    try:
        input_path.relative_to(artifact_dir)
    except ValueError:
        pass
    else:
        raise RunnerError("generated input must not be inside artifact directory")
    raw_dir = scratch / "raw"
    raw_dir.mkdir(parents=True, exist_ok=True)
    generator = executable(args.generator, args.build_dir, "loglens-bench-generate")
    explicit_paths = {"core": args.core, "gui": args.gui}
    component_names = {"core": "loglens-bench-core", "gui": "loglens-bench-gui"}
    component_paths = {
        component: executable(
            explicit_paths[component], args.build_dir, component_names[component]
        )
        for component in args.components
    }
    actual_bytes, digest, generation_note = generate_input(
        generator, input_path, args.bytes, args.records, args.timeout_seconds
    )
    enforce_budgets = not args.skip_budgets and (args.bytes, args.records) == CANONICAL
    policy_budgets = budgets()
    samples: list[dict[str, Any]] = []
    for component in args.components:
        for capacity in args.capacities:
            for repetition in range(1, args.repetitions + 1):
                raw_path = raw_result_path(raw_dir, component, capacity, repetition)
                sample = run_sample(
                    component_paths[component],
                    component,
                    capacity,
                    repetition,
                    input_path,
                    raw_path,
                    args.bytes,
                    args.records,
                    args.qt_major,
                    args.timeout_seconds,
                    enforce_budgets,
                    policy_budgets,
                )
                if raw_path.is_file():
                    destination = artifact_dir / "samples" / raw_path.name
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(raw_path, destination)
                samples.append(sample)
    aggregates = aggregate_samples(
        samples, args.components, args.capacities, args.repetitions, enforce_budgets
    )
    all_correct = bool(samples) and all(
        sample.get("checks", {}).get("correctness") is True for sample in samples
    )
    errors = [
        f"{sample['component']} capacity={sample['capacity']} repetition={sample['repetition']}: {error}"
        for sample in samples
        for error in sample.get("validation_errors", [])
    ]
    decision = recommendation(
        aggregates, args.components, args.capacities, enforce_budgets
    )
    has_eligible_capacity = (
        isinstance(decision.get("recommended_default_capacity"), int)
        if enforce_budgets
        else True
    )
    decision["status"] = "pass" if all_correct and has_eligible_capacity else "fail"
    document: dict[str, Any] = {
        "schema": SCHEMA,
        "benchmark": {
            "name": "large-file",
            "commit": args.commit,
            "format": "plain-iso",
            "input": {"bytes": actual_bytes, "records": args.records, "sha256": digest},
            "generation": generation_note,
        },
        "environment": {"qt_major": args.qt_major, "toolchain_file": "toolchain.json"},
        "policy": {
            "repetitions": args.repetitions,
            "timeout_seconds": args.timeout_seconds,
            "capacities": args.capacities,
            "enforce_budgets": enforce_budgets,
            "budgets": policy_budgets,
        },
        "samples": samples,
        "aggregates": aggregates,
        "decision": decision,
        "toolchain": tc,
        "errors": errors,
    }
    write_outputs(document, artifact_dir)
    return 0 if decision["status"] == "pass" else 1


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    tc = toolchain(args.qt_major)
    try:
        return execute(args)
    except RunnerError as error:
        artifact_dir = args.artifact_dir.resolve()
        artifact_dir.mkdir(parents=True, exist_ok=True)
        write_json(artifact_dir / "toolchain.json", tc)
        (artifact_dir / "toolchain.txt").write_text(
            f"qt_major={args.qt_major}\npython_version={tc.get('python_version')}\n",
            encoding="utf-8",
        )
        document = failure_document(args, tc, [str(error)])
        write_outputs(document, artifact_dir)
        print(f"benchmark failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
