"""Tests for the final BuildScope GitHub Release asset auditor."""

from __future__ import annotations

import hashlib
import json
import os
import sys
import unittest
from contextlib import redirect_stderr
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

CI_DIR = Path(__file__).resolve().parent
if str(CI_DIR) not in sys.path:
    sys.path.insert(0, str(CI_DIR))

from check_buildscope_release_assets import (
    MAX_ASSET_BYTES,
    MAX_ASSET_NAME_BYTES,
    MAX_GITHUB_ID,
    MAX_TOTAL_ASSET_BYTES,
    BuildScopeReleaseAssetError,
    _assert_path_matches,
    _open_regular_file,
    check_release_assets,
    expected_asset_names,
    main,
)

VERSION = "0.5.0"
TAG = f"buildscope-v{VERSION}"


class BuildScopeReleaseAssetTests(unittest.TestCase):
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
        for index, name in enumerate(expected_asset_names(VERSION)):
            payload = f"BuildScope release fixture {index}: {name}\n".encode()
            (self.dist / name).write_bytes(payload)
            assets.append(
                {
                    "id": 1_000 + index,
                    "name": name,
                    "state": "uploaded",
                    "size": len(payload),
                    "digest": f"sha256:{hashlib.sha256(payload).hexdigest()}",
                }
            )
        release: dict[str, object] = {
            "id": 500,
            "tag_name": TAG,
            "name": "BuildScope 0.5.0",
            "draft": False,
            "prerelease": False,
            "published_at": "2026-09-02T00:00:00Z",
            "assets": assets,
        }
        self._write_release(release)
        return release

    def _write_release(self, release: object) -> None:
        self.release_json.write_text(
            json.dumps(release, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )

    def _check(self) -> None:
        check_release_assets(self.release_json, self.dist, TAG, VERSION)

    def test_accepts_final_release_with_exact_nine_streamed_assets(self) -> None:
        with patch("check_buildscope_release_assets.HASH_CHUNK_BYTES", 7):
            self._check()

    def test_cli_returns_success_for_valid_release(self) -> None:
        self.assertEqual(
            main([str(self.release_json), str(self.dist), TAG, VERSION]), 0
        )

    def test_accepts_draft_only_when_the_draft_stage_is_explicit(self) -> None:
        self.release["draft"] = True
        self.release["published_at"] = None
        self._write_release(self.release)

        check_release_assets(
            self.release_json,
            self.dist,
            TAG,
            VERSION,
            stage="draft",
        )
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "draft=False"):
            self._check()

    def test_rejects_wrong_count_and_extra_asset(self) -> None:
        assets = self.release["assets"]
        assert isinstance(assets, list)

        assets.pop()
        self._write_release(self.release)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "count mismatch"):
            self._check()

        self.release = self._write_valid_release()
        assets = self.release["assets"]
        assert isinstance(assets, list)
        assets[-1]["name"] = "unexpected.bin"
        self._write_release(self.release)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "set mismatch"):
            self._check()

    def test_rejects_non_nine_asset_arrays_before_inspecting_entries(self) -> None:
        assets = self.release["assets"]
        assert isinstance(assets, list)
        assets.append(dict(assets[0]))
        self._write_release(self.release)

        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "count mismatch"):
            self._check()

    def test_rejects_malformed_release_and_asset_objects(self) -> None:
        malformed_cases: tuple[object, ...] = (
            [],
            {"tag_name": TAG, "draft": False, "prerelease": False, "assets": "assets"},
            {
                "tag_name": TAG,
                "draft": False,
                "prerelease": False,
                "assets": [None],
            },
        )
        for malformed in malformed_cases:
            with self.subTest(malformed=malformed):
                self._write_release(malformed)
                with self.assertRaises(BuildScopeReleaseAssetError):
                    self._check()

        self.release = self._write_valid_release()
        assets = self.release["assets"]
        assert isinstance(assets, list)
        assets[0]["name"] = None
        self._write_release(self.release)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "invalid name"):
            self._check()

    def test_rejects_wrong_tag_draft_and_prerelease_states(self) -> None:
        self.release["tag_name"] = "buildscope-v9.9.9"
        self._write_release(self.release)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "tag mismatch"):
            self._check()

        self.release = self._write_valid_release()
        self.release["draft"] = True
        self._write_release(self.release)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "final"):
            self._check()

    def test_rejects_invalid_version_or_mismatched_tag_argument(self) -> None:
        with self.assertRaisesRegex(
            BuildScopeReleaseAssetError, "invalid BuildScope version"
        ):
            check_release_assets(self.release_json, self.dist, "buildscope-v0.5", "0.5")
        with self.assertRaisesRegex(
            BuildScopeReleaseAssetError, "does not match version"
        ):
            check_release_assets(
                self.release_json, self.dist, "buildscope-v0.5.1", VERSION
            )

        self.release = self._write_valid_release()
        self.release["prerelease"] = True
        self._write_release(self.release)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "prerelease"):
            self._check()

    def test_rejects_invalid_release_identity_and_publication_metadata(self) -> None:
        invalid_cases = (
            ("id", 0, "release id"),
            ("name", "BuildScope nightly", "name mismatch"),
            ("published_at", None, "published_at"),
        )
        for key, value, message in invalid_cases:
            with self.subTest(key=key):
                self.release = self._write_valid_release()
                self.release[key] = value
                self._write_release(self.release)
                with self.assertRaisesRegex(BuildScopeReleaseAssetError, message):
                    self._check()

        self.release = self._write_valid_release()
        assets = self.release["assets"]
        assert isinstance(assets, list)
        assets[1]["id"] = assets[0]["id"]
        self._write_release(self.release)
        with self.assertRaisesRegex(
            BuildScopeReleaseAssetError, "duplicate public asset ids"
        ):
            self._check()

    def test_rejects_wrong_state_size_and_digest(self) -> None:
        assets = self.release["assets"]
        assert isinstance(assets, list)
        assets[0]["state"] = "new"
        self._write_release(self.release)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "not uploaded"):
            self._check()

        self.release = self._write_valid_release()
        assets = self.release["assets"]
        assert isinstance(assets, list)
        assets[0]["size"] = 999
        self._write_release(self.release)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "size mismatch"):
            self._check()

        self.release = self._write_valid_release()
        assets = self.release["assets"]
        assert isinstance(assets, list)
        assets[0]["digest"] = "sha256:" + "0" * 64
        self._write_release(self.release)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "digest mismatch"):
            self._check()

    def test_rejects_missing_symlink_and_nonregular_local_assets(self) -> None:
        missing = self.dist / expected_asset_names(VERSION)[0]
        missing.unlink()
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "cannot be inspected"):
            self._check()

        self.release = self._write_valid_release()
        symlink = self.dist / expected_asset_names(VERSION)[0]
        target = self.root / "outside.bin"
        target.write_bytes(b"outside")
        symlink.unlink()
        os.symlink(target, symlink)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "regular file"):
            self._check()

        self.release = self._write_valid_release()
        directory = self.dist / expected_asset_names(VERSION)[0]
        directory.unlink()
        directory.mkdir()
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "regular file"):
            self._check()

    def test_rejects_empty_assets_and_symlinked_dist_directory(self) -> None:
        name = expected_asset_names(VERSION)[0]
        local = self.dist / name
        local.write_bytes(b"")
        assets = self.release["assets"]
        assert isinstance(assets, list)
        assets[0]["size"] = 0
        assets[0]["digest"] = f"sha256:{hashlib.sha256(b'').hexdigest()}"
        self._write_release(self.release)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "accepted range"):
            self._check()

        self.release = self._write_valid_release()
        real_dist = self.root / "real-dist"
        self.dist.rename(real_dist)
        os.symlink(real_dist, self.dist)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "real directory"):
            self._check()

    def test_rejects_release_metadata_bounds_encoding_and_symlink(self) -> None:
        with patch("check_buildscope_release_assets.MAX_RELEASE_JSON_BYTES", 16):
            self.release_json.write_text('{"too": "large-value"}', encoding="utf-8")
            with self.assertRaisesRegex(
                BuildScopeReleaseAssetError, "outside the accepted range"
            ):
                self._check()

        self.release = self._write_valid_release()
        self.release_json.write_bytes(b"\xff")
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "UTF-8"):
            self._check()

        self.release = self._write_valid_release()
        target = self.root / "release-target.json"
        target.write_text(
            self.release_json.read_text(encoding="utf-8"), encoding="utf-8"
        )
        self.release_json.unlink()
        os.symlink(target, self.release_json)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "regular file"):
            self._check()

    def test_rejects_duplicate_keys_huge_numbers_and_nonstandard_constants(
        self,
    ) -> None:
        invalid_payloads = (
            '{"id": 500, "id": 501}',
            '{"id": 123456789012345678901}',
            '{"id": 500, "unexpected": NaN}',
            '{"id": 500, "unexpected": 1e999}',
        )
        for payload in invalid_payloads:
            with self.subTest(payload=payload):
                self.release_json.write_text(payload, encoding="utf-8")
                with self.assertRaisesRegex(
                    BuildScopeReleaseAssetError, "not valid JSON"
                ):
                    self._check()

    def test_rejects_fifo_and_path_replacement_during_descriptor_audit(self) -> None:
        fifo = self.dist / expected_asset_names(VERSION)[0]
        fifo.unlink()
        os.mkfifo(fifo, 0o600)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "regular file"):
            self._check()

        fifo.unlink()
        self.release = self._write_valid_release()
        self.release_json.unlink()
        os.mkfifo(self.release_json, 0o600)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "regular file"):
            self._check()

        self.release_json.unlink()
        self._write_release(self.release)
        local = self.dist / expected_asset_names(VERSION)[0]
        fd, info = _open_regular_file(local, "race fixture")
        replacement = self.root / "replacement.bin"
        replacement.write_bytes(local.read_bytes())
        local.unlink()
        os.symlink(replacement, local)
        try:
            with self.assertRaisesRegex(BuildScopeReleaseAssetError, "changed"):
                _assert_path_matches(local, "race fixture", info)
        finally:
            os.close(fd)

    def test_rejects_asset_metadata_ranges_and_total_size(self) -> None:
        cases = (
            ("id", MAX_GITHUB_ID + 1, "invalid id"),
            ("name", "x" * (MAX_ASSET_NAME_BYTES + 1), "excessively long"),
            ("size", MAX_ASSET_BYTES + 1, "accepted range"),
            ("digest", "SHA256:" + "0" * 64, "digest is invalid"),
        )
        for key, value, message in cases:
            with self.subTest(key=key):
                self.release = self._write_valid_release()
                assets = self.release["assets"]
                assert isinstance(assets, list)
                assets[0][key] = value
                self._write_release(self.release)
                with self.assertRaisesRegex(BuildScopeReleaseAssetError, message):
                    self._check()

        self.release = self._write_valid_release()
        assets = self.release["assets"]
        assert isinstance(assets, list)
        for asset in assets[:2]:
            asset["size"] = MAX_ASSET_BYTES
        assets[2]["size"] = MAX_TOTAL_ASSET_BYTES - 2 * MAX_ASSET_BYTES + 1
        self._write_release(self.release)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "total bound"):
            self._check()

    def test_cli_reports_clear_nonzero_error(self) -> None:
        self.release["draft"] = True
        self._write_release(self.release)
        stderr = StringIO()
        with redirect_stderr(stderr), self.assertRaises(SystemExit) as raised:
            main([str(self.release_json), str(self.dist), TAG, VERSION])
        self.assertEqual(raised.exception.code, 1)
        self.assertIn("BuildScope release asset audit failed", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
