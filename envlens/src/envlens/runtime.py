"""Bounded compile, import, and project entry-point runtime checks."""

from __future__ import annotations

import os
import re
import signal
import sys
import tempfile
from collections.abc import Sequence
from pathlib import Path
from typing import Any

from envlens.probe import ProbeError, _run_bounded, resolve_interpreter
from envlens.project import ProjectError, inspect_pyproject

MAX_SOURCE_FILES = 10_000
MAX_SOURCE_BYTES = 64 * 1024 * 1024
MAX_IMPORTS = 1_000
MAX_ENTRY_POINTS = 1_000
MAX_PATHS = 1_000
MAX_PATH_LENGTH = 65_536
MAX_SOURCE_ENTRIES = 100_000
MAX_OUTPUT_CHARS = 65_536
MAX_COMPILE_COMMAND_CHARS = 64 * 1024
MODULE_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*$")

IMPORT_SCRIPT = r"""
import importlib
import sys

module_name = sys.argv[1]
try:
    importlib.import_module(module_name)
except ModuleNotFoundError as error:
    print("ENVLENS_MISSING_IMPORT=" + str(error.name or module_name), file=sys.stderr)
    raise
except BaseException as error:
    print("ENVLENS_IMPORT_ERROR=" + type(error).__name__ + ": " + str(error), file=sys.stderr)
    raise
"""

ENTRY_POINT_SCRIPT = r"""
import importlib
import sys

module_name, attribute_name = sys.argv[1].split(":", 1)
program_name = sys.argv[2] if len(sys.argv) > 2 else module_name
sys.argv[:] = [program_name]
try:
    target = importlib.import_module(module_name)
    for component in attribute_name.split("."):
        target = getattr(target, component)
except ModuleNotFoundError as error:
    print("ENVLENS_MISSING_IMPORT=" + str(error.name or module_name), file=sys.stderr)
    raise
except BaseException as error:
    print("ENVLENS_ENTRY_POINT_ERROR=" + type(error).__name__ + ": " + str(error), file=sys.stderr)
    raise
if not callable(target):
    raise TypeError("entry point target is not callable")
try:
    result = target()
except BaseException as error:
    print("ENVLENS_ENTRY_POINT_ERROR=" + type(error).__name__ + ": " + str(error), file=sys.stderr)
    raise
if result is None:
    raise SystemExit(0)
if isinstance(result, int):
    raise SystemExit(result)
raise SystemExit(0)
"""


class RuntimeCheckError(ValueError):
    """A stable, user-facing runtime-check configuration failure."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message


def _bounded_text(data: bytes) -> str:
    text = data.decode("utf-8", errors="replace")
    if len(text) > MAX_OUTPUT_CHARS:
        return text[:MAX_OUTPUT_CHARS] + "…"
    return text


def _classify_process(
    *,
    stdout: bytes,
    stderr: bytes,
    return_code: int,
    timeout_seconds: int,
    kind: str,
    name: str,
) -> dict[str, Any]:
    stderr_text = _bounded_text(stderr).strip()
    result: dict[str, Any] = {
        "kind": kind,
        "name": name,
        "status": "passed" if return_code == 0 else "failed",
        "return_code": return_code,
    }
    if stdout:
        result["stdout"] = _bounded_text(stdout)
    if stderr_text:
        result["stderr"] = stderr_text
    if return_code < 0:
        number = -return_code
        result["status"] = "signal"
        result["signal"] = number
        try:
            result["signal_name"] = signal.Signals(number).name
        except ValueError:
            result["signal_name"] = f"SIG{number}"
    elif return_code != 0 and "ENVLENS_MISSING_IMPORT=" in stderr_text:
        result["status"] = "missing-import"
        marker = stderr_text.split("ENVLENS_MISSING_IMPORT=", 1)[1].splitlines()[0]
        result["missing_import"] = marker.strip() or name
    elif return_code != 0 and "ENVLENS_IMPORT_ERROR=" in stderr_text:
        result["status"] = "import-error"
    elif return_code != 0 and "ENVLENS_ENTRY_POINT_ERROR=" in stderr_text:
        result["status"] = "entry-point-error"
    if result["status"] == "failed":
        result["reason"] = f"{kind} process exited {return_code}"
    elif result["status"] == "passed":
        result["reason"] = f"{kind} completed successfully"
    result["timeout_seconds"] = timeout_seconds
    return result


def _run_check(
    command: list[str],
    *,
    timeout_seconds: int,
    cwd: Path,
    env: dict[str, str],
    kind: str,
    name: str,
) -> dict[str, Any]:
    try:
        stdout, stderr, return_code = _run_bounded(
            command,
            timeout_seconds,
            cwd=cwd,
            env=env,
        )
    except ProbeError as error:
        if error.code == "probe-timeout":
            return {
                "kind": kind,
                "name": name,
                "status": "timeout",
                "timeout_seconds": timeout_seconds,
                "reason": error.message,
            }
        return {
            "kind": kind,
            "name": name,
            "status": "start-error",
            "reason": error.message,
            "error_code": error.code,
        }
    return _classify_process(
        stdout=stdout,
        stderr=stderr,
        return_code=return_code,
        timeout_seconds=timeout_seconds,
        kind=kind,
        name=name,
    )


def _python_files(paths: Sequence[Path]) -> list[Path]:
    files: list[Path] = []
    total_bytes = 0
    seen: set[tuple[int, int]] = set()
    skip_directories = {".git", ".hg", ".svn", ".tox", ".venv", "venv", "__pycache__"}
    entries_seen = 0
    for path in paths:
        if path.is_symlink():
            continue
        if path.is_file():
            candidates = [path] if path.suffix == ".py" else []
        elif path.is_dir():
            candidates = []
            pending = [path]
            while pending:
                directory = pending.pop()
                try:
                    with os.scandir(directory) as directory_entries:
                        for entry in directory_entries:
                            entries_seen += 1
                            if entries_seen > MAX_SOURCE_ENTRIES:
                                raise RuntimeCheckError(
                                    "source-too-large",
                                    "source tree exceeds 100000 directory entries",
                                )
                            if entry.is_symlink():
                                continue
                            if entry.is_dir(follow_symlinks=False):
                                if entry.name not in skip_directories:
                                    pending.append(Path(entry.path))
                            elif entry.is_file(follow_symlinks=False) and entry.name.endswith(
                                ".py"
                            ):
                                candidates.append(Path(entry.path))
                except OSError as error:
                    raise RuntimeCheckError("source-read-failed", str(error)) from error
        else:
            continue
        for candidate in candidates:
            try:
                stat = candidate.stat()
            except OSError as error:
                raise RuntimeCheckError("source-read-failed", str(error)) from error
            identity = (stat.st_dev, stat.st_ino)
            if identity in seen:
                continue
            seen.add(identity)
            total_bytes += stat.st_size
            if total_bytes > MAX_SOURCE_BYTES:
                raise RuntimeCheckError("source-too-large", "Python source exceeds 64 MiB")
            files.append(candidate)
            if len(files) > MAX_SOURCE_FILES:
                raise RuntimeCheckError("source-too-large", "Python source exceeds 10000 files")
    files.sort(key=lambda item: str(item))
    return files


def _runtime_env(project_root: Path, cache_root: Path | None = None) -> dict[str, str]:
    env = os.environ.copy()
    roots = [str(project_root)]
    src_root = project_root / "src"
    if src_root.is_dir():
        roots.append(str(src_root))
    env["PYTHONPATH"] = os.pathsep.join(roots)
    if cache_root is not None:
        env["PYTHONPYCACHEPREFIX"] = str(cache_root)
    return env


def _interpreter_record(
    requested: str, *, status: str, resolved: Path | None = None
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "requested_executable": requested,
        "status": status,
        "checks": [],
    }
    if resolved is not None:
        result["resolved_executable"] = str(resolved)
    return result


def _resolve_for_runtime(value: str | Path) -> tuple[str, Path | None]:
    requested = str(value)
    path = Path(requested)
    if not path.exists():
        return "missing-interpreter", None
    if not path.is_file() or not os.access(path, os.X_OK):
        return "invalid-interpreter", None
    try:
        resolved, _requested = resolve_interpreter(path)
    except ProbeError:
        return "invalid-interpreter", None
    return "ready", resolved


def _select_entry_points(
    entries: list[dict[str, Any]], requested: Sequence[str] | None
) -> list[dict[str, Any]]:
    if not requested:
        return entries
    selected: list[dict[str, Any]] = []
    wanted = set(requested)
    for entry in entries:
        qualified = f"{entry.get('group', '')}/{entry.get('name', '')}"
        if str(entry.get("name", "")) in wanted or qualified in wanted:
            selected.append(entry)
    return selected


def _entry_check(
    entry: dict[str, Any],
    *,
    resolved: Path,
    project_root: Path,
    timeout_seconds: int,
    execute: bool,
) -> dict[str, Any]:
    result = {
        "kind": "entry-point",
        "group": entry.get("group", ""),
        "name": entry.get("name", ""),
        "value": entry.get("value", ""),
        "location": entry.get("location", {}),
    }
    target = entry.get("target")
    if not execute:
        result.update(
            {
                "status": "inspected",
                "action": "dry-inspection",
                "reason": "entry point was inspected without importing or executing code",
            }
        )
        return result
    if not isinstance(target, dict) or not target.get("module") or not target.get("attribute"):
        result.update(
            {
                "status": "unsupported",
                "action": "execution-skipped",
                "reason": "entry point value is not a module:attribute target",
            }
        )
        return result
    target_text = f"{target['module']}:{target['attribute']}"
    env = _runtime_env(project_root)
    return {
        **result,
        **_run_check(
            [str(resolved), "-c", ENTRY_POINT_SCRIPT, target_text, str(entry.get("name", ""))],
            timeout_seconds=timeout_seconds,
            cwd=project_root,
            env=env,
            kind="entry-point",
            name=str(entry.get("name", "")),
        ),
        "action": "executed",
    }


def _compile_command(interpreter: Path, source_files: Sequence[Path]) -> list[str]:
    command = [str(interpreter), "-m", "compileall", "-q", "-f"]
    command.extend(str(path) for path in source_files)
    command_bytes = sum(
        len(argument.encode("utf-8", errors="surrogatepass")) + 1 for argument in command
    )
    if command_bytes > MAX_COMPILE_COMMAND_CHARS:
        raise RuntimeCheckError(
            "runtime-input-too-large",
            "compileall command exceeds 65536 UTF-8 bytes",
        )
    return command


def _validate_runtime_options(
    timeout_seconds: int, values: Sequence[str] | None, label: str
) -> None:
    if (
        isinstance(timeout_seconds, bool)
        or not isinstance(timeout_seconds, int)
        or timeout_seconds < 1
    ):
        raise RuntimeCheckError("invalid-timeout", "timeout_seconds must be a positive integer")
    if values is not None and len(values) > MAX_IMPORTS:
        raise RuntimeCheckError("runtime-input-too-large", f"{label} exceeds {MAX_IMPORTS} items")


def _validate_paths(values: Sequence[str | Path] | None, label: str) -> None:
    if values is None:
        return
    if len(values) > MAX_PATHS:
        raise RuntimeCheckError("runtime-input-too-large", f"{label} exceeds {MAX_PATHS} items")
    for value in values:
        if len(str(value)) > MAX_PATH_LENGTH:
            raise RuntimeCheckError(
                "runtime-input-too-large", f"{label} contains an oversized path"
            )


def run_runtime_checks(
    project_root: str | Path = ".",
    *,
    interpreters: Sequence[str | Path] | None = None,
    imports: Sequence[str] | None = None,
    compile_paths: Sequence[str | Path] | None = None,
    entry_points: Sequence[str] | None = None,
    pyproject: str | Path | None = None,
    timeout_seconds: int = 10,
    execute_entry_points: bool = False,
) -> dict[str, Any]:
    """Run bounded checks for every configured interpreter.

    Entry points are always inspected first.  Their code is executed only
    when ``execute_entry_points`` is explicitly true.
    """

    _validate_runtime_options(timeout_seconds, imports, "imports")
    _validate_runtime_options(timeout_seconds, entry_points, "entry_points")
    if not isinstance(execute_entry_points, bool):
        raise RuntimeCheckError("invalid-execute-policy", "execute_entry_points must be boolean")
    _validate_paths(compile_paths, "compile_paths")
    _validate_paths(interpreters, "interpreters")
    root = Path(project_root)
    if not root.is_dir():
        raise RuntimeCheckError("project-root-failed", "project root must be a directory")
    pyproject_path = Path(pyproject) if pyproject is not None else root / "pyproject.toml"
    try:
        project_info = inspect_pyproject(pyproject_path)
    except ProjectError as error:
        # Runtime smoke can still be useful without a project file.  Preserve a
        # user-visible inspection result and continue with explicit imports.
        project_info = {
            "schema_version": "envlens.project/v1",
            "path": str(pyproject_path),
            "project": {
                "name": "",
                "normalized_name": "",
                "version": "",
                "requires_python": "",
                "dependencies": [],
            },
            "entry_points": [],
            "warnings": [f"entry-point inspection unavailable: {error}"],
            "inspection_error": {"code": error.code, "message": error.message},
        }
    configuration = project_info.get("configuration", {})
    configured_values = configuration if isinstance(configuration, dict) else {}
    selected_entries = entry_points
    if selected_entries is None:
        selected_entries = configured_values.get("entry_points")
    entries = _select_entry_points(
        [dict(item) for item in project_info.get("entry_points", [])], selected_entries
    )
    modules = list(imports if imports is not None else configured_values.get("imports", []))
    for module in modules:
        if not isinstance(module, str) or not MODULE_RE.fullmatch(module):
            raise RuntimeCheckError("invalid-import", f"invalid import module {module!r}")
    configured_paths = compile_paths
    if configured_paths is None:
        configured_paths = configured_values.get("compile_paths")
    if configured_paths is None:
        paths = [root]
    else:
        _validate_paths(configured_paths, "compile_paths")
        paths = [
            Path(item) if Path(item).is_absolute() else root / Path(item)
            for item in configured_paths
        ]
    source_files = _python_files(paths)
    configured_interpreters = interpreters
    if configured_interpreters is None:
        configured_interpreters = configured_values.get("interpreters")
    if configured_interpreters is not None:
        _validate_paths(configured_interpreters, "interpreters")
    configured = list(configured_interpreters or [sys.executable])
    if interpreters is None and configured_values.get("interpreters"):
        configured = [
            root / Path(item) if not Path(item).is_absolute() else Path(item) for item in configured
        ]
    _validate_runtime_options(timeout_seconds, modules, "imports")
    _validate_runtime_options(timeout_seconds, selected_entries, "entry_points")
    if len(configured) > MAX_ENTRY_POINTS:
        raise RuntimeCheckError("runtime-input-too-large", "interpreters exceeds 1000 items")
    interpreter_results: list[dict[str, Any]] = []
    for configured_value in configured:
        requested = str(configured_value)
        interpreter_status, resolved = _resolve_for_runtime(configured_value)
        if resolved is None:
            record = _interpreter_record(requested, status=interpreter_status)
            for module in modules:
                record["checks"].append(
                    {
                        "kind": "import",
                        "name": module,
                        "status": interpreter_status,
                        "reason": "configured interpreter is unavailable",
                    }
                )
            record["checks"].append(
                {
                    "kind": "compileall",
                    "name": str(root),
                    "status": interpreter_status,
                    "files": len(source_files),
                    "reason": "configured interpreter is unavailable",
                }
            )
            for entry in entries:
                record["checks"].append(
                    {
                        "kind": "entry-point",
                        "group": entry.get("group", ""),
                        "name": entry.get("name", ""),
                        "value": entry.get("value", ""),
                        "location": entry.get("location", {}),
                        "status": interpreter_status,
                        "reason": "configured interpreter is unavailable",
                    }
                )
            interpreter_results.append(record)
            continue
        record = _interpreter_record(requested, status="ready", resolved=resolved)
        with tempfile.TemporaryDirectory(prefix="envlens-runtime-") as cache_directory:
            cache_root = Path(cache_directory)
            env = _runtime_env(root, cache_root)
            if source_files:
                command = _compile_command(resolved, source_files)
                compile_result = _run_check(
                    command,
                    timeout_seconds=timeout_seconds,
                    cwd=root,
                    env=env,
                    kind="compileall",
                    name=str(root),
                )
                compile_result["files"] = len(source_files)
                record["checks"].append(compile_result)
            else:
                record["checks"].append(
                    {
                        "kind": "compileall",
                        "name": str(root),
                        "status": "passed",
                        "files": 0,
                        "reason": "no Python source files were found",
                    }
                )
            for module in modules:
                record["checks"].append(
                    _run_check(
                        [str(resolved), "-c", IMPORT_SCRIPT, module],
                        timeout_seconds=timeout_seconds,
                        cwd=root,
                        env=env,
                        kind="import",
                        name=module,
                    )
                )
            for entry in entries:
                record["checks"].append(
                    _entry_check(
                        entry,
                        resolved=resolved,
                        project_root=root,
                        timeout_seconds=timeout_seconds,
                        execute=execute_entry_points,
                    )
                )
        interpreter_results.append(record)
    checks = [check for record in interpreter_results for check in record["checks"]]
    failures = [
        item
        for item in checks
        if item.get("status")
        in {
            "failed",
            "signal",
            "timeout",
            "missing-import",
            "import-error",
            "entry-point-error",
            "start-error",
            "missing-interpreter",
            "invalid-interpreter",
        }
    ]
    # Dry inspection is a successful, intentionally non-executing action.  It
    # should not turn an otherwise green runtime matrix into an ``unknown``
    # result; unsupported entry-point syntax remains unknown.
    unknown = [item for item in checks if item.get("status") == "unsupported"]
    if failures:
        status = "failed"
    elif unknown or any(record["status"] != "ready" for record in interpreter_results):
        status = "unknown"
    else:
        status = "passed"
    return {
        "schema_version": "envlens.runtime/v1",
        "project": project_info,
        "interpreters": interpreter_results,
        "results": interpreter_results,
        "summary": {
            "status": status,
            "interpreter_count": len(interpreter_results),
            "check_count": len(checks),
            "failure_count": len(failures),
            "unknown_count": len(unknown),
        },
        "policy": {
            "timeout_seconds": timeout_seconds,
            "entry_points_executed": execute_entry_points,
            "network": "not requested by envlens",
        },
    }


def runtime_check(*args: Any, **kwargs: Any) -> dict[str, Any]:
    """Compatibility alias for :func:`run_runtime_checks`."""

    return run_runtime_checks(*args, **kwargs)


def run_smoke(*args: Any, **kwargs: Any) -> dict[str, Any]:
    """Compatibility alias for :func:`run_runtime_checks`."""

    return run_runtime_checks(*args, **kwargs)
