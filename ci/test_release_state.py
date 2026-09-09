#!/usr/bin/env python3
"""Contract tests for the product-agnostic release state audit.

BuildScope's suite pins BuildScope's slot behaviour. It structurally cannot
catch a generalization that quietly still hardcodes BuildScope, because every
value it passes is BuildScope's. These audit abilens instead.
"""

from __future__ import annotations

import hashlib
import json
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_state import (  # noqa: E402
    ReleaseStateError,
    check_release_state,
    inspect_release_slot,
    owner_marker_prefix,
    recover_owned_draft,
    resolve_display_name,
)

REPO_ROOT = Path(__file__).resolve().parent.parent

PRODUCT = "abilens"
DISPLAY = "AbiLens"
VERSION = "0.1.0"
TAG = f"{PRODUCT}-v{VERSION}"
TARGET_SHA = "b" * 40
RELEASE_ID = 424242
OWNER_REPO = "jihoon22-lee/toy-projects"
RUN_ID = 987654321
MARKER = f"<!-- {PRODUCT}-release-owner:{OWNER_REPO}:{RUN_ID}:{TARGET_SHA} -->"


class ReleaseStateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def _write(self, name: str, payload: object) -> Path:
        path = self.root / name
        path.write_text(json.dumps(payload) + "\n", encoding="utf-8")
        return path

    def _draft_release(self, **overrides: object) -> dict:
        draft = self._final_release(draft=True, published_at=None, **overrides)
        return draft

    def _final_release(self, **overrides: object) -> dict:
        release = {
            "id": RELEASE_ID,
            "tag_name": TAG,
            "name": f"{DISPLAY} {VERSION}",
            "draft": False,
            "prerelease": False,
            "target_commitish": "main",
            "body": "notes\n",
            "published_at": "2026-09-09T00:00:00Z",
            "assets": [],
        }
        release.update(overrides)
        return release

    def test_display_name_comes_from_the_product_declaration(self) -> None:
        self.assertEqual(resolve_display_name(PRODUCT, REPO_ROOT), DISPLAY)

    def test_marker_namespace_is_per_product(self) -> None:
        """Two products' drafts must not be able to adopt each other."""

        self.assertEqual(owner_marker_prefix(PRODUCT), f"<!-- {PRODUCT}-release-owner:")
        self.assertNotEqual(owner_marker_prefix(PRODUCT), owner_marker_prefix("buildscope"))

    def test_accepts_this_products_final_release(self) -> None:
        path = self._write("release.json", self._final_release())
        self.assertEqual(
            check_release_state(path, PRODUCT, DISPLAY, TAG, VERSION, TARGET_SHA, "final"),
            RELEASE_ID,
        )

    def test_rejects_a_release_named_for_another_product(self) -> None:
        path = self._write("release.json", self._final_release(name=f"BuildScope {VERSION}"))
        with self.assertRaises(ReleaseStateError):
            check_release_state(path, PRODUCT, DISPLAY, TAG, VERSION, TARGET_SHA, "final")

    def test_rejects_another_products_tag_for_this_version(self) -> None:
        path = self._write("release.json", self._final_release())
        with self.assertRaises(ReleaseStateError):
            check_release_state(
                path, PRODUCT, DISPLAY, f"buildscope-v{VERSION}", VERSION, TARGET_SHA, "final"
            )

    def test_empty_slot_is_reported_when_no_release_holds_the_tag(self) -> None:
        path = self._write("pages.json", [[]])
        slot = inspect_release_slot(path, PRODUCT, DISPLAY, TAG, VERSION, TARGET_SHA)
        self.assertEqual(slot.mode, "empty")

    def test_a_pre_existing_draft_is_never_adopted(self) -> None:
        path = self._write("pages.json", [[self._draft_release()]])
        with self.assertRaisesRegex(ReleaseStateError, "private draft"):
            inspect_release_slot(path, PRODUCT, DISPLAY, TAG, VERSION, TARGET_SHA)

    def test_recovers_only_a_draft_carrying_this_products_marker(self) -> None:
        body = f"notes\n\n{MARKER}"
        digest = hashlib.sha256(body.encode("utf-8")).hexdigest()
        draft = self._draft_release(body=body)
        path = self._write("pages.json", [[draft]])
        self.assertEqual(
            recover_owned_draft(
                path, PRODUCT, DISPLAY, TAG, VERSION, TARGET_SHA, MARKER, digest
            ),
            RELEASE_ID,
        )

    def test_refuses_a_draft_carrying_another_products_marker(self) -> None:
        """A buildscope-owned draft must not be recoverable as abilens."""

        foreign = f"<!-- buildscope-release-owner:{OWNER_REPO}:{RUN_ID}:{TARGET_SHA} -->"
        body = f"notes\n\n{foreign}"
        digest = hashlib.sha256(body.encode("utf-8")).hexdigest()
        path = self._write("pages.json", [[self._draft_release(body=body)]])
        with self.assertRaises(ReleaseStateError):
            recover_owned_draft(
                path, PRODUCT, DISPLAY, TAG, VERSION, TARGET_SHA, foreign, digest
            )

    def test_rejects_a_product_the_manifest_does_not_release(self) -> None:
        with self.assertRaisesRegex(ReleaseStateError, "not a released product"):
            resolve_display_name("gadget", REPO_ROOT)


if __name__ == "__main__":
    unittest.main()
