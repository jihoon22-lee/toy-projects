"""Shared runtime-check error types."""

from __future__ import annotations


class RuntimeCheckError(ValueError):
    """A stable, user-facing runtime-check configuration failure."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message
