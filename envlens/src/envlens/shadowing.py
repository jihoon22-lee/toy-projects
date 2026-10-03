"""Read-only potential import shadowing evidence; never import project code."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Any

MAX_LOCAL_ENTRIES = 10_000


def local_imports(root: Path) -> tuple[list[dict[str, str]], bool]:
    records: list[dict[str, str]] = []
    examined = 0
    limited = False
    for directory in (root, root / "src"):
        try:
            with os.scandir(directory) as entries:
                for entry in entries:
                    examined += 1
                    if examined > MAX_LOCAL_ENTRIES:
                        limited = True
                        break
                    if entry.is_symlink():
                        continue
                    name = entry.name
                    kind = ""
                    if entry.is_file(follow_symlinks=False) and name.endswith(".py"):
                        name, kind = name[:-3], "module"
                    elif entry.is_dir(follow_symlinks=False):
                        initializer = Path(entry.path) / "__init__.py"
                        if initializer.is_file() and not initializer.is_symlink():
                            kind = "package"
                    if kind and name.isidentifier() and name != "__init__":
                        records.append(
                            {"name": name, "path": str(Path(entry.path).absolute()), "kind": kind}
                        )
        except FileNotFoundError:
            continue
        except OSError:
            limited = True
        if limited:
            break
    records.sort(key=lambda item: (item["name"], item["path"]))
    return records, limited


def project_shadowing(project: dict[str, Any], owners: dict[str, set[str]]) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    nested = project.get("project")
    own_name = nested.get("normalized_name", "") if isinstance(nested, dict) else ""
    modules = project.get("local_imports", [])
    if not isinstance(modules, list):
        return issues
    for module in modules[:MAX_LOCAL_ENTRIES]:
        if not isinstance(module, dict) or not isinstance(module.get("name"), str):
            continue
        others = owners.get(module["name"], set()) - {own_name}
        if others:
            issues.append(
                {
                    "kind": "project-shadowing",
                    "name": module["name"],
                    "source": str(module.get("path", "unknown")),
                    "requirement": "",
                    "installed": sorted(others),
                    "certainty": "unknown",
                    "dependency_path": [],
                    "reason": (
                        "local module shares an installed import name; "
                        "actual precedence depends on runtime sys.path"
                    ),
                }
            )
    if project.get("local_imports_limited"):
        issues.append(
            {
                "kind": "shadowing-limit",
                "name": "project",
                "source": "project",
                "requirement": "",
                "installed": [],
                "certainty": "unknown",
                "dependency_path": [],
                "reason": "local module enumeration was incomplete",
            }
        )
    return issues
