"""Bounded Python-source enumeration for runtime checks."""

from __future__ import annotations

import os
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from envlens.runtime_types import RuntimeCheckError


@dataclass
class SourceCollection:
    files: list[Path]
    seen: set[tuple[int, int]]
    total_bytes: int = 0
    entries_seen: int = 0


_SKIP_DIRECTORIES = {".git", ".hg", ".svn", ".tox", ".venv", "venv", "__pycache__"}


def _record_entry(
    entry: os.DirEntry[str],
    state: SourceCollection,
    pending: list[Path],
    candidates: list[Path],
    max_entries: int,
) -> None:
    state.entries_seen += 1
    if state.entries_seen > max_entries:
        raise RuntimeCheckError(
            "source-too-large",
            f"source tree exceeds {max_entries} directory entries",
        )
    if entry.is_symlink():
        return
    if entry.is_dir(follow_symlinks=False):
        if entry.name not in _SKIP_DIRECTORIES:
            pending.append(Path(entry.path))
        return
    if entry.is_file(follow_symlinks=False) and entry.name.endswith(".py"):
        candidates.append(Path(entry.path))


def _scan_directory(
    directory: Path,
    state: SourceCollection,
    pending: list[Path],
    candidates: list[Path],
    max_entries: int,
) -> None:
    try:
        with os.scandir(directory) as directory_entries:
            for entry in directory_entries:
                _record_entry(entry, state, pending, candidates, max_entries)
    except OSError as error:
        raise RuntimeCheckError("source-read-failed", str(error)) from error


def _directory_python_files(
    path: Path,
    state: SourceCollection,
    max_entries: int,
) -> list[Path]:
    candidates: list[Path] = []
    pending = [path]
    while pending:
        directory = pending.pop()
        _scan_directory(directory, state, pending, candidates, max_entries)
    return candidates


def _record_python_file(
    candidate: Path,
    state: SourceCollection,
    max_files: int,
    max_bytes: int,
) -> None:
    try:
        stat = candidate.stat()
    except OSError as error:
        raise RuntimeCheckError("source-read-failed", str(error)) from error
    identity = (stat.st_dev, stat.st_ino)
    if identity in state.seen:
        return
    state.seen.add(identity)
    state.total_bytes += stat.st_size
    if state.total_bytes > max_bytes:
        raise RuntimeCheckError("source-too-large", "Python source exceeds 64 MiB")
    state.files.append(candidate)
    if len(state.files) > max_files:
        raise RuntimeCheckError("source-too-large", "Python source exceeds 10000 files")


def collect_python_files(
    paths: Sequence[Path],
    *,
    max_files: int,
    max_bytes: int,
    max_entries: int,
) -> list[Path]:
    state = SourceCollection([], set())
    for path in paths:
        candidates: list[Path]
        if path.is_symlink():
            continue
        if path.is_file():
            candidates = [path] if path.suffix == ".py" else []
        elif path.is_dir():
            candidates = _directory_python_files(path, state, max_entries)
        else:
            continue
        for candidate in candidates:
            _record_python_file(candidate, state, max_files, max_bytes)
    state.files.sort(key=str)
    return state.files
