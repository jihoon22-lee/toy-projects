"""Bounded, shell-free GCC/Clang include-trace replay."""

from __future__ import annotations

import os
import shutil
import signal
import stat
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Any

from buildscope._replay_policy import (
    COMPILER_NAME,
    DROP_VALUE,
    PRESERVE_VALUE,
    SAFE_LANGUAGES,
    is_rejected,
    is_safe,
    should_drop,
)

MAX_ARGUMENTS = 32_768
MAX_ARGUMENT_CHARS = 1024 * 1024
MAX_TRACE_BYTES = 16 * 1024 * 1024
TRACE_TIMEOUT_SECONDS = 15


class IncludeAnalysisError(ValueError):
    """An entry cannot be traced within the replay security boundary."""


def _is_within(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def _regular_executable(path: Path) -> bool:
    try:
        metadata = path.stat()
    except OSError:
        return False
    return stat.S_ISREG(metadata.st_mode) and os.access(path, os.X_OK)


def _resolve_compiler(value: str, *, project_root: Path) -> Path:
    lexical = Path(value)
    name = lexical.name
    if COMPILER_NAME.fullmatch(name) is None or (
        not lexical.is_absolute() and len(lexical.parts) > 1
    ):
        raise IncludeAnalysisError("only a direct GCC/Clang driver name can be replayed")
    discovered = shutil.which(name, path=os.defpath)
    if discovered is None:
        raise IncludeAnalysisError("the compiler driver is unavailable on the system PATH")
    try:
        approved = Path(discovered).resolve(strict=True)
        compiler = (lexical if lexical.is_absolute() else Path(discovered)).resolve(strict=True)
    except (OSError, RuntimeError) as error:
        raise IncludeAnalysisError("the compiler driver cannot be resolved") from error
    if (
        compiler != approved
        or not _regular_executable(compiler)
        or _is_within(compiler, project_root)
    ):
        raise IncludeAnalysisError("the compiler driver is not an approved system executable")
    return compiler


def _resolved_operand(value: str, cwd: Path) -> Path | None:
    if not value or "\0" in value or value.startswith("-"):
        return None
    try:
        candidate = Path(value)
        return (candidate if candidate.is_absolute() else cwd / candidate).resolve(strict=False)
    except (OSError, RuntimeError):
        return None


def _required_value(argv: list[str], index: int, option: str) -> str:
    if index + 1 >= len(argv) or not argv[index + 1] or "\0" in argv[index + 1]:
        raise IncludeAnalysisError(f"compiler option {option} has no bounded value")
    return argv[index + 1]


def _consume_argument(
    argv: list[str],
    index: int,
    *,
    cwd: Path,
    source: Path,
    after_separator: bool,
) -> tuple[int, tuple[str, ...], int, bool]:
    token = argv[index]
    if token == "--":
        return index + 1, (), 0, True
    if token.startswith("@") or token == "-":
        raise IncludeAnalysisError("response files and compiler stdin are not replayed")
    if _resolved_operand(token, cwd) == source:
        return index + 1, (), 1, after_separator
    if after_separator or not token.startswith("-"):
        raise IncludeAnalysisError("compiler argv contains an extra input operand")
    if token in DROP_VALUE:
        _required_value(argv, index, token)
        return index + 2, (), 0, after_separator
    if should_drop(token):
        return index + 1, (), 0, after_separator
    if is_rejected(token):
        raise IncludeAnalysisError(f"unsafe compiler option is not replayed: {token}")
    if token in PRESERVE_VALUE:
        value = _required_value(argv, index, token)
        if token in {"-x", "--language"} and value not in SAFE_LANGUAGES:
            raise IncludeAnalysisError(f"unsupported compiler language: {value}")
        return index + 2, (token, value), 0, after_separator
    if not is_safe(token):
        raise IncludeAnalysisError(f"compiler option is outside the replay allowlist: {token}")
    return index + 1, (token,), 0, after_separator


def sanitized_arguments(argv: list[str], *, cwd: Path, source: Path) -> list[str]:
    """Return read-only preprocessor arguments from one normalized invocation."""

    if len(argv) > MAX_ARGUMENTS or sum(len(value) for value in argv) > MAX_ARGUMENT_CHARS:
        raise IncludeAnalysisError("compiler argv exceeds the replay limit")
    if any(not value or "\0" in value for value in argv):
        raise IncludeAnalysisError("compiler argv contains an invalid argument")
    kept: list[str] = []
    source_operands = 0
    index = 1
    after_separator = False
    while index < len(argv):
        index, preserved, source_count, after_separator = _consume_argument(
            argv,
            index,
            cwd=cwd,
            source=source,
            after_separator=after_separator,
        )
        kept.extend(preserved)
        source_operands += source_count
    if source_operands != 1:
        raise IncludeAnalysisError("compiler argv must identify its source exactly once")
    return kept


def _native_entry_paths(entry: dict[str, Any], project_root: Path) -> tuple[Path, Path]:
    normalized = entry["normalized"]
    if normalized["command_style"] != ("windows" if os.name == "nt" else "posix"):
        raise IncludeAnalysisError("foreign-platform compiler commands cannot be replayed")
    database_source = Path(entry["file"])
    database_directory = Path(entry["directory"])
    cwd = (
        database_directory
        if database_directory.is_absolute()
        else project_root / database_directory
    )
    try:
        cwd = cwd.resolve(strict=True)
        source = database_source if database_source.is_absolute() else cwd / database_source
        source = source.resolve(strict=True)
    except (OSError, RuntimeError) as error:
        raise IncludeAnalysisError("the compilation directory or source is stale") from error
    if not cwd.is_dir() or not source.is_file() or not _is_within(source, project_root):
        raise IncludeAnalysisError("the compilation source must be a regular project file")
    return cwd, source


def entry_paths(entry: dict[str, Any], project_root: Path) -> tuple[Path, Path]:
    """Resolve and validate a native compilation directory and project source."""

    return _native_entry_paths(entry, project_root)


def build_trace_command(entry: dict[str, Any], project_root: Path) -> tuple[list[str], Path, Path]:
    """Return a read-only compiler command or reject the untrusted database entry."""

    project_root = project_root.resolve(strict=True)
    cwd, source = _native_entry_paths(entry, project_root)
    normalized = entry["normalized"]
    argv = list(normalized["argv"])
    compiler_name = normalized["compiler"]["name"]
    compiler_path = normalized["compiler"]["path"]
    compiler_index = next(
        (
            index
            for index, value in enumerate(argv)
            if value == compiler_path or Path(value).name == compiler_name
        ),
        -1,
    )
    if compiler_index < 0:
        raise IncludeAnalysisError("normalized compiler is absent from compiler argv")
    compiler_argv = argv[compiler_index:]
    compiler = _resolve_compiler(compiler_argv[0], project_root=project_root)
    arguments = sanitized_arguments(compiler_argv, cwd=cwd, source=source)
    command = [
        str(compiler),
        *arguments,
        "-fdiagnostics-color=never",
        "-w",
        "-E",
        "-H",
        "-o",
        os.devnull,
        str(source),
    ]
    return command, cwd, source


def run_trace(command: list[str], cwd: Path) -> tuple[int, str, int]:
    """Run one bounded trace and return status, stderr, and elapsed milliseconds."""

    environment = {"LANG": "C", "LC_ALL": "C", "PATH": os.defpath, "TERM": "dumb"}
    started = time.monotonic()
    with tempfile.TemporaryFile() as stderr:
        try:
            process = subprocess.Popen(
                command,
                cwd=cwd,
                env=environment,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=stderr,
                start_new_session=os.name != "nt",
            )
        except OSError as error:
            raise IncludeAnalysisError("compiler include trace could not start") from error
        deadline = started + TRACE_TIMEOUT_SECONDS
        returncode = process.poll()
        while returncode is None:
            if stderr.tell() > MAX_TRACE_BYTES:
                _stop_process(process)
                raise IncludeAnalysisError("compiler include trace exceeds the output limit")
            if time.monotonic() >= deadline:
                _stop_process(process)
                raise IncludeAnalysisError("compiler include trace timed out")
            time.sleep(0.01)
            returncode = process.poll()
        duration_ms = round((time.monotonic() - started) * 1000)
        size = stderr.tell()
        if size > MAX_TRACE_BYTES:
            raise IncludeAnalysisError("compiler include trace exceeds the output limit")
        stderr.seek(0)
        return returncode, stderr.read().decode("utf-8", errors="replace"), duration_ms


def _stop_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    try:
        if os.name != "nt":
            os.killpg(process.pid, signal.SIGKILL)
        else:
            process.kill()
    except ProcessLookupError:
        process.wait()
        return
    except OSError as error:
        raise IncludeAnalysisError("compiler include trace could not be stopped") from error
    process.wait()
