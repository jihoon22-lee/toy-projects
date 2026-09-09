#!/usr/bin/env python3
"""Discover every place a product states its own version.

A product states its version in more than one place: `ici.toml` is what the
release tooling reads, but a wheel reads `pyproject.toml`, an installed package
reads `__version__`, and a shipped binary prints a compiled-in constant.

Discovery is filesystem-based rather than a registered list, so a surface added
to a product is found the moment it exists rather than when someone remembers
to register it.
"""

from __future__ import annotations

import re
import tomllib
from pathlib import Path

# Directories that hold fixtures, vendored samples or build output. A version
# string in there describes something other than the product being released.
EXCLUDED_DIR_NAMES = frozenset(
    {"build", "dist", "examples", "fixtures", "tests", "third_party", "vendor"}
)

PY_VERSION_RE = re.compile(r'^__version__\s*=\s*"([^"]+)"', re.MULTILINE)
CPP_VERSION_RE = re.compile(r'\bkVersion\s*=\s*"([^"]+)"')


class VersionSurfaceError(Exception):
    """A product states its version in a way that cannot be read unambiguously."""


def load_toml(path: Path) -> dict:
    with path.open("rb") as handle:
        return tomllib.load(handle)


def manifest_version(path: Path) -> str:
    """Read the version an `ici.toml` cuts, failing loudly if it states none.

    Both placements are in use across the portfolio: abilens, diskmap and
    loglens put the identity keys inside `[project]`, while buildscope and
    envlens put them at the top level. Accept either, and reject a file that
    states the version twice without agreeing with itself.
    """

    document = load_toml(path)
    candidates = []
    if "version" in document:
        candidates.append(document["version"])
    project = document.get("project", {})
    if isinstance(project, dict) and "version" in project:
        candidates.append(project["version"])

    if not candidates:
        raise VersionSurfaceError(f"{path} states no version")
    if len(set(candidates)) > 1:
        raise VersionSurfaceError(f"{path} states conflicting versions: {candidates!r}")
    return candidates[0]


def pyproject_version(path: Path) -> str:
    """Read `[project] version`, the one placement PEP 621 defines."""

    return load_toml(path)["project"]["version"]


def _is_product_source(path: Path, product_root: Path) -> bool:
    relative = path.relative_to(product_root)
    return not EXCLUDED_DIR_NAMES.intersection(relative.parts)


def _single(found: list[str], path: Path, what: str) -> str:
    if len(found) > 1:
        raise VersionSurfaceError(f"{path} declares {what} more than once")
    return found[0]


def version_surfaces(product_root: Path, repo_root: Path) -> dict[str, str]:
    """Map every place this product states a version to the value it states.

    Keys are repo-relative paths so a failure names the file to edit.
    """

    surfaces: dict[str, str] = {}

    pyproject = product_root / "pyproject.toml"
    if pyproject.is_file():
        surfaces[str(pyproject.relative_to(repo_root))] = pyproject_version(pyproject)

    for init in sorted(product_root.rglob("__init__.py")):
        if not _is_product_source(init, product_root):
            continue
        found = PY_VERSION_RE.findall(init.read_text(encoding="utf-8"))
        if found:
            surfaces[str(init.relative_to(repo_root))] = _single(found, init, "__version__")

    sources = sorted(product_root.rglob("*.cpp")) + sorted(product_root.rglob("*.hpp"))
    for source in sources:
        if not _is_product_source(source, product_root):
            continue
        found = CPP_VERSION_RE.findall(source.read_text(encoding="utf-8"))
        if found:
            surfaces[str(source.relative_to(repo_root))] = _single(found, source, "kVersion")

    return surfaces
