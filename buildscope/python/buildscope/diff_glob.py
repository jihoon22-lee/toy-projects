"""Shared slash-aware glob matching for configuration-diff suppressions."""

from __future__ import annotations

import re


def _glob_token(pattern: str, index: int) -> tuple[str, int]:
    character = pattern[index]
    if character != "*":
        return (r"[^/]", index + 1) if character == "?" else (re.escape(character), index + 1)
    if index + 1 >= len(pattern) or pattern[index + 1] != "*":
        return r"[^/]*", index + 1
    if index + 2 < len(pattern) and pattern[index + 2] == "/":
        return r"(?:.*/)?", index + 3
    return r".*", index + 2


def _glob_regular_expression(pattern: str) -> str:
    pieces = [r"^(?:.*/)?"] if "/" not in pattern else ["^"]
    index = 0
    while index < len(pattern):
        fragment, index = _glob_token(pattern, index)
        pieces.append(fragment)
    pieces.append("$")
    return "".join(pieces)


def glob_matches(path: str, pattern: str, *, windows: bool) -> bool:
    """Match the versioned slash-aware suppression glob contract."""

    normalized = path.replace("\\", "/")
    if windows:
        normalized = normalized.casefold()
        pattern = pattern.casefold()
    return re.fullmatch(_glob_regular_expression(pattern), normalized) is not None
