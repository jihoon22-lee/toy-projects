"""Tests for private BuildScope release asset downloads by immutable API ID."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import threading
import time
import unittest
from contextlib import redirect_stderr
from io import BytesIO, StringIO
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
from download_buildscope_release_assets import (
    _cleanup_destination,
    _download_to_file,
    _open_destination,
    _owned_file_identity,
    download_release_assets,
    main,
)

VERSION = "0.5.0"
TAG = "buildscope-v0.5.0"
REPOSITORY = "jihoon22-lee/toy-projects"


class _PipeProcess:
    """Small real-pipe process double so selector streaming is exercised."""

    def __init__(
        self,
        command: list[str],
        payload: bytes,
        *,
        returncode: int = 0,
        stderr: bytes = b"",
        hold: bool = False,
    ) -> None:
        self.command = command
        self._returncode = returncode
        self._hold = hold
        self._killed = False
        self._done = threading.Event()
        self._release = threading.Event()
        stdout_read, self._stdout_write = os.pipe()
        stderr_read, self._stderr_write = os.pipe()
        self.stdout = os.fdopen(stdout_read, "rb", buffering=0)
        self.stderr = os.fdopen(stderr_read, "rb", buffering=0)
        self._payload = payload
        self._stderr_payload = stderr
        self._thread = threading.Thread(target=self._produce, daemon=True)
        self._thread.start()

    def _write(self, fd: int, payload: bytes) -> None:
        offset = 0
        try:
            while offset < len(payload) and not self._killed:
                offset += os.write(fd, payload[offset:])
        except OSError:
            pass

    def _produce(self) -> None:
        try:
            self._write(self._stdout_write, self._payload)
            self._write(self._stderr_write, self._stderr_payload)
            if self._hold and not self._killed:
                self._release.wait()
        finally:
            for fd_name in ("_stdout_write", "_stderr_write"):
                fd = getattr(self, fd_name, None)
                if fd is not None:
                    try:
                        os.close(fd)
                    except OSError:
                        pass
                    setattr(self, fd_name, None)
            self._done.set()

    def poll(self) -> int | None:
        if not self._done.is_set():
            return None
        return -9 if self._killed else self._returncode

    def wait(self, timeout: float | None = None) -> int:
        if not self._done.wait(timeout):
            if timeout is None:
                raise RuntimeError("an unbounded fake wait unexpectedly timed out")
            raise subprocess.TimeoutExpired(self.command, timeout)
        return self.poll() or 0

    def kill(self) -> None:
        self._killed = True
        self._release.set()
        for fd_name in ("_stdout_write", "_stderr_write"):
            fd = getattr(self, fd_name, None)
            if fd is not None:
                try:
                    os.close(fd)
                except OSError:
                    pass
                setattr(self, fd_name, None)


class BuildScopeReleaseDownloadTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.release_json = self.root / "release.json"
        self.destination = self.root / "downloaded"
        self.payloads: dict[int, bytes] = {}
        self.behaviors: list[dict[str, object]] = []
        self.popen_calls: list[tuple[list[str], dict[str, object]]] = []
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

    def _fake_popen(
        self,
        command: list[str],
        **kwargs: object,
    ) -> _PipeProcess:
        self.popen_calls.append((command, kwargs))
        asset_id = int(command[-1].rsplit("/", 1)[-1])
        behavior = self.behaviors[len(self.popen_calls) - 1] if self.behaviors else {}
        return _PipeProcess(
            command,
            behavior.get("payload", self.payloads[asset_id]),  # type: ignore[arg-type]
            returncode=behavior.get("returncode", 0),  # type: ignore[arg-type]
            stderr=behavior.get("stderr", b""),  # type: ignore[arg-type]
            hold=behavior.get("hold", False),  # type: ignore[arg-type]
        )

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
            "download_buildscope_release_assets.subprocess.Popen",
            side_effect=self._fake_popen,
        ) as popen:
            self._download()

        self.assertEqual(
            sorted(path.name for path in self.destination.iterdir()),
            sorted(expected_asset_names(VERSION)),
        )
        self.assertEqual(popen.call_count, 9)
        for call, expected_name in zip(
            popen.call_args_list,
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
            self.assertIs(call.kwargs["stdin"], subprocess.DEVNULL)
            self.assertIs(call.kwargs["stdout"], subprocess.PIPE)
            self.assertIs(call.kwargs["stderr"], subprocess.PIPE)
            self.assertFalse(call.kwargs["shell"])
            self.assertTrue(call.kwargs["close_fds"])
            self.assertTrue(call.kwargs["start_new_session"])
            asset_id = int(command[-1].rsplit("/", 1)[-1])
            self.assertEqual(
                (self.destination / expected_name).read_bytes(), self.payloads[asset_id]
            )

    def test_rejects_an_existing_destination_before_spawning_gh(self) -> None:
        self.destination.mkdir()
        with (
            patch("download_buildscope_release_assets.subprocess.Popen") as popen,
            self.assertRaisesRegex(BuildScopeReleaseAssetError, "must be new"),
        ):
            self._download()
        popen.assert_not_called()

    def test_removes_owned_destination_when_open_after_mkdir_fails(self) -> None:
        real_open = os.open

        def fail_destination_open(
            path: object,
            flags: int,
            mode: int = 0o777,
            *,
            dir_fd: int | None = None,
        ) -> int:
            if path == self.destination.name and dir_fd is not None:
                raise OSError("simulated destination open failure")
            if dir_fd is None:
                return real_open(path, flags, mode)
            return real_open(path, flags, mode, dir_fd=dir_fd)

        with (
            patch(
                "download_buildscope_release_assets.os.open",
                side_effect=fail_destination_open,
            ),
            patch("download_buildscope_release_assets.subprocess.Popen") as popen,
            self.assertRaisesRegex(BuildScopeReleaseAssetError, "opened safely"),
        ):
            self._download()
        popen.assert_not_called()
        self.assertFalse(self.destination.exists())

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
        self.behaviors = [
            {"returncode": 1, "stderr": b"asset API failed\n" + b"x" * 10_000}
        ]
        with (
            patch(
                "download_buildscope_release_assets.subprocess.Popen",
                side_effect=self._fake_popen,
            ),
            self.assertRaisesRegex(BuildScopeReleaseAssetError, "asset API failed"),
        ):
            self._download()
        self.assertFalse(self.destination.exists())

        second_destination = self.root / "timed-out"
        self.behaviors = [{"hold": True}]
        self.popen_calls = []
        with (
            patch(
                "download_buildscope_release_assets.subprocess.Popen",
                side_effect=self._fake_popen,
            ),
            patch("download_buildscope_release_assets.DOWNLOAD_TIMEOUT_SECONDS", 1),
            patch("download_buildscope_release_assets.DOWNLOAD_BUDGET_SECONDS", 1),
            patch("download_buildscope_release_assets.PROCESS_DRAIN_GRACE_SECONDS", 0),
            self.assertRaisesRegex(BuildScopeReleaseAssetError, "before completion"),
        ):
            download_release_assets(
                self.release_json,
                second_destination,
                REPOSITORY,
                TAG,
                VERSION,
            )
        self.assertFalse(second_destination.exists())

    def test_rejects_downloaded_bytes_that_disagree_with_api_digest(self) -> None:
        def corrupt_download(command: list[str], **kwargs: object) -> _PipeProcess:
            self.popen_calls.append((command, kwargs))
            return _PipeProcess(command, b"corrupt")

        with (
            patch(
                "download_buildscope_release_assets.subprocess.Popen",
                side_effect=corrupt_download,
            ),
            self.assertRaisesRegex(BuildScopeReleaseAssetError, "size mismatch"),
        ):
            self._download()

    def test_cli_reports_clear_success_and_failure(self) -> None:
        with patch(
            "download_buildscope_release_assets.subprocess.Popen",
            side_effect=self._fake_popen,
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

    def test_rejects_failure_on_second_asset_and_cleans_first_asset(self) -> None:
        self.behaviors = [{}, {"returncode": 1, "stderr": b"second asset failed"}]
        with (
            patch(
                "download_buildscope_release_assets.subprocess.Popen",
                side_effect=self._fake_popen,
            ),
            self.assertRaisesRegex(BuildScopeReleaseAssetError, "second asset failed"),
        ):
            self._download()
        self.assertFalse(self.destination.exists())
        self.assertEqual(len(self.popen_calls), 2)

    def test_finalization_failure_cleans_both_partial_and_linked_asset(self) -> None:
        real_unlink = os.unlink
        failed_once = False

        def fail_first_partial_unlink(
            path: object,
            *,
            dir_fd: int | None = None,
        ) -> None:
            nonlocal failed_once
            if (
                path == ".buildscope.pyz.partial"
                and dir_fd is not None
                and not failed_once
            ):
                failed_once = True
                raise OSError("simulated partial unlink failure")
            if dir_fd is None:
                real_unlink(path)
            else:
                real_unlink(path, dir_fd=dir_fd)

        with (
            patch(
                "download_buildscope_release_assets.subprocess.Popen",
                side_effect=self._fake_popen,
            ),
            patch(
                "download_buildscope_release_assets.os.unlink",
                side_effect=fail_first_partial_unlink,
            ),
            self.assertRaisesRegex(BuildScopeReleaseAssetError, "cannot be finalized"),
        ):
            self._download()
        self.assertTrue(failed_once)
        self.assertFalse(self.destination.exists())
        self.assertEqual(len(self.popen_calls), 1)

    def test_cleanup_preserves_a_replacement_with_a_different_identity(self) -> None:
        parent_fd, destination_fd, destination_info = _open_destination(
            self.destination
        )
        try:
            owned = self.destination / "owned.bin"
            owned.write_bytes(b"owned")
            owned_info = os.stat(
                "owned.bin",
                dir_fd=destination_fd,
                follow_symlinks=False,
            )
            replacement = self.root / "replacement.bin"
            replacement.write_bytes(b"replacement")
            os.replace(replacement, owned)

            _cleanup_destination(
                self.destination,
                parent_fd,
                destination_fd,
                destination_info,
                {"owned.bin": _owned_file_identity(owned_info)},
            )
            self.assertEqual(owned.read_bytes(), b"replacement")
            self.assertTrue(self.destination.is_dir())
        finally:
            os.close(destination_fd)
            os.close(parent_fd)

    def test_pipe_setup_failure_closes_pipes_and_reaps_process(self) -> None:
        process = _PipeProcess(["gh"], b"fixture")
        real_set_blocking = os.set_blocking
        calls = 0

        def fail_second_pipe(fd: int, blocking: bool) -> None:
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError("simulated pipe setup failure")
            real_set_blocking(fd, blocking)

        with (
            patch(
                "download_buildscope_release_assets.subprocess.Popen",
                return_value=process,
            ),
            patch(
                "download_buildscope_release_assets.os.set_blocking",
                side_effect=fail_second_pipe,
            ),
            self.assertRaisesRegex(
                BuildScopeReleaseAssetError, "could not be prepared"
            ),
        ):
            _download_to_file(["gh"], BytesIO(), 7, time.monotonic() + 2)
        self.assertTrue(process.stdout.closed)
        self.assertTrue(process.stderr.closed)
        self.assertIsNotNone(process.poll())

    @unittest.skipUnless(hasattr(os, "fork"), "requires POSIX process groups")
    def test_timeout_terminates_descendants_that_hold_download_pipes(self) -> None:
        script = (
            "import os, time\n"
            "if os.fork() == 0:\n"
            "    time.sleep(60)\n"
            "else:\n"
            "    os.write(1, b'x')\n"
            "    os._exit(0)\n"
        )
        started = time.monotonic()
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "timed out"):
            _download_to_file(
                [sys.executable, "-c", script],
                BytesIO(),
                1,
                started + 0.25,
            )
        self.assertLess(time.monotonic() - started, 3.0)

    def test_rejects_stdout_that_exceeds_the_exact_api_size(self) -> None:
        first_name = expected_asset_names(VERSION)[0]
        assets = self.release["assets"]
        assert isinstance(assets, list)
        first_asset = assets[0]
        assert isinstance(first_asset, dict)
        asset_id = first_asset["id"]
        assert isinstance(asset_id, int)
        expected = self.payloads[asset_id]
        self.behaviors = [{"payload": expected + b"overflow"}]
        with (
            patch(
                "download_buildscope_release_assets.subprocess.Popen",
                side_effect=self._fake_popen,
            ),
            self.assertRaisesRegex(
                BuildScopeReleaseAssetError, "exceeded the API size"
            ),
        ):
            self._download()
        self.assertFalse(self.destination.exists())
        self.assertEqual(len(self.popen_calls), 1)
        self.assertFalse((self.destination / first_name).exists())

    def test_drains_large_stderr_but_retains_at_most_4096_bytes(self) -> None:
        self.behaviors = [{"returncode": 1, "stderr": b"e" * 100_000}]
        with (
            patch(
                "download_buildscope_release_assets.subprocess.Popen",
                side_effect=self._fake_popen,
            ),
            self.assertRaises(BuildScopeReleaseAssetError) as raised,
        ):
            self._download()
        message = str(raised.exception)
        self.assertIn("release asset download failed", message)
        stderr_fragment = message.rsplit(": ", 1)[-1]
        self.assertLessEqual(len(stderr_fragment), 4_096)
        self.assertNotIn("e" * 4_097, message)
        self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main()
