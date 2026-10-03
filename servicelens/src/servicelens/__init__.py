"""Read-only, offline systemd configuration analysis."""

from .analysis import inspect
from .model import VERSION as __version__
from .model import InputError, Limits
from .report import check, diff, graph
from .storage import load, save, validate

__all__ = [
    "InputError",
    "Limits",
    "__version__",
    "check",
    "diff",
    "graph",
    "inspect",
    "load",
    "save",
    "validate",
]
