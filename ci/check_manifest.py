#!/usr/bin/env python3
"""Validate the toy-projects CI manifest and emit matrix JSON.

The workflow deliberately keeps project/build metadata in one checked-in JSON
file. This script is dependency-free so the discovery job can run before any
toolchain setup, and its path validation keeps manifest-controlled values safe
to pass to later shell steps.
"""

from __future__ import annotations

import json
import os
import re
from pathlib import Path
from typing import Any


MANIFEST_PATH = Path("ci/projects.json")
SAFE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]*$")
SAFE_PATH = re.compile(r"^[A-Za-z0-9._/-]+$")
# Every enabled GUI project gets one leg for each supported major. Keeping this
# in discovery, rather than duplicating entries in projects.json, means a new
# project cannot accidentally enter only half of the GUI matrix.
SUPPORTED_QT_MAJORS = (5, 6)


def _required_path(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value or not SAFE_PATH.fullmatch(value):
        raise ValueError(f"invalid {label}: {value!r}")
    path = Path(value)
    if value.startswith("/") or ".." in path.parts:
        raise ValueError(f"unsafe {label}: {value!r}")
    return value


def discover(
    root: Path = Path("."),
) -> tuple[list[dict[str, str]], list[dict[str, Any]], list[str]]:
    """Validate the manifest and expand every GUI project across Qt majors.

    ``root`` is injectable for the dependency-free unit tests. The workflow
    still calls the default, repository-relative location.
    """

    root = Path(root)
    payload = json.loads((root / MANIFEST_PATH).read_text(encoding="utf-8"))
    if payload.get("schema") != 1:
        raise ValueError("ci/projects.json must declare schema 1")
    entries = payload.get("projects")
    if not isinstance(entries, list) or not entries:
        raise ValueError("ci/projects.json must contain a non-empty projects list")

    names: list[str] = []
    verify_projects: list[dict[str, str]] = []
    gui_projects: list[dict[str, str]] = []
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError("every project manifest entry must be an object")
        name = entry.get("name")
        if not isinstance(name, str) or not SAFE_NAME.fullmatch(name):
            raise ValueError(f"invalid project name: {name!r}")
        if name in names:
            raise ValueError(f"duplicate project name: {name}")
        project_dir = root / name
        if not project_dir.is_dir() or not (project_dir / "ici.toml").is_file():
            raise ValueError(f"{name} must be a directory containing ici.toml")
        if entry.get("verify") is not True:
            raise ValueError(f"{name} must opt into ici verification")

        names.append(name)
        verify_projects.append({"name": name})

        gui = entry.get("gui")
        if not isinstance(gui, dict) or not isinstance(gui.get("enabled"), bool):
            raise ValueError(f"{name} must declare gui.enabled true or false")
        if gui["enabled"] is False:
            continue

        build_system = gui.get("build_system")
        if build_system not in ("cmake", "qmake"):
            raise ValueError(f"{name} has unsupported GUI build system: {build_system!r}")
        descriptor = _required_path(gui.get("build_descriptor"), f"{name}.gui.build_descriptor")
        binary = _required_path(gui.get("binary"), f"{name}.gui.binary")
        smoke_arg = _required_path(gui.get("smoke_arg"), f"{name}.gui.smoke_arg")
        if not (project_dir / descriptor).is_file():
            raise ValueError(f"{name} GUI descriptor does not exist: {descriptor}")
        for qt_major in SUPPORTED_QT_MAJORS:
            gui_projects.append(
                {
                    "name": name,
                    "build_system": build_system,
                    "build_descriptor": descriptor,
                    "gui_binary": binary,
                    "smoke_arg": smoke_arg,
                    "qt_major": qt_major,
                }
            )

    discovered_projects = sorted(
        path.name
        for path in root.iterdir()
        if path.is_dir() and (path / "ici.toml").is_file()
    )
    if sorted(names) != discovered_projects:
        raise ValueError(
            "manifest/project mismatch: "
            f"manifest={sorted(names)!r} discovered={discovered_projects!r}"
        )
    return verify_projects, gui_projects, names


def write_github_outputs(
    verify_projects: list[dict[str, str]],
    gui_projects: list[dict[str, str]],
    names: list[str],
) -> None:
    output_name = os.environ.get("GITHUB_OUTPUT")
    if not output_name:
        return
    with Path(output_name).open("a", encoding="utf-8") as output:
        output.write("projects=" + json.dumps(verify_projects, separators=(",", ":")) + "\n")
        output.write("gui_projects=" + json.dumps(gui_projects, separators=(",", ":")) + "\n")
        output.write("names=" + ",".join(names) + "\n")
        output.write("count=" + str(len(names)) + "\n")
        output.write("gui_count=" + str(len(gui_projects)) + "\n")


def main() -> int:
    try:
        verify_projects, gui_projects, names = discover()
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(str(error)) from error
    write_github_outputs(verify_projects, gui_projects, names)
    print(f"validated {len(names)} project(s), {len(gui_projects)} GUI matrix entrie(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
