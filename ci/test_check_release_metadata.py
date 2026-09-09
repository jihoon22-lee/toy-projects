#!/usr/bin/env python3
"""Contract tests for the generic per-product release metadata gate."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parent))

from check_release_metadata import (  # noqa: E402
    ReleaseMetadataError,
    check_release_metadata,
)

REPO_ROOT = Path(__file__).resolve().parent.parent


def _product(root: Path, name: str, version: str, *, display: str) -> Path:
    product = root / name
    (product / "src").mkdir(parents=True)
    (product / "ici.toml").write_text(
        f'[project]\nname = "{name}"\nversion = "{version}"\n', encoding="utf-8"
    )
    (product / "src" / "main.cpp").write_text(
        f'constexpr const char* kVersion = "{version}";\n', encoding="utf-8"
    )
    (root / "ci").mkdir(exist_ok=True)
    (root / "ci" / "projects.json").write_text(
        '{"schema": 1, "projects": [{"name": "%s", "verify": true,'
        ' "gui": {"enabled": false},'
        ' "release": {"enabled": true, "display_name": "%s",'
        ' "artifacts": {"pyz": false, "wheel": false, "sdist": false,'
        ' "native_bundle": true}}}]}' % (name, display),
        encoding="utf-8",
    )
    (root / "CHANGELOG.md").write_text(
        f"# Changelog\n\n### {display} {version}\n\n- released\n", encoding="utf-8"
    )
    return product


class ReleaseMetadataTests(unittest.TestCase):
    def test_accepts_a_product_whose_surfaces_and_changelog_all_agree(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            _product(root, "widget", "1.2.3", display="Widget")
            surfaces = check_release_metadata(root, "widget", "1.2.3")
            self.assertEqual(surfaces["widget/src/main.cpp"], "1.2.3")

    def test_rejects_a_compiled_constant_that_drifted_from_the_manifest(self) -> None:
        """The released binary would state a version that was never cut."""

        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            product = _product(root, "widget", "1.2.3", display="Widget")
            (product / "src" / "main.cpp").write_text(
                'constexpr const char* kVersion = "1.2.2";\n', encoding="utf-8"
            )
            with self.assertRaises(ReleaseMetadataError) as caught:
                check_release_metadata(root, "widget", "1.2.3")
            self.assertIn("src/main.cpp", str(caught.exception))

    def test_rejects_a_tag_version_the_manifest_never_declared(self) -> None:
        """Tagging widget-v9.9.9 must not publish whatever ici.toml happens to say."""

        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            _product(root, "widget", "1.2.3", display="Widget")
            with self.assertRaises(ReleaseMetadataError):
                check_release_metadata(root, "widget", "9.9.9")

    def test_requires_exactly_one_changelog_heading_for_this_release(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            _product(root, "widget", "1.2.3", display="Widget")
            changelog = root / "CHANGELOG.md"

            changelog.write_text("# Changelog\n\n- nothing\n", encoding="utf-8")
            with self.assertRaises(ReleaseMetadataError):
                check_release_metadata(root, "widget", "1.2.3")

            changelog.write_text(
                "# Changelog\n\n### Widget 1.2.3\n\n### Widget 1.2.3\n", encoding="utf-8"
            )
            with self.assertRaises(ReleaseMetadataError):
                check_release_metadata(root, "widget", "1.2.3")

    def test_another_products_heading_does_not_satisfy_this_release(self) -> None:
        """Five products share one CHANGELOG, so the heading has to name the right one."""

        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            _product(root, "widget", "1.2.3", display="Widget")
            (root / "CHANGELOG.md").write_text(
                "# Changelog\n\n### Gadget 1.2.3\n\n- released\n", encoding="utf-8"
            )
            with self.assertRaises(ReleaseMetadataError):
                check_release_metadata(root, "widget", "1.2.3")

    def test_rejects_a_product_the_manifest_does_not_release(self) -> None:
        with TemporaryDirectory() as tmp:
            root = Path(tmp)
            _product(root, "widget", "1.2.3", display="Widget")
            with self.assertRaises(ReleaseMetadataError):
                check_release_metadata(root, "gadget", "1.2.3")

    def test_every_released_product_passes_against_the_real_tree(self) -> None:
        """The gate has to accept the portfolio as it actually stands today."""

        import json

        manifest = json.loads((REPO_ROOT / "ci" / "projects.json").read_text(encoding="utf-8"))
        for project in manifest["projects"]:
            if not project.get("release", {}).get("enabled"):
                continue
            name = project["name"]
            with self.subTest(product=name):
                import tomllib

                with (REPO_ROOT / name / "ici.toml").open("rb") as handle:
                    document = tomllib.load(handle)
                version = document.get("version") or document["project"]["version"]
                surfaces = check_release_metadata(REPO_ROOT, name, version, require_changelog=False)
                self.assertTrue(surfaces, f"{name} states no version anywhere")


if __name__ == "__main__":
    unittest.main()
