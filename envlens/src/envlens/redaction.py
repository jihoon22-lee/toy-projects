"""Default-safe redaction for environment snapshots."""

from __future__ import annotations

import re
from collections.abc import Mapping
from typing import Any

REDACTED = "<REDACTED>"
USER_HOME = "<USER_HOME>"
SENSITIVE_NAME_PARTS = (
    "ACCESS_KEY",
    "API_KEY",
    "AUTH",
    "COOKIE",
    "CREDENTIAL",
    "PASSWORD",
    "PRIVATE_KEY",
    "REGISTRY",
    "REPOSITORY",
    "SECRET",
    "TOKEN",
)
SENSITIVE_NAME_SUFFIXES = ("_URL", "_URI")
URL_USERINFO_RE = re.compile(
    r"(?P<scheme>\b[a-z][a-z0-9+.-]*://)(?P<userinfo>[^/@\s]+)@",
    flags=re.IGNORECASE,
)
URL_SECRET_QUERY_RE = re.compile(
    r"(?P<prefix>[?&](?:access[_-]?key|access[_-]?token|api[_-]?key|auth|"
    r"credential|password|private[_-]?key|secret|token)=)[^&#\s]*",
    flags=re.IGNORECASE,
)


def is_sensitive_name(name: str) -> bool:
    """Return whether an environment-variable name normally carries a secret."""

    normalized = name.upper().replace("-", "_")
    return any(part in normalized for part in SENSITIVE_NAME_PARTS) or normalized.endswith(
        SENSITIVE_NAME_SUFFIXES
    )


def _redact_url_secrets(value: str) -> str:
    redacted = URL_USERINFO_RE.sub(lambda match: match.group("scheme") + REDACTED + "@", value)
    return URL_SECRET_QUERY_RE.sub(
        lambda match: match.group("prefix") + REDACTED,
        redacted,
    )


def _home_patterns(home: str) -> tuple[re.Pattern[str], ...]:
    variants = {home, home.replace("\\", "/"), home.replace("/", "\\")}
    return tuple(
        re.compile(re.escape(value), flags=re.IGNORECASE)
        for value in sorted(variants, key=len, reverse=True)
        if value and value not in {"/", "\\"}
    )


def redact_text(value: str, homes: tuple[str, ...]) -> str:
    """Replace host/target home paths without changing unrelated text."""

    redacted = _redact_url_secrets(value)
    for home in sorted(set(homes), key=len, reverse=True):
        for pattern in _home_patterns(home):
            redacted = pattern.sub(USER_HOME, redacted)
    return redacted


def redact_value(value: Any, homes: tuple[str, ...]) -> Any:
    """Recursively redact strings while retaining JSON-compatible shapes."""

    if isinstance(value, str):
        return redact_text(value, homes)
    if isinstance(value, list):
        return [redact_value(item, homes) for item in value]
    if isinstance(value, dict):
        return {
            str(key): redact_value(item, homes)
            for key, item in sorted(value.items(), key=lambda pair: str(pair[0]))
        }
    return value


def redact_environment(environment: Mapping[str, str], homes: tuple[str, ...]) -> dict[str, str]:
    """Redact secret values and user paths from a target environment."""

    return {
        name: REDACTED if is_sensitive_name(name) else redact_text(value, homes)
        for name, value in sorted(environment.items())
    }
