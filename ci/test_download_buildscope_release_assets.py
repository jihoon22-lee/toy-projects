"""Tests for private BuildScope release asset downloads by immutable API ID."""

from __future__ import annotations

import hashlib
import json
import subprocess
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
    expected_asset_names,
)
from download_buildscope_release_assets import download_release_assets, main

VERSION = "0.5.0"
TAG = "buildscope-v0.5.0"
REPOSITORY = "jihoon22-lee/toy-projects"


class BuildScopeReleaseDownloadTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.release_json = self.root / "release.json"
        self.destination = self.root / "downloaded"
        self.payloads: dict[int, bytes] = {}
        assets = []
        for index, name in enumerate(expected_asset_names(VERSION)):
            asset_id = 1_000 + index
            payload = f"downloaded BuildScope fixture {index}: {name}\n".encode()
            self.payloads[asset_id] = payload
            assets.append(
                {
                    "id": asset_id,
                    "name": name,
                    "state": "uploaded",
                    "size": len(payload),
                    "digest": f"sha256:{hashlib.sha256(payload).hexdigest()}",
                }
            )
        self.release = {
            "id": 500,
            "tag_name": TAG,
            "name": "BuildScope 0.5.0",
            "draft": True,
            "prerelease": False,
            "published_at": None,
            "assets": assets,
        }
        self._write_release()

    def _write_release(self) -> None:
        self.release_json.write_text(
            json.dumps(self.release, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )

    def _completed_download(
        self,
        command: list[str],
        **kwargs: object,
    ) -> subprocess.CompletedProcess[bytes]:
        asset_id = int(command[-1].rsplit("/", 1)[-1])
        output = kwargs["stdout"]
        output.write(self.payloads[asset_id])  # type: ignore[attr-defined]
        return subprocess.CompletedProcess(command, 0, stderr=b"")

    def _download(self) -> None:
        download_release_assets(
            self.release_json,
            self.destination,
            REPOSITORY,
            TAG,
            VERSION,
        )

    def test_downloads_each_exact_asset_id_without_a_shell_and_verifies_bytes(
        self,
    ) -> None:
        with patch(
            "download_buildscope_release_assets.subprocess.run",
            side_effect=self._completed_download,
        ) as run:
            self._download()

        self.assertEqual(
            sorted(path.name for path in self.destination.iterdir()),
            sorted(expected_asset_names(VERSION)),
        )
        self.assertEqual(run.call_count, 9)
        for call, expected_name in zip(
            run.call_args_list,
            expected_asset_names(VERSION),
            strict=True,
        ):
            command = call.args[0]
            self.assertEqual(
                command[:4], ["gh", "api", "-H", "Accept: application/octet-stream"]
            )
            self.assertTrue(
                command[-1].startswith(f"repos/{REPOSITORY}/releases/assets/")
            )
            asset_id = int(command[-1].rsplit("/", 1)[-1])
            self.assertEqual(
                (self.destination / expected_name).read_bytes(), self.payloads[asset_id]
            )
            self.assertIs(call.kwargs["stdin"], subprocess.DEVNULL)

    def test_rejects_an_existing_destination_before_spawning_gh(self) -> None:
        self.destination.mkdir()
        with (
            patch("download_buildscope_release_assets.subprocess.run") as run,
            self.assertRaisesRegex(BuildScopeReleaseAssetError, "must be new"),
        ):
            self._download()
        run.assert_not_called()

    def test_rejects_unsafe_repository_before_creating_destination(self) -> None:
        with self.assertRaisesRegex(
            BuildScopeReleaseAssetError, "repository identifier"
        ):
            download_release_assets(
                self.release_json,
                self.destination,
                "owner/repo;echo",
                TAG,
                VERSION,
            )
        self.assertFalse(self.destination.exists())

    def test_rejects_failed_or_timed_out_download_and_removes_partial_file(
        self,
    ) -> None:
        failed = subprocess.CompletedProcess(
            ["gh"],
            1,
            stderr=b"asset API failed\n" + b"x" * 10_000,
        )
        with (
            patch(
                "download_buildscope_release_assets.subprocess.run", return_value=failed
            ),
            self.assertRaisesRegex(BuildScopeReleaseAssetError, "asset API failed"),
        ):
            self._download()
        self.assertEqual(list(self.destination.iterdir()), [])

        second_destination = self.root / "timed-out"
        with (
            patch(
                "download_buildscope_release_assets.subprocess.run",
                side_effect=subprocess.TimeoutExpired(["gh"], 1),
            ),
            self.assertRaisesRegex(BuildScopeReleaseAssetError, "before completion"),
        ):
            download_release_assets(
                self.release_json,
                second_destination,
                REPOSITORY,
                TAG,
                VERSION,
            )
        self.assertEqual(list(second_destination.iterdir()), [])

    def test_rejects_downloaded_bytes_that_disagree_with_api_digest(self) -> None:
        def corrupt_download(
            command: list[str],
            **kwargs: object,
        ) -> subprocess.CompletedProcess[bytes]:
            output = kwargs["stdout"]
            output.write(b"corrupt")  # type: ignore[attr-defined]
            return subprocess.CompletedProcess(command, 0, stderr=b"")

        with (
            patch(
                "download_buildscope_release_assets.subprocess.run",
                side_effect=corrupt_download,
            ),
            self.assertRaisesRegex(BuildScopeReleaseAssetError, "size mismatch"),
        ):
            self._download()

    def test_cli_reports_clear_success_and_failure(self) -> None:
        with patch(
            "download_buildscope_release_assets.subprocess.run",
            side_effect=self._completed_download,
        ):
            self.assertEqual(
                main(
                    [
                        str(self.release_json),
                        str(self.destination),
                        REPOSITORY,
                        TAG,
                        VERSION,
                    ]
                ),
                0,
            )

        stderr = StringIO()
        with redirect_stderr(stderr), self.assertRaises(SystemExit) as raised:
            main(
                [
                    str(self.release_json),
                    str(self.destination),
                    REPOSITORY,
                    TAG,
                    VERSION,
                ]
            )
        self.assertEqual(raised.exception.code, 1)
        self.assertIn("BuildScope release download audit failed", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
