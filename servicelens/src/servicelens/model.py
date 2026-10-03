"""Small public contracts. No host service manager is queried."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Any

VERSION = "0.2.0"  # x-release-please-version
SNAPSHOT = "servicelens.snapshot/v1"
DIFF = "servicelens.diff/v1"
SEMANTICS = "systemd-255-subset-v1"


class InputError(ValueError):
    """An invalid, unsupported, inconsistent, or oversized input."""


@dataclass(frozen=True)
class Limits:
    files: int = 4096
    file_bytes: int = 4 * 1024 * 1024
    total_bytes: int = 64 * 1024 * 1024
    line_bytes: int = 64 * 1024
    directives: int = 100000
    nodes: int = 256
    edges: int = 8192
    depth: int = 3
    symlink_hops: int = 32
    directory_entries: int = 100000

    def __post_init__(self) -> None:
        if any(type(v) is not int or v < 1 for v in asdict(self).values()):
            raise InputError("all resource limits must be positive integers")
        if self.depth > 32 or self.symlink_hops > 128:
            raise InputError("depth may not exceed 32; symlink_hops may not exceed 128")


def diagnostic(
    code: str,
    message: str,
    *,
    severity: str = "unknown",
    path: str | None = None,
    line: int | None = None,
) -> dict[str, Any]:
    return {"code": code, "severity": severity, "message": message, "path": path, "line": line}
