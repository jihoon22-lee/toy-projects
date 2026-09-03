"""Bounded compile, import, and project entry-point runtime checks."""

from __future__ import annotations

import os
import re
import sys
import tempfile
from collections.abc import Sequence
from pathlib import Path
from typing import Any

from envlens.probe import ProbeError, _run_bounded
from envlens.project import ProjectError, inspect_pyproject
from envlens.runtime_execution import (
    PreparedRuntime as _PreparedRuntime,
)
from envlens.runtime_execution import (
    interpreter_record as _interpreter_record,
)
from envlens.runtime_execution import (
    missing_entry_check as _missing_entry_check,
)
from envlens.runtime_execution import (
    resolve_for_runtime as _resolve_for_runtime,
)
from envlens.runtime_execution import (
    select_entry_points as _select_entry_points,
)
from envlens.runtime_execution import (
    unavailable_record as _unavailable_record,
)
from envlens.runtime_files import collect_python_files
from envlens.runtime_process import (
    bounded_text,
    classify_process,
    failed_process_status,
)
from envlens.runtime_types import RuntimeCheckError as RuntimeCheckError

MAX_SOURCE_FILES = 10_000
MAX_SOURCE_BYTES = 64 * 1024 * 1024
MAX_IMPORTS = 1_000
MAX_ENTRY_POINTS = 1_000
MAX_PATHS = 1_000
MAX_PATH_LENGTH = 65_536
MAX_SOURCE_ENTRIES = 100_000
MAX_OUTPUT_CHARS = 65_536
MAX_COMPILE_COMMAND_CHARS = 64 * 1024
_PROJECT_LINE_RE = re.compile(r"\bline (?P<line>[0-9]+)\b")
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


def _bounded_text(data: bytes) -> str:
    return bounded_text(data, MAX_OUTPUT_CHARS)


def _failed_process_status(return_code: int, stderr: str, name: str) -> dict[str, Any]:
    return failed_process_status(return_code, stderr, name)


def _classify_process(
    *,
    stdout: bytes,
    stderr: bytes,
    return_code: int,
    timeout_seconds: int,
    kind: str,
    name: str,
) -> dict[str, Any]:
    return classify_process(
        stdout=stdout,
        stderr=stderr,
        return_code=return_code,
        timeout_seconds=timeout_seconds,
        kind=kind,
        name=name,
        max_output_chars=MAX_OUTPUT_CHARS,
        text_renderer=_bounded_text,
        status_renderer=_failed_process_status,
    )


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
    return collect_python_files(
        paths,
        max_files=MAX_SOURCE_FILES,
        max_bytes=MAX_SOURCE_BYTES,
        max_entries=MAX_SOURCE_ENTRIES,
    )


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


def _entry_check(
    entry: dict[str, Any],
    *,
    resolved: Path,
    project_root: Path,
    env: dict[str, str],
    timeout_seconds: int,
    execute: bool,
) -> dict[str, Any]:
    if entry.get("status") == "missing":
        return _missing_entry_check(entry)
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


def _empty_project_info(path: Path) -> dict[str, Any]:
    return {
        "schema_version": "envlens.project/v1",
        "path": str(path),
        "project": {
            "name": "",
            "normalized_name": "",
            "version": "",
            "requires_python": "",
            "dependencies": [],
        },
        "entry_points": [],
        "warnings": [
            "pyproject.toml was not found; project metadata inspection was skipped",
        ],
    }


def _project_error_location(path: Path, message: str) -> dict[str, Any]:
    match = _PROJECT_LINE_RE.search(message)
    line = int(match.group("line")) if match is not None else 0
    return {"path": str(path), "line": line}


def _default_project_file_is_absent(path: Path) -> bool:
    try:
        path.lstat()
    except FileNotFoundError:
        return True
    except OSError:
        # Let inspect_pyproject preserve permission and other filesystem
        # failures as explicit project-inspection evidence.
        return False
    return False


def _project_info(root: Path, pyproject: str | Path | None) -> dict[str, Any]:
    path = Path(pyproject) if pyproject is not None else root / "pyproject.toml"
    if pyproject is None and _default_project_file_is_absent(path):
        return _empty_project_info(path)
    try:
        return inspect_pyproject(path)
    except ProjectError as error:
        location = _project_error_location(path, error.message)
        return {
            "schema_version": "envlens.project/v1",
            "path": str(path),
            "project": {
                "name": "",
                "normalized_name": "",
                "version": "",
                "requires_python": "",
                "dependencies": [],
            },
            "entry_points": [],
            "warnings": [f"entry-point inspection unavailable: {error}"],
            "inspection_error": {
                "code": error.code,
                "message": error.message,
                "location": location,
            },
        }


def _project_inspection_check(
    project_info: dict[str, Any], default_path: Path
) -> dict[str, Any] | None:
    raw_error = project_info.get("inspection_error")
    if not isinstance(raw_error, dict):
        return None
    raw_location = raw_error.get("location")
    location = (
        dict(raw_location)
        if isinstance(raw_location, dict)
        else {"path": str(project_info.get("path", default_path)), "line": 0}
    )
    return {
        "kind": "project-inspection",
        "name": str(project_info.get("path", default_path)),
        "status": "failed",
        "error_code": str(raw_error.get("code", "project-inspection-failed")),
        "location": location,
        "reason": str(raw_error.get("message", "project metadata inspection failed")),
    }


def _configured_list(
    explicit: Sequence[Any] | None,
    configuration: dict[str, Any],
    key: str,
) -> tuple[list[Any] | None, bool]:
    if explicit is not None:
        return list(explicit), False
    configured = configuration.get(key)
    if configured is None:
        return None, False
    if not isinstance(configured, list):
        raise RuntimeCheckError("invalid-runtime-config", f"{key} must be a list")
    return list(configured), True


def _prepare_runtime(
    project_root: str | Path,
    *,
    interpreters: Sequence[str | Path] | None,
    imports: Sequence[str] | None,
    compile_paths: Sequence[str | Path] | None,
    entry_points: Sequence[str] | None,
    pyproject: str | Path | None,
    timeout_seconds: int,
) -> _PreparedRuntime:
    root = Path(project_root)
    if not root.is_dir():
        raise RuntimeCheckError("project-root-failed", "project root must be a directory")
    project_info = _project_info(root, pyproject)
    raw_configuration = project_info.get("configuration", {})
    configuration = raw_configuration if isinstance(raw_configuration, dict) else {}

    selected_entries, _configured_entries = _configured_list(
        entry_points, configuration, "entry_points"
    )
    project_entries = project_info.get("entry_points", [])
    if not isinstance(project_entries, list):
        raise RuntimeCheckError("invalid-runtime-config", "project entry_points must be a list")
    project_path = project_info.get("path")
    location_path = project_path if isinstance(project_path, (str, Path)) else None
    entries = _select_entry_points(
        [dict(item) for item in project_entries if isinstance(item, dict)],
        selected_entries,
        location_path=location_path,
    )

    raw_modules, _configured_modules = _configured_list(imports, configuration, "imports")
    modules = [] if raw_modules is None else raw_modules
    if any(not isinstance(module, str) or not MODULE_RE.fullmatch(module) for module in modules):
        raise RuntimeCheckError("invalid-import", "imports contain an invalid module name")

    raw_paths, _configured_paths_used = _configured_list(
        compile_paths, configuration, "compile_paths"
    )
    paths = [root]
    if raw_paths is not None:
        _validate_paths(raw_paths, "compile_paths")
        paths = [
            Path(item) if Path(item).is_absolute() else root / Path(item) for item in raw_paths
        ]

    raw_interpreters, from_configuration = _configured_list(
        interpreters, configuration, "interpreters"
    )
    configured: list[str | Path] = raw_interpreters or [sys.executable]
    _validate_paths(configured, "interpreters")
    if from_configuration:
        configured = [
            root / Path(item) if not Path(item).is_absolute() else Path(item) for item in configured
        ]
    _validate_runtime_options(timeout_seconds, modules, "imports")
    _validate_runtime_options(timeout_seconds, selected_entries, "entry_points")
    if len(configured) > MAX_ENTRY_POINTS:
        raise RuntimeCheckError("runtime-input-too-large", "interpreters exceeds 1000 items")
    return _PreparedRuntime(
        root=root,
        project_info=project_info,
        entries=entries,
        modules=modules,
        source_files=_python_files(paths),
        interpreters=configured,
        project_check=_project_inspection_check(project_info, root / "pyproject.toml"),
    )


def _compile_check(
    resolved: Path,
    prepared: _PreparedRuntime,
    env: dict[str, str],
    timeout_seconds: int,
) -> dict[str, Any]:
    if not prepared.source_files:
        return {
            "kind": "compileall",
            "name": str(prepared.root),
            "status": "passed",
            "files": 0,
            "reason": "no Python source files were found",
        }
    result = _run_check(
        _compile_command(resolved, prepared.source_files),
        timeout_seconds=timeout_seconds,
        cwd=prepared.root,
        env=env,
        kind="compileall",
        name=str(prepared.root),
    )
    result["files"] = len(prepared.source_files)
    return result


def _ready_record(
    requested: str,
    resolved: Path,
    prepared: _PreparedRuntime,
    timeout_seconds: int,
    execute_entry_points: bool,
) -> dict[str, Any]:
    record = _interpreter_record(requested, status="ready", resolved=resolved)
    with tempfile.TemporaryDirectory(prefix="envlens-runtime-") as cache_directory:
        env = _runtime_env(prepared.root, Path(cache_directory))
        record["checks"].append(_compile_check(resolved, prepared, env, timeout_seconds))
        record["checks"].extend(
            _run_check(
                [str(resolved), "-c", IMPORT_SCRIPT, module],
                timeout_seconds=timeout_seconds,
                cwd=prepared.root,
                env=env,
                kind="import",
                name=module,
            )
            for module in prepared.modules
        )
        record["checks"].extend(
            _entry_check(
                entry,
                resolved=resolved,
                project_root=prepared.root,
                env=env,
                timeout_seconds=timeout_seconds,
                execute=execute_entry_points,
            )
            for entry in prepared.entries
        )
        if prepared.project_check is not None:
            record["checks"].append(dict(prepared.project_check))
    return record


_FAILURE_STATUSES = {
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


def _runtime_summary(interpreters: list[dict[str, Any]]) -> dict[str, Any]:
    checks = [check for record in interpreters for check in record["checks"]]
    failures = [item for item in checks if item.get("status") in _FAILURE_STATUSES]
    unknown = [item for item in checks if item.get("status") == "unsupported"]
    if failures:
        status = "failed"
    elif unknown or any(record["status"] != "ready" for record in interpreters):
        status = "unknown"
    else:
        status = "passed"
    return {
        "status": status,
        "interpreter_count": len(interpreters),
        "check_count": len(checks),
        "failure_count": len(failures),
        "unknown_count": len(unknown),
    }


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

    if not isinstance(execute_entry_points, bool):
        raise RuntimeCheckError("invalid-execute-policy", "execute_entry_points must be boolean")
    _validate_runtime_options(timeout_seconds, imports, "imports")
    _validate_runtime_options(timeout_seconds, entry_points, "entry_points")
    _validate_paths(compile_paths, "compile_paths")
    _validate_paths(interpreters, "interpreters")
    prepared = _prepare_runtime(
        project_root,
        interpreters=interpreters,
        imports=imports,
        compile_paths=compile_paths,
        entry_points=entry_points,
        pyproject=pyproject,
        timeout_seconds=timeout_seconds,
    )
    interpreter_results: list[dict[str, Any]] = []
    for configured_value in prepared.interpreters:
        requested = str(configured_value)
        interpreter_status, resolved = _resolve_for_runtime(configured_value)
        if resolved is None:
            interpreter_results.append(_unavailable_record(requested, interpreter_status, prepared))
            continue
        interpreter_results.append(
            _ready_record(
                requested,
                resolved,
                prepared,
                timeout_seconds,
                execute_entry_points,
            )
        )
    return {
        "schema_version": "envlens.runtime/v1",
        "project": prepared.project_info,
        "interpreters": interpreter_results,
        "results": interpreter_results,
        "summary": _runtime_summary(interpreter_results),
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
