"""Cross-platform lexical path normalization and native status checks."""

from __future__ import annotations

import ntpath
import os
import posixpath
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Any

from buildscope._command import looks_windows_path

_VENDOR_COMPONENTS = frozenset(
    {"_deps", "deps", "external", "externals", "third-party", "third_party", "vendor"}
)
MAX_PATH_CHARS = 1024 * 1024


class PathNormalizationError(ValueError):
    """Raised when a normalized path exceeds the public contract."""


def normalize_lexical(value: str, base: str, style: str) -> str:
    """Normalize a path lexically in its originating platform style."""

    if style == "windows":
        windows_path = PureWindowsPath(value)
        if not windows_path.is_absolute():
            windows_path = PureWindowsPath(base) / windows_path
        return ntpath.normpath(str(windows_path)).replace("\\", "/")
    posix_path = PurePosixPath(value)
    if not posix_path.is_absolute():
        posix_path = PurePosixPath(base) / posix_path
    return posixpath.normpath(posix_path.as_posix())


def _relative_path(path: str, root: str, style: str) -> str | None:
    path_type = PureWindowsPath if style == "windows" else PurePosixPath
    path_parts = path_type(path).parts
    root_parts = path_type(root).parts
    if style == "windows":
        folded_path = tuple(part.casefold() for part in path_parts)
        folded_root = tuple(part.casefold() for part in root_parts)
        if folded_path[: len(folded_root)] != folded_root:
            return None
    elif path_parts[: len(root_parts)] != root_parts:
        return None
    remainder = path_parts[len(root_parts) :]
    return PurePosixPath(*remainder).as_posix() if remainder else "."


def _has_vendor_component(value: str) -> bool:
    return any(part.casefold() in _VENDOR_COMPONENTS for part in PurePosixPath(value).parts)


def is_native_style(style: str) -> bool:
    """Return whether paths in this style are meaningful on the host."""

    return (os.name == "nt" and style == "windows") or (os.name != "nt" and style == "posix")


def _native_record(
    normalized: str, normalized_root: str, expected: str | None
) -> tuple[str, str | None, bool | None]:
    candidate = Path(normalized)
    try:
        resolved = candidate.resolve(strict=False)
        resolved_root = Path(normalized_root).resolve(strict=False)
    except (OSError, RuntimeError):
        return normalized, None, None
    try:
        relative = resolved.relative_to(resolved_root).as_posix() or "."
    except ValueError:
        relative = None
    exists: bool | None = None
    if expected == "file":
        exists = candidate.is_file()
    elif expected == "directory":
        exists = candidate.is_dir()
    elif expected == "path":
        exists = candidate.exists()
    displayed = relative if relative is not None else resolved.as_posix()
    return displayed, relative, exists


def path_record(
    value: str,
    *,
    base: str,
    project_root: str,
    style: str,
    expected: str | None,
) -> dict[str, Any]:
    """Build a canonical public path record without touching foreign paths."""

    normalized = normalize_lexical(value, base, style)
    root_style = "windows" if looks_windows_path(project_root) else "posix"
    normalized_root = normalize_lexical(project_root, project_root, root_style)
    relative: str | None
    exists: bool | None = None
    if root_style != style:
        displayed, relative = normalized, None
    elif is_native_style(style):
        displayed, relative, exists = _native_record(normalized, normalized_root, expected)
    else:
        relative = _relative_path(normalized, normalized_root, style)
        displayed = relative if relative is not None else normalized
    if _has_vendor_component(displayed):
        scope = "vendor"
    elif relative is not None:
        scope = "project"
    else:
        scope = "system"
    if len(displayed) > MAX_PATH_CHARS:
        raise PathNormalizationError("normalized path exceeds the character limit")
    return {"exists": exists, "path": displayed, "scope": scope, "style": style}


def native_mtime(value: str, base: str, style: str) -> float | None:
    """Read a regular native file's mtime, returning unknown for other styles."""

    if not is_native_style(style):
        return None
    candidate = Path(normalize_lexical(value, base, style))
    try:
        return candidate.stat().st_mtime if candidate.is_file() else None
    except OSError:
        return None
