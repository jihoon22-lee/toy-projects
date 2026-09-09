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
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from version_surfaces import (  # noqa: E402
    CPP_VERSION_RE,
    manifest_version,
    version_surfaces,
)

REPO_ROOT = Path(__file__).resolve().parent.parent


def _released_product_names() -> tuple[str, ...]:
    manifest = json.loads((REPO_ROOT / "ci" / "projects.json").read_text(encoding="utf-8"))
    return tuple(
        project["name"]
        for project in manifest["projects"]
        if project.get("release", {}).get("enabled")
    )


def _version_surfaces(product_root: Path) -> dict[str, str]:
    return version_surfaces(product_root, REPO_ROOT)


class ProductVersionSurfaceTests(unittest.TestCase):
    def test_every_stated_version_matches_the_product_manifest(self) -> None:
        """Whatever a product says its version is, it says the same thing everywhere."""

        for product in _released_product_names():
            product_root = REPO_ROOT / product
            declared = manifest_version(product_root / "ici.toml")
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
