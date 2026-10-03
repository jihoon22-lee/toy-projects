"""Bounded input discovery, strict JSON and atomic output."""

from __future__ import annotations

import glob
import hashlib
import json
import math
import os
import re
import tempfile
from pathlib import Path
from typing import Any

Document = dict[str, Any]


class InputError(ValueError):
    """An input cannot be interpreted safely or unambiguously."""


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def finite_time(value: str | None) -> float | None:
    try:
        result = float(value) if value is not None else None
        return result if result is not None and math.isfinite(result) and result >= 0 else None
    except ValueError:
        return None


def clean_text(value: str) -> str:
    # Strip OSC/CSI as well as remaining C0 controls from terminal output.
    value = re.sub(r"\x1b\][^\x07]*(?:\x07|\x1b\\)", "", value)
    value = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", value)
    return "".join(c for c in value if c in "\n\t" or ord(c) >= 32 and not 127 <= ord(c) < 160)


def read_bytes(path: Path, limit: int) -> bytes:
    try:
        with path.open("rb") as handle:
            data = handle.read(limit + 1)
    except OSError as exc:
        raise InputError(f"Cannot read {path}: {exc}") from exc
    if len(data) > limit:
        raise InputError(f"Input exceeds {limit} bytes: {path}")
    return data


def discover(patterns: list[str], suffix: str, max_files: int = 1024) -> list[Path]:
    paths: dict[str, Path] = {}
    for pattern in patterns:
        matches = [Path(pattern)] if Path(pattern).exists() else map(Path, glob.iglob(pattern))
        found = False
        for match in matches:
            found = True
            candidates = match.rglob(f"*{suffix}") if match.is_dir() else [match]
            for path in candidates:
                if path.is_symlink() or not path.is_file():
                    continue
                paths[str(path.resolve())] = path.resolve()
                if len(paths) > max_files:
                    raise InputError(f"Input file limit exceeded ({max_files})")
        if not found:
            raise InputError(f"No input matches: {pattern}")
    if not paths:
        raise InputError("No input files found")
    return sorted(paths.values())


def load_json(path: Path, limit: int = 256 * 1024 * 1024) -> Document:
    def pairs(items: list[tuple[str, Any]]) -> Document:
        result: Document = {}
        for key, value in items:
            if key in result:
                raise InputError(f"Duplicate JSON key: {key}")
            result[key] = value
        return result

    def invalid(value: str) -> None:
        raise InputError(f"Non-finite JSON number: {value}")

    def number(value: str) -> float:
        parsed = float(value)
        if not math.isfinite(parsed):
            raise InputError(f"Non-finite JSON number: {value}")
        return parsed

    try:
        value = json.loads(
            read_bytes(path, limit),
            object_pairs_hook=pairs,
            parse_constant=invalid,
            parse_float=number,
        )
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise InputError(f"Invalid JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise InputError(f"JSON document must be an object: {path}")
    return value


def json_text(document: Document) -> str:
    return (
        json.dumps(document, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False) + "\n"
    )


def atomic_write(path: Path, text: str, inputs: list[Path]) -> None:
    for source in inputs:
        if path.resolve() == source.resolve() or (
            path.exists() and source.exists() and os.path.samefile(path, source)
        ):
            raise InputError(f"Output conflicts with input: {source}")
    if path.is_symlink():
        raise InputError(f"Refusing symlink output: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            handle.write(text)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)
