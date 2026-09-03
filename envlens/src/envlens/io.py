"""Atomic, private-by-default snapshot writes."""

from __future__ import annotations

import os
import tempfile
from collections.abc import Mapping
from contextlib import suppress
from pathlib import Path

from envlens.snapshot import SnapshotError, dumps_snapshot


def write_snapshot(snapshot: Mapping[str, object], output: Path, *, pretty: bool = False) -> None:
    """Atomically replace a regular output while refusing links/special files."""

    output = Path(output)
    parent = output.parent
    if not parent.is_dir() or parent.is_symlink():
        raise SnapshotError("invalid-output", "output parent must be a real directory")
    if output.is_symlink() or (output.exists() and not output.is_file()):
        raise SnapshotError("invalid-output", "output must be a regular file or absent")
    payload = dumps_snapshot(snapshot, pretty=pretty).encode("utf-8")
    descriptor = -1
    temporary_name = ""
    try:
        descriptor, temporary_name = tempfile.mkstemp(
            dir=parent, prefix=f".{output.name}.", suffix=".tmp"
        )
        if os.name == "posix":
            os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            descriptor = -1
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, output)
        temporary_name = ""
        if os.name == "posix":
            directory_fd = os.open(parent, os.O_RDONLY)
            try:
                os.fsync(directory_fd)
            finally:
                os.close(directory_fd)
    except OSError as error:
        raise SnapshotError("atomic-write-failed", str(error)) from error
    finally:
        if descriptor >= 0:
            os.close(descriptor)
        if temporary_name:
            with suppress(FileNotFoundError):
                os.unlink(temporary_name)
