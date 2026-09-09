#!/usr/bin/env python3
"""Contract tests for the per-product release registry."""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parent))

from check_buildscope_release_assets import (  # noqa: E402
    TAG_PREFIX as BUILDSCOPE_TAG_PREFIX,
)
from check_buildscope_release_assets import (  # noqa: E402
    expected_asset_names as buildscope_expected_asset_names,
)
from release_registry import (  # noqa: E402
    ReleaseRegistryError,
    load_release_specs,
    spec_for_tag,
)

REPO_ROOT = Path(__file__).resolve().parent.parent
PRODUCTS = ("abilens", "buildscope", "diskmap", "envlens", "loglens")


def _manifest_with(release: object) -> dict[str, object]:
    return {
        "schema": 1,
        "projects": [
            {"name": "widget", "verify": True, "gui": {"enabled": False}, "release": release}
        ],
    }


class ReleaseRegistryTests(unittest.TestCase):
    def test_generic_names_reproduce_the_published_buildscope_nine(self) -> None:
        """The derived list must equal the one BuildScope 0.5.0 actually shipped.

        This is the whole safety argument for deriving asset names from the
        registry: if the generic rule cannot reproduce a release that is
        already public, it is the wrong rule.
        """

        spec = load_release_specs(REPO_ROOT)["buildscope"]
        for version in ("0.5.0", "1.2.3", "10.20.30"):
            with self.subTest(version=version):
                self.assertEqual(
                    spec.expected_asset_names(version),
                    buildscope_expected_asset_names(version),
                )

    def test_tag_prefix_is_derived_from_the_product_name(self) -> None:
        spec = load_release_specs(REPO_ROOT)["buildscope"]
        self.assertEqual(spec.tag_prefix, BUILDSCOPE_TAG_PREFIX)

    def test_every_product_is_releasable_and_ships_a_build_output(self) -> None:
        specs = load_release_specs(REPO_ROOT)
        self.assertEqual(tuple(sorted(specs)), PRODUCTS)
        for name, spec in sorted(specs.items()):
            with self.subTest(product=name):
                names = spec.expected_asset_names("0.1.0")
                self.assertEqual(names[-1], "SHA256SUMS")
                self.assertEqual(len(set(names)), len(names))
                # Reports alone are not a release.
                self.assertTrue(
                    any(
                        spec.ships(kind)
                        for kind in ("pyz", "wheel", "sdist", "native_bundle")
                    )
                )

    def test_asset_list_follows_what_the_product_can_build(self) -> None:
        specs = load_release_specs(REPO_ROOT)
        # AbiLens has no Python packaging; EnvLens produces no linker output.
        self.assertFalse(specs["abilens"].ships("wheel"))
        self.assertFalse(specs["abilens"].ships("sdist"))
        self.assertFalse(specs["envlens"].ships("native_bundle"))
        self.assertTrue(specs["envlens"].ships("wheel"))
        for name in ("diskmap", "loglens"):
            self.assertTrue(specs[name].ships("native_bundle"))
            self.assertFalse(specs[name].ships("pyz"))

    def test_resolves_a_tag_to_its_product_and_version(self) -> None:
        for name in PRODUCTS:
            with self.subTest(product=name):
                spec, version = spec_for_tag(f"{name}-v1.4.2", REPO_ROOT)
                self.assertEqual(spec.name, name)
                self.assertEqual(version, "1.4.2")

    def test_rejects_a_tag_that_names_no_registered_product(self) -> None:
        for tag in ("widget-v1.0.0", "v1.0.0", "abilens1.0.0", "", "-v1.0.0"):
            with self.subTest(tag=tag):
                with self.assertRaisesRegex(ReleaseRegistryError, "releasable product"):
                    spec_for_tag(tag, REPO_ROOT)

    def test_rejects_malformed_release_blocks(self) -> None:
        full = dict(pyz=False, wheel=True, sdist=True, native_bundle=False)
        cases = (
            ("must be an object", "not-an-object"),
            ("enabled=true", {"display_name": "W", "artifacts": full}),
            (
                "enabled=true",
                {"enabled": False, "display_name": "W", "artifacts": full},
            ),
            ("display_name", {"enabled": True, "display_name": "  ", "artifacts": full}),
            ("display_name", {"enabled": True, "artifacts": full}),
            ("unknown keys", {"enabled": True, "display_name": "W", "artifacts": full, "extra": 1}),
            ("artifacts must be an object", {"enabled": True, "display_name": "W", "artifacts": []}),
            (
                "unknown kinds",
                {"enabled": True, "display_name": "W", "artifacts": dict(full, deb=True)},
            ),
            (
                "must state every kind",
                {"enabled": True, "display_name": "W", "artifacts": {"wheel": True}},
            ),
            (
                "must be a boolean",
                {"enabled": True, "display_name": "W", "artifacts": dict(full, wheel="yes")},
            ),
            (
                "ships no build output",
                {
                    "enabled": True,
                    "display_name": "W",
                    "artifacts": dict(pyz=False, wheel=False, sdist=False, native_bundle=False),
                },
            ),
        )
        for message, release in cases:
            with self.subTest(message=message):
                with TemporaryDirectory() as tmp:
                    root = Path(tmp)
                    (root / "ci").mkdir()
                    (root / "ci/projects.json").write_text(
                        json.dumps(_manifest_with(release)), encoding="utf-8"
                    )
                    with self.assertRaisesRegex(ReleaseRegistryError, message):
                        load_release_specs(root)


if __name__ == "__main__":
    unittest.main()
