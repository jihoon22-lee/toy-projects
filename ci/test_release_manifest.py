#!/usr/bin/env python3
"""Contract tests for the product-agnostic SHA256SUMS audit.

BuildScope's suite pins its own eight-entry manifest and pyz sidecar. These pin
what that suite structurally cannot: that a product with a different asset set
and no zipapp is audited against its own manifest, and that the sidecar rule
does not leak across products.
"""

from __future__ import annotations

import hashlib
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_audit import ReleaseAssetError  # noqa: E402
from release_manifest import (  # noqa: E402
    check_release_manifest,
    manifest_asset_names,
)

REPO_ROOT = Path(__file__).resolve().parent.parent
PRODUCT = "abilens"
VERSION = "0.1.0"


class ReleaseManifestTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.dist = Path(self.temp.name) / "dist"
        self.dist.mkdir()
        self._write_valid_manifest()

    def _write_valid_manifest(self) -> None:
        lines = []
        for index, name in enumerate(manifest_asset_names(PRODUCT, VERSION, REPO_ROOT)):
            payload = f"AbiLens manifest fixture {index}: {name}\n".encode()
            (self.dist / name).write_bytes(payload)
            lines.append(f"{hashlib.sha256(payload).hexdigest()}  {name}")
        (self.dist / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")

    def _check(self) -> None:
        check_release_manifest(self.dist, PRODUCT, VERSION, REPO_ROOT)

    def test_covers_the_products_own_assets_excluding_the_manifest_itself(self) -> None:
        names = manifest_asset_names(PRODUCT, VERSION, REPO_ROOT)
        self.assertNotIn("SHA256SUMS", names)
        self.assertEqual(
            names,
            (
                "abilens-ici-deep.json",
                "abilens-ici-deep.html",
                "abilens-provenance.json",
                f"abilens-{VERSION}-linux-x86_64.tar.gz",
            ),
        )

    def test_accepts_a_manifest_matching_the_bytes_on_disk(self) -> None:
        self._check()

    def test_a_product_without_a_zipapp_needs_no_sidecar(self) -> None:
        """Demanding a pyz sidecar here would fail four correct releases."""

        self.assertFalse((self.dist / f"{PRODUCT}.pyz.sha256").exists())
        self._check()

    def test_rejects_a_digest_that_does_not_match_the_file(self) -> None:
        manifest = self.dist / "SHA256SUMS"
        lines = manifest.read_text(encoding="utf-8").splitlines()
        lines[0] = "0" * 64 + lines[0][64:]
        manifest.write_text("\n".join(lines) + "\n", encoding="utf-8")
        with self.assertRaisesRegex(ReleaseAssetError, "digest mismatch"):
            self._check()

    def test_rejects_a_manifest_missing_one_of_the_products_assets(self) -> None:
        manifest = self.dist / "SHA256SUMS"
        lines = manifest.read_text(encoding="utf-8").splitlines()
        manifest.write_text("\n".join(lines[:-1]) + "\n", encoding="utf-8")
        with self.assertRaisesRegex(ReleaseAssetError, "entry count mismatch"):
            self._check()

    def test_rejects_a_version_the_tag_pattern_would_not_allow(self) -> None:
        with self.assertRaises(ReleaseAssetError):
            check_release_manifest(self.dist, PRODUCT, "not-a-version", REPO_ROOT)

    def test_every_released_product_covers_all_but_the_manifest(self) -> None:
        import json

        manifest = json.loads((REPO_ROOT / "ci" / "projects.json").read_text(encoding="utf-8"))
        for project in manifest["projects"]:
            if not project.get("release", {}).get("enabled"):
                continue
            name = project["name"]
            with self.subTest(product=name):
                covered = manifest_asset_names(name, VERSION, REPO_ROOT)
                self.assertNotIn("SHA256SUMS", covered)
                self.assertIn(f"{name}-provenance.json", covered)


if __name__ == "__main__":
    unittest.main()
