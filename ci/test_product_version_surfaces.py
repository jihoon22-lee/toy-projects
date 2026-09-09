#!/usr/bin/env python3
"""Contract tests pinning every version a product states to its manifest.

A product states its version in more than one place: `ici.toml` is what the
release tooling reads, but a wheel reads `pyproject.toml`, an installed package
reads `__version__`, and a shipped binary prints a compiled-in constant. Nothing
kept those in step, so a release could ship a binary that names a version the
manifest never cut.

The check is deliberately discovery-based rather than a hardcoded list: a new
surface added to any product is pinned the moment it exists, instead of when
someone remembers to register it here.
"""

from __future__ import annotations

import json
import re
import tomllib
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Directories that hold fixtures, vendored samples or build output. A version
# string in there describes something other than the product being released.
EXCLUDED_DIR_NAMES = frozenset(
    {"build", "dist", "examples", "fixtures", "tests", "third_party", "vendor"}
)

PY_VERSION_RE = re.compile(r'^__version__\s*=\s*"([^"]+)"', re.MULTILINE)
CPP_VERSION_RE = re.compile(r'\bkVersion\s*=\s*"([^"]+)"')


def _released_product_names() -> tuple[str, ...]:
    manifest = json.loads((REPO_ROOT / "ci" / "projects.json").read_text(encoding="utf-8"))
    return tuple(
        project["name"]
        for project in manifest["projects"]
        if project.get("release", {}).get("enabled")
    )


def _load_toml(path: Path) -> dict:
    with path.open("rb") as handle:
        return tomllib.load(handle)


def _manifest_version(path: Path) -> str:
    """Read the version an `ici.toml` cuts, failing loudly if it states none.

    Both placements are in use across the portfolio: abilens, diskmap and
    loglens put the identity keys inside `[project]`, while buildscope and
    envlens put them at the top level. Accept either, and reject a file that
    states the version twice without agreeing with itself.
    """

    document = _load_toml(path)
    candidates = []
    if "version" in document:
        candidates.append(document["version"])
    project = document.get("project", {})
    if isinstance(project, dict) and "version" in project:
        candidates.append(project["version"])

    if not candidates:
        raise AssertionError(f"{path} states no version")
    if len(set(candidates)) > 1:
        raise AssertionError(f"{path} states conflicting versions: {candidates!r}")
    return candidates[0]


def _pyproject_version(path: Path) -> str:
    """Read `[project] version`, the one placement PEP 621 defines."""

    return _load_toml(path)["project"]["version"]


def _is_product_source(path: Path, product_root: Path) -> bool:
    relative = path.relative_to(product_root)
    return not EXCLUDED_DIR_NAMES.intersection(relative.parts)


def _version_surfaces(product_root: Path) -> dict[str, str]:
    """Map every place this product states a version to the value it states.

    Keys are repo-relative paths so a failure names the file to edit.
    """

    surfaces: dict[str, str] = {}

    pyproject = product_root / "pyproject.toml"
    if pyproject.is_file():
        key = str(pyproject.relative_to(REPO_ROOT))
        surfaces[key] = _pyproject_version(pyproject)

    for init in sorted(product_root.rglob("__init__.py")):
        if not _is_product_source(init, product_root):
            continue
        found = PY_VERSION_RE.findall(init.read_text(encoding="utf-8"))
        if not found:
            continue
        if len(found) > 1:
            raise AssertionError(f"{init} declares __version__ more than once")
        surfaces[str(init.relative_to(REPO_ROOT))] = found[0]

    for source in sorted(product_root.rglob("*.cpp")) + sorted(product_root.rglob("*.hpp")):
        if not _is_product_source(source, product_root):
            continue
        found = CPP_VERSION_RE.findall(source.read_text(encoding="utf-8"))
        if not found:
            continue
        if len(found) > 1:
            raise AssertionError(f"{source} declares kVersion more than once")
        surfaces[str(source.relative_to(REPO_ROOT))] = found[0]

    return surfaces


class ProductVersionSurfaceTests(unittest.TestCase):
    def test_every_stated_version_matches_the_product_manifest(self) -> None:
        """Whatever a product says its version is, it says the same thing everywhere."""

        for product in _released_product_names():
            product_root = REPO_ROOT / product
            declared = _manifest_version(product_root / "ici.toml")
            for surface, stated in _version_surfaces(product_root).items():
                with self.subTest(product=product, surface=surface):
                    self.assertEqual(
                        stated,
                        declared,
                        f"{surface} states {stated!r} but {product}/ici.toml cut {declared!r}",
                    )

    def test_discovery_finds_the_surfaces_each_product_is_known_to_have(self) -> None:
        """An agreement check over an empty set passes while proving nothing.

        Pinning the located surfaces is what keeps the check above honest: if a
        path moves or the discovery stops matching, this fails instead of
        quietly narrowing to zero.
        """

        expected = {
            "abilens": {"abilens/src/main.cpp"},
            "buildscope": {
                "buildscope/pyproject.toml",
                "buildscope/python/buildscope/__init__.py",
            },
            "diskmap": {"diskmap/src/main.cpp"},
            "envlens": {
                "envlens/pyproject.toml",
                "envlens/src/envlens/__init__.py",
            },
            "loglens": {"loglens/src/main.cpp"},
        }

        self.assertEqual(set(expected), set(_released_product_names()))
        for product, surfaces in expected.items():
            with self.subTest(product=product):
                self.assertEqual(set(_version_surfaces(REPO_ROOT / product)), surfaces)

    def test_every_released_native_cli_can_state_its_version(self) -> None:
        """A binary a user downloads has to be able to answer `--version`.

        Only the C++ CLIs are covered here; the Python entry points get their
        version from the package metadata the wheel already carries.
        """

        for product in ("abilens", "diskmap", "loglens"):
            with self.subTest(product=product):
                main = REPO_ROOT / product / "src" / "main.cpp"
                source = main.read_text(encoding="utf-8")
                self.assertRegex(source, CPP_VERSION_RE, f"{main} has no version constant")
                self.assertIn(
                    '"--version"',
                    source,
                    f"{main} compiles a version it never lets the user read",
                )


if __name__ == "__main__":
    unittest.main()
