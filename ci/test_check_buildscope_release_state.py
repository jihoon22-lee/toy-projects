"""Tests for BuildScope GitHub Release slot and state validation."""

from __future__ import annotations

import hashlib
import json
import os
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

CI_DIR = Path(__file__).resolve().parent
if str(CI_DIR) not in sys.path:
    sys.path.insert(0, str(CI_DIR))

from check_buildscope_release_state import (
    MAX_RELEASE_ID,
    MAX_RELEASE_PAGES,
    MAX_RELEASES_PER_PAGE,
    MAX_TAG_BYTES,
    BuildScopeReleaseStateError,
    ReleaseSlot,
    check_release_state,
    inspect_release_slot,
    main,
    recover_owned_draft,
)

VERSION = "0.5.0"
TAG = f"buildscope-v{VERSION}"
TARGET_SHA = "a" * 40
RELEASE_ID = 123456
ASSET_COUNT = 9
OWNER_REPO = "openai/buildscope"
RUN_ID = 987654321


class BuildScopeReleaseStateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.release_json = self.root / "release.json"
        self.pages_json = self.root / "release-pages.json"

    def _release(
        self,
        *,
        draft: bool = False,
        release_id: object = RELEASE_ID,
        published_at: object = "2026-09-02T00:00:00Z",
        assets: object | None = None,
        body: object = "release notes",
    ) -> dict[str, object]:
        if assets is None:
            assets = [{} for _ in range(ASSET_COUNT)]
        return {
            "id": release_id,
            "tag_name": TAG,
            "name": f"BuildScope {VERSION}",
            "target_commitish": TARGET_SHA,
            "draft": draft,
            "prerelease": False,
            "published_at": None if draft else published_at,
            "assets": assets,
            "body": body,
        }

    def _write(self, path: Path, value: object) -> None:
        path.write_text(json.dumps(value, sort_keys=True) + "\n", encoding="utf-8")

    def _state(
        self,
        release: dict[str, object],
        *,
        stage: str,
        expected_release_id: int | None = None,
        expected_asset_count: int | None = None,
        expected_owner_marker: str | None = None,
        expected_body_sha256: str | None = None,
    ) -> int:
        self._write(self.release_json, release)
        return check_release_state(
            self.release_json,
            TAG,
            VERSION,
            TARGET_SHA,
            stage,
            expected_release_id=expected_release_id,
            expected_asset_count=expected_asset_count,
            expected_owner_marker=expected_owner_marker,
            expected_body_sha256=expected_body_sha256,
        )

    def _owner_marker(
        self,
        *,
        owner_repo: str = OWNER_REPO,
        run_id: int = RUN_ID,
        target_sha: str = TARGET_SHA,
    ) -> str:
        return f"<!-- buildscope-release-owner:{owner_repo}:{run_id}:{target_sha} -->"

    def _recover(
        self,
        release: dict[str, object],
        *,
        owner_marker: str | None = None,
    ) -> int:
        self._write(self.pages_json, [[release]])
        return recover_owned_draft(
            self.pages_json,
            TAG,
            VERSION,
            TARGET_SHA,
            self._owner_marker() if owner_marker is None else owner_marker,
        )

    def test_empty_and_matching_final_release_slots_succeed(self) -> None:
        self._write(self.pages_json, [])
        self.assertEqual(
            inspect_release_slot(self.pages_json, TAG, VERSION, TARGET_SHA),
            ReleaseSlot(mode="empty", release_id=None),
        )

        self._write(self.pages_json, [[self._release()]])
        self.assertEqual(
            inspect_release_slot(self.pages_json, TAG, VERSION, TARGET_SHA),
            ReleaseSlot(mode="final", release_id=RELEASE_ID),
        )

    def test_draft_and_final_single_release_states_succeed(self) -> None:
        self.assertEqual(
            self._state(
                self._release(draft=True),
                stage="draft",
                expected_release_id=RELEASE_ID,
                expected_asset_count=ASSET_COUNT,
            ),
            RELEASE_ID,
        )
        self.assertEqual(
            self._state(
                self._release(),
                stage="final",
                expected_release_id=RELEASE_ID,
                expected_asset_count=ASSET_COUNT,
            ),
            RELEASE_ID,
        )

    def test_rejects_existing_draft_and_duplicate_matching_slots(self) -> None:
        self._write(self.pages_json, [[self._release(draft=True)]])
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "private draft"):
            inspect_release_slot(self.pages_json, TAG, VERSION, TARGET_SHA)

        self._write(self.pages_json, [[self._release()], [self._release()]])
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "contains 2 entries"):
            inspect_release_slot(self.pages_json, TAG, VERSION, TARGET_SHA)

    def test_rejects_malformed_or_oversized_release_pages(self) -> None:
        malformed_pages: tuple[object, ...] = (
            {},
            [{}],
            [[None]],
        )
        for pages in malformed_pages:
            with self.subTest(pages=pages):
                self._write(self.pages_json, pages)
                with self.assertRaises(BuildScopeReleaseStateError):
                    inspect_release_slot(self.pages_json, TAG, VERSION, TARGET_SHA)

        self._write(self.pages_json, [[] for _ in range(MAX_RELEASE_PAGES + 1)])
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "invalid pages"):
            inspect_release_slot(self.pages_json, TAG, VERSION, TARGET_SHA)

        self._write(
            self.pages_json,
            [[{} for _ in range(MAX_RELEASES_PER_PAGE + 1)]],
        )
        with self.assertRaisesRegex(
            BuildScopeReleaseStateError, "invalid shape or size"
        ):
            inspect_release_slot(self.pages_json, TAG, VERSION, TARGET_SHA)

    def test_rejects_invalid_input_tag_version_and_target(self) -> None:
        self._write(self.pages_json, [])
        cases = (
            ("wrong-tag", VERSION, TARGET_SHA),
            (TAG, "0.5", TARGET_SHA),
            (TAG, VERSION, "A" * 40),
            (TAG, VERSION, "a" * 39),
        )
        for tag, version, target_sha in cases:
            with (
                self.subTest(tag=tag, version=version, target_sha=target_sha),
                self.assertRaises(BuildScopeReleaseStateError),
            ):
                inspect_release_slot(self.pages_json, tag, version, target_sha)

        self._write(self.pages_json, [[{"tag_name": "x" * (MAX_TAG_BYTES + 1)}]])
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "invalid tag"):
            inspect_release_slot(self.pages_json, TAG, VERSION, TARGET_SHA)

    def test_rejects_invalid_release_ids_and_expected_id_mismatch(self) -> None:
        invalid_ids: tuple[object, ...] = (0, -1, True, MAX_RELEASE_ID + 1)
        for release_id in invalid_ids:
            with (
                self.subTest(release_id=release_id),
                self.assertRaisesRegex(BuildScopeReleaseStateError, "release id"),
            ):
                self._state(self._release(release_id=release_id), stage="final")

        with self.assertRaisesRegex(BuildScopeReleaseStateError, "release id mismatch"):
            self._state(
                self._release(), stage="final", expected_release_id=RELEASE_ID + 1
            )

    def test_accepts_arbitrary_or_missing_api_target_commitish(self) -> None:
        release = self._release()
        release["target_commitish"] = "refs/heads/release-candidate"
        self.assertEqual(self._state(release, stage="final"), RELEASE_ID)

        release = self._release()
        del release["target_commitish"]
        self.assertEqual(self._state(release, stage="final"), RELEASE_ID)

        release = self._release()
        del release["target_commitish"]
        self._write(self.pages_json, [[release]])
        self.assertEqual(
            inspect_release_slot(self.pages_json, TAG, VERSION, TARGET_SHA),
            ReleaseSlot(mode="final", release_id=RELEASE_ID),
        )

    def test_state_requires_the_exact_release_body_digest(self) -> None:
        body = "exact release notes\n"
        digest = hashlib.sha256(body.encode("utf-8")).hexdigest()
        release = self._release(body=body)
        self.assertEqual(
            self._state(release, stage="final", expected_body_sha256=digest),
            RELEASE_ID,
        )

        changed = self._release(body=body + "changed")
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "body SHA-256"):
            self._state(changed, stage="final", expected_body_sha256=digest)

        with self.assertRaisesRegex(BuildScopeReleaseStateError, "lowercase"):
            self._state(release, stage="final", expected_body_sha256="A" * 64)

    def test_rejects_state_name_published_and_asset_count_mismatches(
        self,
    ) -> None:
        state_cases = (
            ("draft", False, "draft mismatch"),
            ("final", True, "draft mismatch"),
        )
        for stage, draft, message in state_cases:
            with (
                self.subTest(stage=stage),
                self.assertRaisesRegex(BuildScopeReleaseStateError, message),
            ):
                self._state(self._release(draft=draft), stage=stage)

        release = self._release()
        release["name"] = "BuildScope nightly"
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "name"):
            self._state(release, stage="final")

        release = self._release()
        release["published_at"] = None
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "published_at"):
            self._state(release, stage="final")

        release = self._release(draft=True)
        release["published_at"] = "2026-09-02T00:00:00Z"
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "published_at"):
            self._state(release, stage="draft")

        asset_cases: tuple[list[dict[str, object]], ...] = (
            [{} for _ in range(ASSET_COUNT - 1)],
            [{} for _ in range(ASSET_COUNT + 1)],
        )
        for assets in asset_cases:
            with (
                self.subTest(asset_count=len(assets)),
                self.assertRaisesRegex(BuildScopeReleaseStateError, "asset count"),
            ):
                self._state(
                    self._release(assets=assets),
                    stage="final",
                    expected_asset_count=ASSET_COUNT,
                )

    def test_recovers_one_exact_current_run_owned_zero_asset_draft(self) -> None:
        marker = self._owner_marker()
        release = self._release(
            draft=True,
            assets=[],
            body=f"release notes\n\n{marker}",
        )
        self.assertEqual(self._recover(release), RELEASE_ID)

    def test_recovery_accepts_missing_or_arbitrary_api_target_commitish(self) -> None:
        marker = self._owner_marker()
        for target_commitish in (None, "refs/heads/main"):
            with self.subTest(target_commitish=target_commitish):
                release = self._release(
                    draft=True,
                    assets=[],
                    body=marker,
                )
                if target_commitish is None:
                    del release["target_commitish"]
                else:
                    release["target_commitish"] = target_commitish
                self.assertEqual(self._recover(release), RELEASE_ID)

    def test_recovery_rejects_duplicate_or_missing_matching_releases(self) -> None:
        owned = self._release(
            draft=True,
            assets=[],
            body=self._owner_marker(),
        )
        self._write(self.pages_json, [[owned], [owned.copy()]])
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "contains 2 entries"):
            recover_owned_draft(
                self.pages_json,
                TAG,
                VERSION,
                TARGET_SHA,
                self._owner_marker(),
            )

        self._write(self.pages_json, [[]])
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "no private draft"):
            recover_owned_draft(
                self.pages_json,
                TAG,
                VERSION,
                TARGET_SHA,
                self._owner_marker(),
            )

    def test_recovery_rejects_wrong_state_name_assets_and_id(self) -> None:
        marker = self._owner_marker()
        published_release = self._release(draft=True, assets=[], body=marker)
        published_release["published_at"] = "2026-09-02T00:00:00Z"
        invalid_releases: tuple[tuple[dict[str, object], str], ...] = (
            (self._release(body=marker), "draft mismatch"),
            (
                self._release(draft=True, assets=[], body=marker, release_id=0),
                "release id",
            ),
            (
                self._release(draft=True, assets=[{}], body=marker),
                "zero assets",
            ),
            (
                published_release,
                "published_at",
            ),
        )
        for release, message in invalid_releases:
            with (
                self.subTest(release=release, message=message),
                self.assertRaisesRegex(BuildScopeReleaseStateError, message),
            ):
                self._recover(release)

        release = self._release(draft=True, assets=[], body=marker)
        release["name"] = "BuildScope nightly"
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "name mismatch"):
            self._recover(release)

    def test_recovery_requires_one_exact_terminal_owner_marker(self) -> None:
        expected = self._owner_marker()
        invalid_bodies = (
            "release notes",
            f"{expected}\n",
            f"{expected} trailing text",
            f"<!-- buildscope-release-owner:other/repo:{RUN_ID}:{TARGET_SHA} -->",
            f"<!-- buildscope-release-owner:{OWNER_REPO}:{RUN_ID + 1}:{TARGET_SHA} -->",
            f"<!-- buildscope-release-owner:{OWNER_REPO}:{RUN_ID}:{'b' * 40} -->",
            f"{expected}\n{expected}",
            f"<!-- buildscope-release-owner:{OWNER_REPO}:0:{TARGET_SHA} -->",
            f"<!-- buildscope-release-owner:{OWNER_REPO}:{RUN_ID}:{'A' * 40} -->",
        )
        for body in invalid_bodies:
            with (
                self.subTest(body=body),
                self.assertRaisesRegex(BuildScopeReleaseStateError, "owner marker"),
            ):
                self._recover(self._release(draft=True, assets=[], body=body))

    def test_recovery_rejects_invalid_owner_marker_inputs(self) -> None:
        self._write(
            self.pages_json,
            [
                [
                    self._release(
                        draft=True,
                        assets=[],
                        body=self._owner_marker(),
                    )
                ]
            ],
        )
        invalid_markers = (
            "owner/repo",
            f"<!-- buildscope-release-owner:owner:{RUN_ID}:{TARGET_SHA} -->",
            f"<!-- buildscope-release-owner:owner/repo/extra:{RUN_ID}:{TARGET_SHA} -->",
            f"<!-- buildscope-release-owner:owner repo:{RUN_ID}:{TARGET_SHA} -->",
            f"<!-- buildscope-release-owner:é/repo:{RUN_ID}:{TARGET_SHA} -->",
            f"<!-- buildscope-release-owner:{OWNER_REPO}:0:{TARGET_SHA} -->",
            f"<!-- buildscope-release-owner:{OWNER_REPO}:{MAX_RELEASE_ID + 1}:{TARGET_SHA} -->",
            f"<!-- buildscope-release-owner:{OWNER_REPO}:{RUN_ID}:{'A' * 40} -->",
            f"<!-- buildscope-release-owner:{OWNER_REPO}:{RUN_ID}:{'b' * 40} -->",
        )
        for owner_marker in invalid_markers:
            with (
                self.subTest(owner_marker=owner_marker),
                self.assertRaisesRegex(BuildScopeReleaseStateError, "owner marker"),
            ):
                recover_owned_draft(
                    self.pages_json,
                    TAG,
                    VERSION,
                    TARGET_SHA,
                    owner_marker,
                )

    def test_rejects_duplicate_json_keys_and_huge_integers(self) -> None:
        duplicate = (
            '{"id": 123, "id": 124, "tag_name": "buildscope-v0.5.0",'
            ' "name": "BuildScope 0.5.0", "target_commitish": "' + TARGET_SHA + '"}'
        )
        self.release_json.write_text(duplicate, encoding="utf-8")
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "not valid JSON"):
            check_release_state(self.release_json, TAG, VERSION, TARGET_SHA, "final")

        for invalid_number in ("NaN", "Infinity", "-Infinity", "1e999"):
            with self.subTest(invalid_number=invalid_number):
                self.release_json.write_text(
                    '{"id": 123, "unexpected": ' + invalid_number + "}",
                    encoding="utf-8",
                )
                with self.assertRaisesRegex(
                    BuildScopeReleaseStateError, "not valid JSON"
                ):
                    check_release_state(
                        self.release_json,
                        TAG,
                        VERSION,
                        TARGET_SHA,
                        "final",
                    )

        self.release_json.write_text(
            '{"id": 123456789012345678901, "tag_name": "buildscope-v0.5.0"}',
            encoding="utf-8",
        )
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "not valid JSON"):
            check_release_state(self.release_json, TAG, VERSION, TARGET_SHA, "final")

    def test_rejects_symlink_release_metadata(self) -> None:
        target = self.root / "release-target.json"
        self._write(target, self._release())
        try:
            self.release_json.symlink_to(target)
        except OSError as error:
            self.skipTest(f"symlink creation is unavailable: {error}")
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "safely"):
            check_release_state(self.release_json, TAG, VERSION, TARGET_SHA, "final")

    def test_rejects_fifo_release_metadata(self) -> None:
        fifo = self.root / "release-fifo.json"
        if not hasattr(os, "mkfifo"):
            self.skipTest("FIFO creation is unavailable")
        try:
            os.mkfifo(fifo, 0o600)
        except OSError as error:
            self.skipTest(f"FIFO creation is unavailable: {error}")
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "regular file"):
            check_release_state(fifo, TAG, VERSION, TARGET_SHA, "final")

    def test_rejects_prerelease_and_invalid_stage(self) -> None:
        release = self._release()
        release["prerelease"] = True
        with self.assertRaisesRegex(BuildScopeReleaseStateError, "prerelease"):
            self._state(release, stage="final")

        with self.assertRaisesRegex(
            BuildScopeReleaseStateError, "invalid release stage"
        ):
            self._state(self._release(), stage="candidate")

    def test_cli_emits_slot_json_and_state_id(self) -> None:
        self._write(self.pages_json, [])
        stdout = StringIO()
        with redirect_stdout(stdout):
            self.assertEqual(
                main(["slot", str(self.pages_json), TAG, VERSION, TARGET_SHA]),
                0,
            )
        self.assertEqual(stdout.getvalue(), '{"mode":"empty","release_id":null}\n')

        self._write(self.release_json, self._release())
        stdout = StringIO()
        with redirect_stdout(stdout):
            self.assertEqual(
                main(
                    [
                        "state",
                        str(self.release_json),
                        TAG,
                        VERSION,
                        TARGET_SHA,
                        "--stage",
                        "final",
                        "--expected-release-id",
                        str(RELEASE_ID),
                        "--expected-asset-count",
                        str(ASSET_COUNT),
                    ]
                ),
                0,
            )
        self.assertEqual(stdout.getvalue(), f"{RELEASE_ID}\n")

        self._write(
            self.release_json,
            self._release(body=f"release notes\n{self._owner_marker()}"),
        )
        exact_body = f"release notes\n{self._owner_marker()}"
        exact_body_sha256 = hashlib.sha256(exact_body.encode("utf-8")).hexdigest()
        stdout = StringIO()
        with redirect_stdout(stdout):
            self.assertEqual(
                main(
                    [
                        "state",
                        str(self.release_json),
                        TAG,
                        VERSION,
                        TARGET_SHA,
                        "--stage",
                        "final",
                        "--expected-owner-marker",
                        self._owner_marker(),
                        "--expected-body-sha256",
                        exact_body_sha256,
                    ]
                ),
                0,
            )
        self.assertEqual(stdout.getvalue(), f"{RELEASE_ID}\n")

        self._write(
            self.pages_json,
            [
                [
                    self._release(
                        draft=True,
                        assets=[],
                        body=self._owner_marker(),
                    )
                ]
            ],
        )
        stdout = StringIO()
        with redirect_stdout(stdout):
            self.assertEqual(
                main(
                    [
                        "recover-owned-draft",
                        str(self.pages_json),
                        TAG,
                        VERSION,
                        TARGET_SHA,
                        self._owner_marker(),
                    ]
                ),
                0,
            )
        self.assertEqual(stdout.getvalue(), f"{RELEASE_ID}\n")

    def test_cli_reports_validation_errors_with_nonzero_status(self) -> None:
        self._write(self.pages_json, [[self._release(draft=True)]])
        stderr = StringIO()
        with redirect_stderr(stderr), self.assertRaises(SystemExit) as raised:
            main(["slot", str(self.pages_json), TAG, VERSION, TARGET_SHA])
        self.assertEqual(raised.exception.code, 1)
        self.assertIn("BuildScope release state audit failed", stderr.getvalue())

        self._write(self.release_json, self._release())
        stderr = StringIO()
        with redirect_stderr(stderr), self.assertRaises(SystemExit) as raised:
            main(
                [
                    "state",
                    str(self.release_json),
                    TAG,
                    VERSION,
                    TARGET_SHA,
                    "--stage",
                    "final",
                    "--expected-release-id",
                    str(RELEASE_ID + 1),
                ]
            )
        self.assertEqual(raised.exception.code, 1)
        self.assertIn("BuildScope release state audit failed", stderr.getvalue())

    def test_rejects_oversized_json_payload(self) -> None:
        self.release_json.write_text('{"id":"too-large"}', encoding="utf-8")
        with (
            patch("check_buildscope_release_state.MAX_JSON_BYTES", 8),
            self.assertRaisesRegex(
                BuildScopeReleaseStateError, "outside the accepted range"
            ),
        ):
            check_release_state(self.release_json, TAG, VERSION, TARGET_SHA, "final")


if __name__ == "__main__":
    unittest.main()
