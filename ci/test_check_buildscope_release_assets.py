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
    BuildScopeReleaseAssetError,
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
                    "name": name,
                    "state": "uploaded",
                    "size": len(payload),
                    "digest": f"sha256:{hashlib.sha256(payload).hexdigest()}",
                }
            )
        release: dict[str, object] = {
            "tag_name": TAG,
            "draft": False,
            "prerelease": False,
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

    def test_rejects_duplicate_names_before_count_or_set_can_mask_it(self) -> None:
        assets = self.release["assets"]
        assert isinstance(assets, list)
        assets.append(dict(assets[0]))
        self._write_release(self.release)

        with self.assertRaisesRegex(
            BuildScopeReleaseAssetError, "duplicate public asset names"
        ):
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
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "final"):
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
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "must not be empty"):
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
