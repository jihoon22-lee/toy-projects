"""Collect raw metadata inside an explicitly selected Python interpreter."""

from __future__ import annotations

import json
import os
import signal
import subprocess
import sys
import threading
import time
from contextlib import suppress
from pathlib import Path
from typing import Any, BinaryIO, cast

MAX_PROBE_BYTES = 8 * 1024 * 1024
MAX_STDERR_BYTES = 64 * 1024
READER_DRAIN_SECONDS = 2.0

PROBE_SCRIPT = r"""
import importlib.metadata
import json
import os
import pathlib
import platform
import sys
import sysconfig

def scalar(value):
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    return str(value)

def error_record(field, error):
    return {
        "code": "metadata-error",
        "field": field,
        "type": type(error).__name__,
        "message": str(error),
    }

def distribution_record(distribution):
    errors = []
    try:
        metadata = distribution.metadata
    except Exception as error:
        metadata = None
        errors.append(error_record("metadata", error))

    def metadata_value(field):
        if metadata is None:
            return ""
        try:
            return str(metadata.get(field) or "")
        except Exception as error:
            errors.append(error_record(field, error))
            return ""

    try:
        requirements = [str(value) for value in (distribution.requires or [])]
    except Exception as error:
        requirements = []
        errors.append(error_record("requires_dist", error))

    entry_points = []
    try:
        for entry_point in distribution.entry_points:
            try:
                entry_points.append({
                    "group": str(entry_point.group),
                    "name": str(entry_point.name),
                    "value": str(entry_point.value),
                })
            except Exception as error:
                errors.append(error_record("entry_point", error))
    except Exception as error:
        errors.append(error_record("entry_points", error))

    try:
        location = str(distribution.locate_file(""))
    except Exception as error:
        location = ""
        errors.append(error_record("location", error))

    # ``top_level.txt`` is the most reliable offline hint for the import names
    # exposed by a distribution.  A small files-based fallback also handles
    # modern metadata that omits the legacy file.  This is deliberately only a
    # hint: namespace packages and generated importers cannot be inferred from
    # wheel metadata alone.
    import_names = set()
    try:
        top_level = distribution.read_text("top_level.txt") or ""
        import_names.update(
            line.strip()
            for line in top_level.splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        )
    except Exception:
        pass
    if not import_names:
        try:
            for file_name in distribution.files or []:
                parts = str(file_name).replace("\\", "/").split("/")
                if not parts:
                    continue
                first = parts[0]
                if first.endswith((".dist-info", ".egg-info")) or first == "__pycache__":
                    continue
                if first.endswith(".py") and len(parts) == 1:
                    candidate = first[:-3]
                    if candidate != "__init__":
                        import_names.add(candidate)
                elif len(parts) > 1 and first not in {"", "."}:
                    import_names.add(first)
        except Exception:
            pass

    # A wheel's ``Tag:`` records are local metadata and do not require parsing
    # or opening the wheel archive.  Source installs simply report an empty
    # list, which lets the compatibility consumer distinguish unknown from
    # incompatible.
    wheel_tags = []
    try:
        wheel_text = distribution.read_text("WHEEL") or ""
        wheel_tags = [
            line.split(":", 1)[1].strip()
            for line in wheel_text.splitlines()
            if line.lower().startswith("tag:") and ":" in line
        ]
    except Exception:
        pass

    return {
        "name": metadata_value("Name"),
        "version": metadata_value("Version"),
        "metadata": {
            "requires_python": metadata_value("Requires-Python"),
            "requires_dist": requirements,
            "wheel_tags": sorted(set(wheel_tags)),
        },
        "import_names": sorted(import_names),
        "entry_points": entry_points,
        "location": location,
        "errors": errors,
    }

payload = {
    "schema_version": "envlens.probe/v1",
    "identity": {
        "implementation": sys.implementation.name,
        "version": platform.python_version(),
        "version_info": list(sys.version_info),
        "cache_tag": sys.implementation.cache_tag or "",
        "platform": sys.platform,
        "machine": platform.machine(),
        "reported_executable": sys.executable,
        "prefix": sys.prefix,
        "base_prefix": sys.base_prefix,
        "exec_prefix": sys.exec_prefix,
        "compiler": platform.python_compiler(),
        "user_home": str(pathlib.Path.home()),
    },
    "sysconfig": {
        "paths": {str(key): scalar(value) for key, value in sysconfig.get_paths().items()},
        "variables": {
            str(key): scalar(value) for key, value in sysconfig.get_config_vars().items()
        },
    },
    "environment": {str(key): str(value) for key, value in os.environ.items()},
    "distributions": [
        distribution_record(distribution)
        for distribution in importlib.metadata.distributions()
    ],
}
print(json.dumps(payload, ensure_ascii=True, separators=(",", ":"), sort_keys=True))
"""


class ProbeError(ValueError):
    """The selected interpreter could not produce a valid bounded probe."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message


def resolve_interpreter(interpreter: str | Path | None) -> tuple[Path, str]:
    """Resolve one explicit executable without PATH lookup ambiguity."""

    requested = str(sys.executable if interpreter is None else interpreter)
    path = Path(requested)
    if not path.is_file() or not os.access(path, os.X_OK):
        raise ProbeError(
            "invalid-interpreter",
            "interpreter must resolve to an executable regular file",
        )
    try:
        resolved = path.resolve(strict=True)
    except OSError as error:
        raise ProbeError("invalid-interpreter", str(error)) from error
    if not resolved.is_file() or not os.access(resolved, os.X_OK):
        raise ProbeError(
            "invalid-interpreter",
            "interpreter must resolve to an executable regular file",
        )
    return resolved, requested


def _drain(stream: BinaryIO, limit: int, chunks: list[bytes]) -> None:
    retained = 0
    while True:
        try:
            chunk = stream.read(64 * 1024)
        except (OSError, ValueError):
            break
        if not chunk:
            break
        if retained <= limit:
            keep = chunk[: limit + 1 - retained]
            chunks.append(keep)
            retained += len(keep)


def _terminate_tree(process: subprocess.Popen[bytes]) -> None:
    """Best-effort termination of the isolated probe and its descendants."""

    if os.name == "posix":
        # The probe is a session leader. Its process group can outlive the
        # leader when an import hook spawns a child that inherits our pipes.
        with suppress(ProcessLookupError, PermissionError):
            os.killpg(process.pid, signal.SIGTERM)
    else:
        # taskkill is part of Windows and is the only stdlib-adjacent way to
        # terminate an entire descendant tree without native extensions.
        with suppress(OSError, subprocess.TimeoutExpired):
            subprocess.run(
                ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                check=False,
                timeout=2,
            )
    if process.poll() is None:
        with suppress(OSError):
            process.terminate()
        with suppress(subprocess.TimeoutExpired):
            process.wait(timeout=1)
    if os.name == "posix":
        # Kill a surviving descendant group even when the direct interpreter
        # has already exited and therefore cannot be waited on again.
        with suppress(ProcessLookupError, PermissionError):
            os.killpg(process.pid, signal.SIGKILL)
    if process.poll() is None:
        with suppress(OSError):
            process.kill()
        with suppress(subprocess.TimeoutExpired):
            process.wait(timeout=1)


def _finish_readers(
    process: subprocess.Popen[bytes],
    threads: list[threading.Thread],
    streams: tuple[BinaryIO, BinaryIO],
) -> None:
    """Drain ordinary output while placing a hard bound on inherited pipes."""

    deadline = time.monotonic() + READER_DRAIN_SECONDS
    for thread in threads:
        thread.join(max(0.0, deadline - time.monotonic()))
    if any(thread.is_alive() for thread in threads):
        _terminate_tree(process)
        for stream in streams:
            with suppress(OSError, ValueError):
                stream.close()
        for thread in threads:
            thread.join(0.25)


def _run_bounded(command: list[str], timeout_seconds: int) -> tuple[bytes, bytes, int]:
    try:
        process = subprocess.Popen(
            command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=False,
            start_new_session=os.name == "posix",
            creationflags=(
                getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0) if os.name == "nt" else 0
            ),
        )
    except OSError as error:
        raise ProbeError("probe-start-failed", str(error)) from error
    assert process.stdout is not None
    assert process.stderr is not None
    stdout_stream = cast(BinaryIO, process.stdout)
    stderr_stream = cast(BinaryIO, process.stderr)
    stdout_chunks: list[bytes] = []
    stderr_chunks: list[bytes] = []
    threads = [
        threading.Thread(
            target=_drain,
            args=(stdout_stream, MAX_PROBE_BYTES, stdout_chunks),
            daemon=True,
        ),
        threading.Thread(
            target=_drain,
            args=(stderr_stream, MAX_STDERR_BYTES, stderr_chunks),
            daemon=True,
        ),
    ]
    for thread in threads:
        thread.start()
    try:
        return_code = process.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired as error:
        _terminate_tree(process)
        _finish_readers(process, threads, (stdout_stream, stderr_stream))
        raise ProbeError(
            "probe-timeout", f"interpreter exceeded {timeout_seconds} seconds"
        ) from error
    _finish_readers(process, threads, (stdout_stream, stderr_stream))
    return b"".join(stdout_chunks), b"".join(stderr_chunks), return_code


def collect_probe(
    interpreter: str | Path | None, *, timeout_seconds: int
) -> tuple[dict[str, Any], Path, str]:
    """Execute the fixed probe argv and validate its top-level protocol."""

    resolved, requested = resolve_interpreter(interpreter)
    stdout, stderr, return_code = _run_bounded([str(resolved), "-c", PROBE_SCRIPT], timeout_seconds)
    if len(stdout) > MAX_PROBE_BYTES:
        raise ProbeError("probe-output-too-large", "probe JSON exceeds 8 MiB")
    if return_code != 0:
        raise ProbeError(
            "probe-failed",
            f"interpreter exited {return_code}; stderr bytes={len(stderr)}",
        )

    def reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ProbeError("invalid-probe-json", f"duplicate key {key!r}")
            result[key] = value
        return result

    def reject_constant(value: str) -> None:
        raise ProbeError("invalid-probe-json", f"non-finite number {value}")

    try:
        payload = json.loads(
            stdout.decode("utf-8"),
            object_pairs_hook=reject_duplicates,
            parse_constant=reject_constant,
        )
    except ProbeError:
        raise
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ProbeError("invalid-probe-json", str(error)) from error
    if not isinstance(payload, dict) or payload.get("schema_version") != "envlens.probe/v1":
        raise ProbeError("invalid-probe-schema", "expected envlens.probe/v1 object")
    return payload, resolved, requested
