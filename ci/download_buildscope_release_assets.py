#!/usr/bin/env python3
"""Download and verify every asset from a private BuildScope draft release."""

from __future__ import annotations

import argparse
import os
import re
import selectors
import signal
import stat
import subprocess
import time
from collections.abc import Sequence
from pathlib import Path
from typing import BinaryIO, cast

from check_buildscope_release_assets import (
    _NOFOLLOW,
    MAX_ASSET_BYTES,
    MAX_GITHUB_ID,
    BuildScopeReleaseAssetError,
    _assert_path_matches,
    _open_flags,
    _open_real_directory,
    check_release_assets,
    expected_asset_names,
    load_release_assets,
)

REPOSITORY_PATTERN = re.compile(
    r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,99}/[A-Za-z0-9][A-Za-z0-9_.-]{0,99}$"
)
DOWNLOAD_TIMEOUT_SECONDS = 120
DOWNLOAD_BUDGET_SECONDS = 600
MAX_ERROR_TEXT_BYTES = 4096
STDOUT_READ_CHUNK_BYTES = 64 * 1024
PROCESS_DRAIN_GRACE_SECONDS = 2


def _safe_repository(repository: str) -> str:
    if REPOSITORY_PATTERN.fullmatch(repository) is None:
        raise BuildScopeReleaseAssetError(
            f"invalid GitHub repository identifier: {repository!r}"
        )
    return repository


def _bounded_error_text(payload: bytes | bytearray | None) -> str:
    """Decode at most the bounded stderr prefix retained by the downloader."""

    if not payload:
        return "no stderr"
    return (
        bytes(payload[:MAX_ERROR_TEXT_BYTES]).decode("utf-8", errors="replace").strip()
    )


def _terminate_process(process: subprocess.Popen[bytes]) -> None:
    try:
        pid = getattr(process, "pid", None)
        if isinstance(pid, int) and pid > 0:
            os.killpg(pid, signal.SIGKILL)
        elif process.poll() is None:
            process.kill()
    except ProcessLookupError:
        try:
            if process.poll() is None:
                process.kill()
        except (OSError, ProcessLookupError):
            pass
    except OSError:
        pass


def _reap_process(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is None:
        _terminate_process(process)
        try:
            process.wait(timeout=1)
        except (OSError, subprocess.TimeoutExpired):
            pass


def _download_to_file(
    command: list[str],
    output: BinaryIO,
    expected_size: int,
    deadline: float,
) -> None:
    """Run ``gh`` while bounding stdout, stderr, and process lifetime."""

    try:
        process = subprocess.Popen(
            command,
            shell=False,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            close_fds=True,
            start_new_session=True,
        )
    except OSError as exc:
        raise BuildScopeReleaseAssetError(
            f"release asset download could not start: {exc}"
        ) from exc

    stdout = process.stdout
    stderr = process.stderr
    if stdout is None or stderr is None:
        _terminate_process(process)
        for stream in (stdout, stderr):
            if stream is not None:
                stream.close()
        _reap_process(process)
        raise BuildScopeReleaseAssetError(
            "release asset download did not provide stdout/stderr pipes"
        )

    stderr_buffer = bytearray()
    written = 0
    failure: str | None = None
    selector = selectors.DefaultSelector()
    streams: dict[int, tuple[BinaryIO, str]] = {}
    pipes = (cast(BinaryIO, stdout), cast(BinaryIO, stderr))
    try:
        try:
            for stream, label in zip(pipes, ("stdout", "stderr"), strict=True):
                os.set_blocking(stream.fileno(), False)
                selector.register(stream, selectors.EVENT_READ, label)
                streams[stream.fileno()] = (stream, label)
        except (KeyError, OSError, ValueError) as exc:
            _terminate_process(process)
            raise BuildScopeReleaseAssetError(
                f"release asset download streams could not be prepared: {exc}"
            ) from exc

        drain_deadline = deadline
        while streams:
            now = time.monotonic()
            if now >= drain_deadline:
                if failure is None:
                    failure = "release asset download timed out before completion"
                _terminate_process(process)
                if drain_deadline == deadline:
                    drain_deadline = now + PROCESS_DRAIN_GRACE_SECONDS
                else:
                    break

            wait_seconds = max(0.0, min(0.25, drain_deadline - now))
            try:
                events = selector.select(wait_seconds)
            except OSError as exc:
                if failure is None:
                    failure = f"release asset download streams became invalid: {exc}"
                _terminate_process(process)
                break
            if not events:
                continue

            for key, _ in events:
                stream = cast(BinaryIO, key.fileobj)
                label = cast(str, key.data)
                fd = stream.fileno()
                read_size = (
                    MAX_ERROR_TEXT_BYTES
                    if label == "stderr"
                    else min(
                        STDOUT_READ_CHUNK_BYTES,
                        max(1, expected_size - written + 1),
                    )
                )
                try:
                    chunk = os.read(fd, read_size)
                except BlockingIOError:
                    continue
                except OSError as exc:
                    if failure is None:
                        failure = f"release asset {label} could not be read: {exc}"
                    _terminate_process(process)
                    chunk = b""

                if not chunk:
                    try:
                        selector.unregister(stream)
                    except (KeyError, ValueError):
                        pass
                    streams.pop(fd, None)
                    stream.close()
                    continue

                if label == "stderr":
                    remaining = MAX_ERROR_TEXT_BYTES - len(stderr_buffer)
                    if remaining > 0:
                        stderr_buffer.extend(chunk[:remaining])
                    continue

                remaining = expected_size - written
                if len(chunk) > remaining:
                    if remaining > 0:
                        try:
                            output.write(chunk[:remaining])
                        except OSError as exc:
                            if failure is None:
                                failure = (
                                    f"release asset output could not be written: {exc}"
                                )
                            _terminate_process(process)
                            continue
                        written += remaining
                    if failure is None:
                        failure = (
                            "release asset download exceeded the API size: "
                            f"expected={expected_size}"
                        )
                    _terminate_process(process)
                    continue
                try:
                    output.write(chunk)
                except OSError as exc:
                    if failure is None:
                        failure = f"release asset output could not be written: {exc}"
                    _terminate_process(process)
                    continue
                written += len(chunk)

        try:
            return_code = process.wait(timeout=1)
        except subprocess.TimeoutExpired:
            _terminate_process(process)
            try:
                return_code = process.wait(timeout=1)
            except subprocess.TimeoutExpired as exc:
                raise BuildScopeReleaseAssetError(
                    "release asset download process did not terminate"
                ) from exc
        if failure is not None:
            raise BuildScopeReleaseAssetError(
                f"{failure}: {_bounded_error_text(stderr_buffer)}"
            )
        if return_code != 0:
            raise BuildScopeReleaseAssetError(
                "release asset download failed: "
                f"exit={return_code}: {_bounded_error_text(stderr_buffer)}"
            )
        if written != expected_size:
            raise BuildScopeReleaseAssetError(
                "release asset download size mismatch: "
                f"expected={expected_size} actual={written}"
            )
    finally:
        selector.close()
        for stream in pipes:
            try:
                stream.close()
            except OSError:
                pass
        _reap_process(process)


def _open_destination(
    destination: Path,
) -> tuple[int, int, os.stat_result]:
    """Create and open a fresh destination using parent/directory descriptors."""

    parent_fd, parent_info = _open_real_directory(
        destination.parent, "download parent directory"
    )
    created_info: os.stat_result | None = None
    try:
        _assert_path_matches(
            destination.parent, "download parent directory", parent_info
        )
        if not destination.name:
            raise BuildScopeReleaseAssetError(
                "download destination must name a new directory"
            )
        try:
            os.mkdir(destination.name, mode=0o700, dir_fd=parent_fd)
        except OSError as exc:
            raise BuildScopeReleaseAssetError(
                f"download destination must be new and creatable: {exc}"
            ) from exc
        try:
            created_info = os.stat(
                destination.name,
                dir_fd=parent_fd,
                follow_symlinks=False,
            )
        except OSError as exc:
            raise BuildScopeReleaseAssetError(
                f"download destination cannot be inspected after creation: {exc}"
            ) from exc
        if not stat.S_ISDIR(created_info.st_mode):
            raise BuildScopeReleaseAssetError(
                "download destination changed immediately after creation"
            )
        try:
            destination_fd = os.open(
                destination.name,
                _open_flags(directory=True),
                dir_fd=parent_fd,
            )
        except OSError as exc:
            raise BuildScopeReleaseAssetError(
                f"download destination cannot be opened safely: {exc}"
            ) from exc
        try:
            destination_info = os.fstat(destination_fd)
            if not stat.S_ISDIR(destination_info.st_mode):
                raise BuildScopeReleaseAssetError(
                    "download destination must be a real directory"
                )
            if _directory_identity(destination_info) != _directory_identity(
                created_info
            ):
                raise BuildScopeReleaseAssetError(
                    "download destination changed while it was opened"
                )
            _assert_path_matches(destination, "download destination", destination_info)
            return parent_fd, destination_fd, destination_info
        except BaseException:
            os.close(destination_fd)
            raise
    except BaseException:
        if created_info is not None:
            try:
                current = os.stat(
                    destination.name,
                    dir_fd=parent_fd,
                    follow_symlinks=False,
                )
                if _directory_identity(current) == _directory_identity(created_info):
                    os.rmdir(destination.name, dir_fd=parent_fd)
            except OSError:
                pass
        os.close(parent_fd)
        raise


def _open_partial(destination_fd: int, name: str) -> tuple[int, os.stat_result]:
    if _NOFOLLOW is None:
        raise BuildScopeReleaseAssetError(
            "this platform does not provide O_NOFOLLOW for safe downloads"
        )
    flags = (
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | _NOFOLLOW | getattr(os, "O_CLOEXEC", 0)
    )
    try:
        fd = os.open(name, flags, mode=0o600, dir_fd=destination_fd)
    except OSError as exc:
        raise BuildScopeReleaseAssetError(
            f"download partial file cannot be created: {name}: {exc}"
        ) from exc
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode):
            raise BuildScopeReleaseAssetError(
                f"download partial file is not regular: {name}"
            )
        return fd, info
    except BaseException:
        os.close(fd)
        raise


def _assert_directory_entry_matches(
    destination_fd: int,
    name: str,
    label: str,
    initial_info: os.stat_result,
) -> None:
    try:
        final_info = os.stat(name, dir_fd=destination_fd, follow_symlinks=False)
    except OSError as exc:
        raise BuildScopeReleaseAssetError(
            f"{label} disappeared while it was being audited: {exc}"
        ) from exc
    if _file_identity(final_info) != _file_identity(initial_info):
        raise BuildScopeReleaseAssetError(f"{label} changed while it was being audited")


def _finalize_partial(
    destination_fd: int,
    partial_name: str,
    name: str,
    partial_info: os.stat_result,
    created_entries: dict[str, tuple[int, int, int]],
) -> None:
    """Publish a partial file without replacing an entry created by a race."""

    try:
        os.link(
            partial_name,
            name,
            src_dir_fd=destination_fd,
            dst_dir_fd=destination_fd,
            follow_symlinks=False,
        )
        final_info = os.stat(name, dir_fd=destination_fd, follow_symlinks=False)
        if _owned_file_identity(final_info) != _owned_file_identity(partial_info):
            raise BuildScopeReleaseAssetError(
                f"downloaded asset changed while it was finalized: {name}"
            )
        created_entries[name] = _owned_file_identity(final_info)
        os.unlink(partial_name, dir_fd=destination_fd)
    except OSError as exc:
        raise BuildScopeReleaseAssetError(
            f"downloaded asset cannot be finalized: {name}: {exc}"
        ) from exc


def _directory_identity(info: os.stat_result) -> tuple[int, int, int]:
    return info.st_dev, info.st_ino, info.st_mode & 0o170000


def _file_identity(info: os.stat_result) -> tuple[int, int, int, int]:
    return info.st_dev, info.st_ino, info.st_mode & 0o170000, info.st_nlink


def _owned_file_identity(info: os.stat_result) -> tuple[int, int, int]:
    """Return the stable identity used to prove cleanup ownership."""

    return info.st_dev, info.st_ino, info.st_mode & 0o170000


def _assert_directory_path_identity(
    path: Path, label: str, initial_info: os.stat_result
) -> None:
    try:
        current = os.stat(path, follow_symlinks=False)
    except OSError as exc:
        raise BuildScopeReleaseAssetError(
            f"{label} disappeared while it was being audited: {exc}"
        ) from exc
    if _directory_identity(current) != _directory_identity(initial_info):
        raise BuildScopeReleaseAssetError(f"{label} changed while it was being audited")


def _cleanup_destination(
    destination: Path,
    parent_fd: int,
    destination_fd: int,
    destination_info: os.stat_result,
    entries: dict[str, tuple[int, int, int]],
) -> None:
    """Remove only files created during this run, then the owned empty directory."""

    for name, owned_identity in sorted(entries.items()):
        try:
            current = os.stat(name, dir_fd=destination_fd, follow_symlinks=False)
            if _owned_file_identity(current) == owned_identity:
                os.unlink(name, dir_fd=destination_fd)
        except FileNotFoundError:
            pass
        except OSError:
            # Never broaden cleanup to a recursive/path-following delete.
            pass
    try:
        current = os.stat(
            destination.name,
            dir_fd=parent_fd,
            follow_symlinks=False,
        )
        if _directory_identity(current) == _directory_identity(destination_info):
            os.rmdir(destination.name, dir_fd=parent_fd)
    except OSError:
        pass


def download_release_assets(
    release_json: Path,
    destination: Path,
    repository: str,
    tag: str,
    version: str,
    *,
    stage: str = "draft",
    gh_executable: str = "gh",
) -> None:
    """Download exact API asset IDs into a fresh directory and verify all bytes."""

    repository = _safe_repository(repository)
    _, by_name = load_release_assets(
        release_json,
        tag,
        version,
        stage=stage,
    )

    parent_fd: int | None = None
    destination_fd: int | None = None
    destination_info: os.stat_result | None = None
    created_entries: dict[str, tuple[int, int, int]] = {}
    partial_entries: dict[str, tuple[int, int, int]] = {}
    try:
        parent_fd, destination_fd, destination_info = _open_destination(destination)
        deadline = time.monotonic() + DOWNLOAD_BUDGET_SECONDS
        for name in expected_asset_names(version):
            asset_id = by_name[name]["id"]
            expected_size = by_name[name]["size"]
            if (
                isinstance(asset_id, bool)
                or not isinstance(asset_id, int)
                or not 0 < asset_id <= MAX_GITHUB_ID
                or isinstance(expected_size, bool)
                or not isinstance(expected_size, int)
                or not 0 < expected_size <= MAX_ASSET_BYTES
            ):
                raise BuildScopeReleaseAssetError(
                    f"asset metadata became invalid while downloading: {name}"
                )
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise BuildScopeReleaseAssetError(
                    "release asset download exceeded the overall time budget"
                )
            asset_deadline = min(
                deadline,
                time.monotonic() + DOWNLOAD_TIMEOUT_SECONDS,
            )
            partial_name = f".{name}.partial"
            partial_fd: int | None = None
            try:
                partial_fd, partial_info = _open_partial(destination_fd, partial_name)
                partial_entries[partial_name] = _owned_file_identity(partial_info)
                with os.fdopen(partial_fd, "wb", closefd=True) as output:
                    partial_fd = None
                    _download_to_file(
                        [
                            gh_executable,
                            "api",
                            "-H",
                            "Accept: application/octet-stream",
                            f"repos/{repository}/releases/assets/{asset_id}",
                        ],
                        output,
                        expected_size,
                        asset_deadline,
                    )
                    output.flush()
                    final_info = os.fstat(output.fileno())
                    if (
                        _file_identity(final_info) != _file_identity(partial_info)
                        or final_info.st_size != expected_size
                    ):
                        raise BuildScopeReleaseAssetError(
                            f"download partial file changed while being written: {name}"
                        )
                _assert_directory_entry_matches(
                    destination_fd,
                    partial_name,
                    f"download partial file {name}",
                    partial_info,
                )
                _finalize_partial(
                    destination_fd,
                    partial_name,
                    name,
                    partial_info,
                    created_entries,
                )
                _assert_directory_entry_matches(
                    destination_fd,
                    name,
                    f"downloaded asset {name}",
                    partial_info,
                )
                final_info = os.stat(
                    name,
                    dir_fd=destination_fd,
                    follow_symlinks=False,
                )
                created_entries[name] = _owned_file_identity(final_info)
                partial_entries.pop(partial_name, None)
            finally:
                if partial_fd is not None:
                    os.close(partial_fd)

        _assert_directory_path_identity(
            destination, "download destination", destination_info
        )
        check_release_assets(
            release_json,
            destination,
            tag,
            version,
            stage=stage,
        )
        _assert_directory_path_identity(
            destination, "download destination", destination_info
        )
    except BuildScopeReleaseAssetError:
        if (
            parent_fd is not None
            and destination_fd is not None
            and destination_info is not None
        ):
            _cleanup_destination(
                destination,
                parent_fd,
                destination_fd,
                destination_info,
                created_entries | partial_entries,
            )
        raise
    except OSError as exc:
        if (
            parent_fd is not None
            and destination_fd is not None
            and destination_info is not None
        ):
            _cleanup_destination(
                destination,
                parent_fd,
                destination_fd,
                destination_info,
                created_entries | partial_entries,
            )
        raise BuildScopeReleaseAssetError(
            f"release asset download failed safely: {exc}"
        ) from exc
    finally:
        try:
            if destination_fd is not None:
                os.close(destination_fd)
        finally:
            if parent_fd is not None:
                os.close(parent_fd)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("release_json", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("repository")
    parser.add_argument("tag")
    parser.add_argument("version")
    parser.add_argument(
        "--stage",
        choices=("draft", "final"),
        default="draft",
        help="release visibility required by the download (default: draft)",
    )
    args = parser.parse_args(argv)
    try:
        download_release_assets(
            args.release_json,
            args.destination,
            args.repository,
            args.tag,
            args.version,
            stage=args.stage,
        )
    except BuildScopeReleaseAssetError as exc:
        parser.exit(1, f"BuildScope release download audit failed: {exc}{os.linesep}")
    print(
        f"downloaded and audited {args.stage} BuildScope {args.version}: exact 9 assets"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
