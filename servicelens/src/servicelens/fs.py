"""Bounded rootfs access. Absolute links are interpreted inside the supplied root."""

from __future__ import annotations

import fnmatch
import os
import stat
from pathlib import Path, PurePosixPath
from typing import Any

from .model import InputError, Limits


class RootFS:
    def __init__(self, root: str | Path, limits: Limits) -> None:
        self.root = Path(root).absolute()
        self.limits = limits
        self.bytes_read = 0
        self.entries_seen = 0
        self.directives_seen = 0
        self.files: dict[str, dict[str, Any]] = {}
        self.identities: set[tuple[int, int]] = set()
        self.fd = os.open(self.root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)

    def close(self) -> None:
        os.close(self.fd)

    def _directory(self, parts: list[str]) -> int:
        fd = os.dup(self.fd)
        try:
            for part in parts:
                nxt = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
                os.close(fd)
                fd = nxt
            return fd
        except BaseException:
            os.close(fd)
            raise

    def resolve(self, path: str, *, allow_mask: bool = False) -> str:
        if not path.startswith("/") or "\x00" in path:
            raise InputError("rootfs paths must be absolute and contain no NUL")
        pending = list(PurePosixPath(path).parts[1:])
        done: list[str] = []
        hops = 0
        while pending:
            part = pending.pop(0)
            if part in ("", "."):
                continue
            if part == "..":
                if not done:
                    raise InputError("path escapes the rootfs")
                done.pop()
                continue
            fd = self._directory(done)
            try:
                info = os.stat(part, dir_fd=fd, follow_symlinks=False)
                if stat.S_ISLNK(info.st_mode):
                    hops += 1
                    if hops > self.limits.symlink_hops:
                        raise InputError("symlink loop or hop limit exceeded")
                    target = os.readlink(part, dir_fd=fd)
                    if allow_mask and target == "/dev/null" and not pending:
                        return "/dev/null"
                    if target.startswith("/"):
                        done = []
                    pending = (
                        list(PurePosixPath(target).parts)[int(target.startswith("/")) :] + pending
                    )
                else:
                    done.append(part)
            finally:
                os.close(fd)
        return "/" + "/".join(done)

    def link(self, path: str) -> str | None:
        """Read the final link without following it, including /dev/null masks."""
        parent, name = path.rsplit("/", 1)
        canonical = self.resolve(parent or "/")
        fd = self._directory(canonical.strip("/").split("/") if canonical != "/" else [])
        try:
            info = os.stat(name, dir_fd=fd, follow_symlinks=False)
            return os.readlink(name, dir_fd=fd) if stat.S_ISLNK(info.st_mode) else None
        finally:
            os.close(fd)

    def exists(self, path: str) -> bool:
        try:
            self.resolve(path)
            return True
        except FileNotFoundError:
            return False

    def listdir(self, path: str) -> list[str]:
        canonical = self.resolve(path)
        fd = self._directory(canonical.strip("/").split("/") if canonical != "/" else [])
        try:
            # scandir iterates; unlike listdir it does not allocate an unbounded result first.
            names: list[str] = []
            with os.scandir(fd) as entries:
                for entry in entries:
                    self.entries_seen += 1
                    if self.entries_seen > self.limits.directory_entries:
                        raise InputError("directory entry budget exceeded")
                    names.append(entry.name)
            return sorted(names)
        finally:
            os.close(fd)

    def read(self, path: str) -> str:
        canonical = self.resolve(path)
        parent, name = canonical.rsplit("/", 1)
        directory = self._directory(parent.strip("/").split("/") if parent else [])
        try:
            fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=directory)
        finally:
            os.close(directory)
        try:
            before = os.fstat(fd)
            if not stat.S_ISREG(before.st_mode):
                raise InputError("only regular input files are supported")
            if canonical not in self.files and len(self.files) >= self.limits.files:
                raise InputError("input file count limit exceeded")
            if before.st_size > self.limits.file_bytes:
                raise InputError("input file byte limit exceeded")
            chunks: list[bytes] = []
            count = 0
            while True:
                chunk = os.read(fd, min(65536, self.limits.file_bytes - count + 1))
                if not chunk:
                    break
                count += len(chunk)
                self.bytes_read += len(chunk)
                if count > self.limits.file_bytes or self.bytes_read > self.limits.total_bytes:
                    raise InputError("input byte budget exceeded")
                chunks.append(chunk)
            after = os.fstat(fd)
            fields = ("st_dev", "st_ino", "st_size", "st_mtime_ns", "st_ctime_ns")
            if any(getattr(before, key) != getattr(after, key) for key in fields):
                raise InputError("input changed while being read")
            self.identities.add((after.st_dev, after.st_ino))
            data = b"".join(chunks).decode("utf-8", errors="strict")
            if "\x00" in data:
                raise InputError("input contains NUL")
            self.files[canonical] = {"path": canonical, "bytes": count}
            return data
        finally:
            os.close(fd)

    def glob(self, pattern: str) -> list[str]:
        if not pattern.startswith("/") or ".." in PurePosixPath(pattern).parts:
            raise InputError("environment file patterns must be absolute without '..'")
        candidates = [""]
        for component in PurePosixPath(pattern).parts[1:]:
            expanded: list[str] = []
            for base in candidates:
                if any(c in component for c in "*?["):
                    try:
                        names = self.listdir(base or "/")
                    except FileNotFoundError:
                        continue
                    expanded.extend(
                        base + "/" + n
                        for n in names
                        if fnmatch.fnmatchcase(n, component)
                        and (not n.startswith(".") or component.startswith("."))
                    )
                else:
                    expanded.append(base + "/" + component)
                if len(expanded) > self.limits.files:
                    raise InputError("glob match limit exceeded")
            candidates = expanded
        return sorted(p for p in candidates if self.exists(p))
