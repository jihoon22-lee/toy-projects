#!/usr/bin/env python3
"""Contract tests for the product-agnostic release asset audit.

`check_buildscope_release_assets` has its own suite pinning BuildScope's nine.
These pin the part that suite cannot: that the same audit answers correctly for
a product with a different artifact set, resolved from `ci/projects.json`.
"""

from __future__ import annotations

import hashlib
import json
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_assets import (  # noqa: E402
    check_release_assets,
    expected_asset_names,
    load_release_assets,
)
from release_audit import ReleaseAssetError  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent

# abilens ships a native bundle and no Python packaging, so its set is five
# assets where BuildScope's is nine. Auditing it is the generalization claim.
PRODUCT = "abilens"
DISPLAY = "AbiLens"
VERSION = "0.1.0"
TAG = f"abilens-v{VERSION}"


class ReleaseAssetsTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.dist = self.root / "dist"
        self.dist.mkdir()
        self.release_json = self.root / "release.json"
        self.release = self._write_valid_release()

    def _write_valid_release(self) -> dict[str, object]:
        assets: list[dict[str, object]] = []
        for index, name in enumerate(expected_asset_names(PRODUCT, VERSION, REPO_ROOT)):
            payload = f"AbiLens release fixture {index}: {name}\n".encode()
            (self.dist / name).write_bytes(payload)
            assets.append(
                {
                    "id": 2_000 + index,
                    "name": name,
                    "state": "uploaded",
                    "size": len(payload),
                    "digest": f"sha256:{hashlib.sha256(payload).hexdigest()}",
                }
            )
        release: dict[str, object] = {
            "id": 700,
            "tag_name": TAG,
            "name": f"{DISPLAY} {VERSION}",
            "draft": False,
            "prerelease": False,
            "published_at": "2026-09-09T00:00:00Z",
            "assets": assets,
        }
        self._write(release)
        return release

    def _write(self, release: object) -> None:
        self.release_json.write_text(
            json.dumps(release, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )

    def _check(self) -> None:
        check_release_assets(
            self.release_json, self.dist, PRODUCT, TAG, VERSION, repo_root=REPO_ROOT
        )

    def test_derived_asset_set_is_the_products_own_not_buildscopes(self) -> None:
        names = expected_asset_names(PRODUCT, VERSION, REPO_ROOT)
        self.assertEqual(
            names,
            (
                "abilens-ici-deep.json",
                "abilens-ici-deep.html",
                "abilens-provenance.json",
                f"abilens-{VERSION}-linux-x86_64.tar.gz",
                "SHA256SUMS",
            ),
        )
        self.assertNotIn("abilens.pyz", names)
        self.assertNotIn(f"abilens-{VERSION}-py3-none-any.whl", names)

    def test_accepts_a_release_carrying_exactly_the_declared_assets(self) -> None:
        self._check()

    def test_rejects_a_buildscope_shaped_asset_set_for_this_product(self) -> None:
        """A wheel in a native-only release means the wrong thing was uploaded."""

        extra = f"abilens-{VERSION}-py3-none-any.whl"
        payload = b"wheel\n"
        (self.dist / extra).write_bytes(payload)
        assets = list(self.release["assets"])
        assets.append(
            {
                "id": 2_900,
                "name": extra,
                "state": "uploaded",
                "size": len(payload),
                "digest": f"sha256:{hashlib.sha256(payload).hexdigest()}",
            }
        )
        self.release["assets"] = assets
        self._write(self.release)
        with self.assertRaises(ReleaseAssetError) as caught:
            self._check()
        self.assertIn("count mismatch", str(caught.exception))

    def test_release_name_must_use_this_products_display_name(self) -> None:
        self.release["name"] = f"BuildScope {VERSION}"
        self._write(self.release)
        with self.assertRaises(ReleaseAssetError) as caught:
            self._check()
        self.assertIn(DISPLAY, str(caught.exception))

    def test_tag_prefix_is_derived_from_the_product(self) -> None:
        with self.assertRaises(ReleaseAssetError):
            check_release_assets(
                self.release_json,
                self.dist,
                PRODUCT,
                f"buildscope-v{VERSION}",
                VERSION,
                repo_root=REPO_ROOT,
            )

    def test_digest_mismatch_is_caught_for_this_product_too(self) -> None:
        assets = list(self.release["assets"])
        assets[0]["digest"] = "sha256:" + "0" * 64
        self.release["assets"] = assets
        self._write(self.release)
        with self.assertRaises(ReleaseAssetError) as caught:
            self._check()
        self.assertIn("digest mismatch", str(caught.exception))

    def test_draft_and_final_stages_are_distinguished(self) -> None:
        self.release["draft"] = True
        self.release["published_at"] = None
        self._write(self.release)
        with self.assertRaises(ReleaseAssetError):
            self._check()
        load_release_assets(
            self.release_json, PRODUCT, TAG, VERSION, stage="draft", repo_root=REPO_ROOT
        )

    def test_rejects_a_product_the_manifest_does_not_release(self) -> None:
        with self.assertRaises(ReleaseAssetError) as caught:
            check_release_assets(
                self.release_json, self.dist, "gadget", TAG, VERSION, repo_root=REPO_ROOT
            )
        self.assertIn("not a released product", str(caught.exception))

    def test_every_released_product_derives_a_distinct_nonempty_asset_set(self) -> None:
        manifest = json.loads((REPO_ROOT / "ci" / "projects.json").read_text(encoding="utf-8"))
        seen = {}
        for project in manifest["projects"]:
            if not project.get("release", {}).get("enabled"):
                continue
            name = project["name"]
            names = expected_asset_names(name, VERSION, REPO_ROOT)
            with self.subTest(product=name):
                self.assertIn("SHA256SUMS", names)
                self.assertIn(f"{name}-provenance.json", names)
                self.assertNotIn(names, seen.values())
            seen[name] = names
        self.assertEqual(len(seen), 5)


if __name__ == "__main__":
    unittest.main()
