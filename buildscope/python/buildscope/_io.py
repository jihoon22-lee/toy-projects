"""Stable input and atomic output primitives for BuildScope snapshots."""

from __future__ import annotations

import os
import secrets
import stat
import tempfile
from contextlib import suppress
from pathlib import Path


class SnapshotIoError(OSError):
    """Raised when snapshot input or output cannot be handled safely."""


def _object_identity(metadata: os.stat_result) -> tuple[int, int, int]:
    return metadata.st_dev, metadata.st_ino, stat.S_IFMT(metadata.st_mode)


def _stable_identity(metadata: os.stat_result) -> tuple[int, int, int, int, int, int]:
    return (
        *_object_identity(metadata),
        metadata.st_size,
        metadata.st_mtime_ns,
        metadata.st_ctime_ns,
    )


def _read_limited(descriptor: int, limit: int) -> tuple[bytes, int]:
    chunks: list[bytes] = []
    size = 0
    while size <= limit:
        chunk = os.read(descriptor, min(1024 * 1024, limit + 1 - size))
        if not chunk:
            break
        chunks.append(chunk)
        size += len(chunk)
    if size > limit:
        raise SnapshotIoError(f"compilation database exceeds {limit} byte limit")
    return b"".join(chunks), size


def _named_metadata_after_read(candidate: Path) -> os.stat_result:
    try:
        return os.lstat(candidate)
    except OSError as error:
        raise SnapshotIoError("compilation database changed while it was being read") from error


def _validate_unchanged(
    before: os.stat_result,
    after: os.stat_result,
    named_after: os.stat_result,
    size: int,
) -> None:
    if (
        _stable_identity(before) != _stable_identity(after)
        or _object_identity(after) != _object_identity(named_after)
        or size != after.st_size
    ):
        raise SnapshotIoError("compilation database changed while it was being read")


def read_bounded_regular(path: Path, limit: int) -> tuple[Path, bytes]:
    """Read one unchanged regular file without following its final symlink."""

    candidate = Path(path).absolute()
    flags = (
        os.O_RDONLY
        | getattr(os, "O_BINARY", 0)
        | getattr(os, "O_CLOEXEC", 0)
        | getattr(os, "O_NONBLOCK", 0)
        | getattr(os, "O_NOFOLLOW", 0)
    )
    try:
        named_before = os.lstat(candidate)
        if stat.S_ISLNK(named_before.st_mode):
            raise SnapshotIoError(
                "cannot open compilation database safely: final symbolic links are forbidden"
            )
        descriptor = os.open(candidate, flags)
    except SnapshotIoError:
        raise
    except OSError as error:
        raise SnapshotIoError(f"cannot open compilation database safely: {error}") from error
    try:
        before = os.fstat(descriptor)
        if _object_identity(named_before) != _object_identity(before):
            raise SnapshotIoError("compilation database changed while it was being opened")
        if not stat.S_ISREG(before.st_mode):
            raise SnapshotIoError("compilation database must be a regular file")
        if before.st_size > limit:
            raise SnapshotIoError(f"compilation database exceeds {limit} byte limit")
        payload, size = _read_limited(descriptor, limit)
        after = os.fstat(descriptor)
        _validate_unchanged(before, after, _named_metadata_after_read(candidate), size)
        return candidate, payload
    except SnapshotIoError:
        raise
    except OSError as error:
        raise SnapshotIoError(f"cannot read compilation database safely: {error}") from error
    finally:
        os.close(descriptor)


def _same_object(first: os.stat_result, second: os.stat_result) -> bool:
    return _object_identity(first) == _object_identity(second)


def _source_metadata(source: Path) -> os.stat_result:
    try:
        return source.stat()
    except OSError as error:
        raise SnapshotIoError(f"cannot inspect protected compilation database: {error}") from error


def _destination_metadata(path: Path) -> os.stat_result | None:
    try:
        return path.stat()
    except FileNotFoundError:
        return None
    except OSError as error:
        raise SnapshotIoError(f"cannot inspect snapshot output safely: {error}") from error


def _reject_protected_alias(destination: Path, source: Path) -> None:
    destination_metadata = _destination_metadata(destination)
    if destination_metadata is not None and _same_object(
        destination_metadata, _source_metadata(source)
    ):
        raise SnapshotIoError("snapshot output must not overwrite the compilation database")


def _open_directory_component(current: int, component: str, flags: int) -> int:
    following = os.open(component, flags, dir_fd=current)
    try:
        os.close(current)
    except OSError:
        with suppress(OSError):
            os.close(following)
        raise
    return following


def _open_parent_no_follow(parent: Path) -> int | None:
    """Anchor an absolute POSIX directory without traversing symlink components."""

    if (
        os.name == "nt"
        or os.open not in os.supports_dir_fd
        or not hasattr(os, "O_DIRECTORY")
        or not getattr(os, "O_NOFOLLOW", 0)
    ):
        return None
    absolute = parent.absolute()
    flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | os.O_DIRECTORY | os.O_NOFOLLOW
    current = os.open(absolute.anchor or os.sep, flags)
    try:
        for component in absolute.parts[1:]:
            current = _open_directory_component(current, component, flags)
        return current
    except OSError:
        with suppress(OSError):
            os.close(current)
        raise


def _metadata_at(parent_descriptor: int, name: str) -> os.stat_result | None:
    try:
        return os.stat(name, dir_fd=parent_descriptor)
    except FileNotFoundError:
        return None


def _reject_protected_alias_at(
    parent_descriptor: int,
    destination_name: str,
    source: Path,
) -> None:
    destination_metadata = _metadata_at(parent_descriptor, destination_name)
    if destination_metadata is not None and _same_object(
        destination_metadata, _source_metadata(source)
    ):
        raise SnapshotIoError("snapshot output must not overwrite the compilation database")


def _temporary_at(parent_descriptor: int, destination_name: str) -> tuple[str, int]:
    flags = (
        os.O_WRONLY
        | os.O_CREAT
        | os.O_EXCL
        | getattr(os, "O_BINARY", 0)
        | getattr(os, "O_CLOEXEC", 0)
    )
    for _ in range(128):
        name = f".{destination_name}.{secrets.token_hex(12)}.tmp"
        try:
            return name, os.open(name, flags, 0o600, dir_fd=parent_descriptor)
        except FileExistsError:
            continue
    raise SnapshotIoError("cannot allocate a unique snapshot temporary file")


def _write_anchored(
    parent_descriptor: int,
    destination_name: str,
    text: str,
    source: Path,
) -> None:
    temporary_name: str | None = None
    try:
        _reject_protected_alias_at(parent_descriptor, destination_name, source)
        temporary_name, descriptor = _temporary_at(parent_descriptor, destination_name)
        try:
            stream = os.fdopen(descriptor, mode="w", encoding="utf-8")
        except (OSError, ValueError):
            with suppress(OSError):
                os.close(descriptor)
            raise
        with stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        _reject_protected_alias_at(parent_descriptor, destination_name, source)
        os.rename(
            temporary_name,
            destination_name,
            src_dir_fd=parent_descriptor,
            dst_dir_fd=parent_descriptor,
        )
        temporary_name = None
    finally:
        if temporary_name is not None:
            with suppress(OSError):
                os.unlink(temporary_name, dir_fd=parent_descriptor)


def _write_portable(destination: Path, text: str, source: Path) -> None:
    """Best-effort fallback for platforms without directory-relative operations."""

    try:
        parent = destination.parent.resolve(strict=True)
    except (OSError, RuntimeError) as error:
        raise SnapshotIoError(f"cannot resolve snapshot output directory: {error}") from error
    anchored_destination = parent / destination.name
    _reject_protected_alias(anchored_destination, source)
    parent_before = parent.stat()
    temporary: Path | None = None
    temporary_identity: tuple[int, int, int] | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            dir=parent,
            prefix=f".{destination.name}.",
            suffix=".tmp",
            encoding="utf-8",
            delete=False,
        ) as stream:
            temporary = Path(stream.name)
            temporary_identity = _object_identity(os.fstat(stream.fileno()))
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
        parent_after = parent.stat()
        if not _same_object(parent_before, parent_after):
            raise SnapshotIoError("snapshot output directory changed while writing")
        temporary_after = os.lstat(temporary)
        if temporary_identity != _object_identity(temporary_after) or not stat.S_ISREG(
            temporary_after.st_mode
        ):
            raise SnapshotIoError("snapshot temporary file changed while writing")
        _reject_protected_alias(anchored_destination, source)
        temporary.replace(anchored_destination)
    finally:
        if temporary is not None:
            with suppress(OSError):
                temporary.unlink(missing_ok=True)


def write_atomic_text(target: Path, text: str, *, protected: Path) -> None:
    """Atomically write text while refusing to replace the source database."""

    destination = Path(target).absolute()
    source = Path(protected).absolute()
    try:
        if destination == source:
            raise SnapshotIoError("snapshot output must not overwrite the compilation database")
        parent_descriptor = _open_parent_no_follow(destination.parent)
        if parent_descriptor is None:
            _write_portable(destination, text, source)
            return
        try:
            _write_anchored(parent_descriptor, destination.name, text, source)
        finally:
            os.close(parent_descriptor)
    except SnapshotIoError:
        raise
    except OSError as error:
        raise SnapshotIoError(f"cannot write snapshot safely: {error}") from error
