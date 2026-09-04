"""Project metadata evidence used by bounded runtime checks."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any

from envlens.project import ProjectError, inspect_pyproject

_PROJECT_LINE_RE = re.compile(r"\bline (?P<line>[0-9]+)\b")


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


def load_project_info(root: Path, pyproject: str | Path | None) -> dict[str, Any]:
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


def project_inspection_check(
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
