"""Source-preserving systemd syntax subset; never uses shell evaluation."""

from __future__ import annotations

import re
from typing import Any

from .model import InputError, Limits, diagnostic

_ESCAPES = {
    "a": "\a",
    "b": "\b",
    "f": "\f",
    "n": "\n",
    "r": "\r",
    "t": "\t",
    "v": "\v",
    "s": " ",
    "\\": "\\",
    '"': '"',
    "'": "'",
}


def words(value: str) -> list[str]:
    result: list[str] = []
    current: list[str] = []
    quote = ""
    active = False
    i = 0
    while i < len(value):
        c = value[i]
        if c == "\\":
            i += 1
            if i >= len(value):
                raise InputError("trailing escape")
            c = value[i]
            if c in _ESCAPES:
                current.append(_ESCAPES[c])
            elif c in "xuU" or c in "01234567":
                length = {"x": 2, "u": 4, "U": 8}.get(c, 3)
                start = i + 1 if c in "xuU" else i
                digits = value[start : start + length]
                base = 16 if c in "xuU" else 8
                try:
                    number = int(digits, base)
                    if len(digits) != length or number == 0 or 0xD800 <= number <= 0xDFFF:
                        raise ValueError
                    current.append(chr(number))
                except (ValueError, OverflowError) as exc:
                    raise InputError("invalid character escape") from exc
                i = start + length - 1
            else:
                raise InputError("unsupported escape sequence")
            active = True
        elif quote:
            if c == quote:
                quote = ""
            else:
                current.append(c)
        elif c in "\"'":
            # systemd permits quotes at item boundaries, not shell-style concatenation.
            if current:
                raise InputError("quote inside an unquoted item")
            quote = c
            active = True
        elif c.isspace():
            if active:
                result.append("".join(current))
                current = []
                active = False
        else:
            current.append(c)
            active = True
        i += 1
    if quote:
        raise InputError("unterminated quote")
    if active:
        result.append("".join(current))
    return result


def parse_unit(
    text: str, path: str, limits: Limits
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    records: list[dict[str, Any]] = []
    issues: list[dict[str, Any]] = []
    section = ""
    buffer = ""
    start = 1
    lines = text.splitlines()
    for index, physical in enumerate(lines, 1):
        if len(physical.encode()) > limits.line_bytes:
            raise InputError("physical line limit exceeded")
        stripped = physical.strip()
        if stripped.startswith(("#", ";")):
            continue
        if not buffer:
            start = index
        trailing = len(physical) - len(physical.rstrip("\\"))
        continued = trailing % 2 == 1
        buffer += physical[:-1] + " " if continued else physical
        if len(buffer.encode()) > limits.line_bytes:
            raise InputError("logical line limit exceeded")
        if continued:
            continue
        line = buffer.strip()
        buffer = ""
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        if not section or "=" not in line:
            issues.append(
                diagnostic(
                    "invalid-line",
                    "Expected section and key=value",
                    severity="error",
                    path=path,
                    line=start,
                )
            )
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        if not re.fullmatch(r"[A-Za-z][A-Za-z0-9-]*", key):
            issues.append(
                diagnostic(
                    "invalid-key", "Invalid directive name", severity="error", path=path, line=start
                )
            )
            continue
        records.append(
            {
                "section": section,
                "key": key,
                "value": value.strip(),
                "path": path,
                "line": start,
                "end_line": index,
            }
        )
        if len(records) > limits.directives:
            raise InputError("directive limit exceeded")
    if buffer:
        issues.append(
            diagnostic(
                "unfinished-continuation",
                "Unfinished continuation",
                severity="error",
                path=path,
                line=start,
            )
        )
    return records, issues


def environment_file(text: str, path: str, limits: Limits) -> list[dict[str, Any]]:
    """EnvironmentFile has POSIX-like quoting, but no variable/command expansion."""
    if any(
        ord(c) == 0xFEFF or ord(c) & 0xFFFF in (0xFFFE, 0xFFFF) or 0xFDD0 <= ord(c) <= 0xFDEF
        for c in text
    ):
        raise InputError("invalid environment file Unicode character")
    result: list[dict[str, Any]] = []
    lines = text.splitlines(keepends=True)
    index = 0
    while index < len(lines):
        start = index + 1
        line = lines[index]
        index += 1
        if len(line.encode()) > limits.line_bytes:
            raise InputError("environment line limit exceeded")
        stripped = line.lstrip()
        if not stripped or stripped.startswith(("#", ";")) or "=" not in line:
            continue
        name, value = line.split("=", 1)
        name = name.strip()
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            raise InputError("invalid environment variable name")
        value = value.lstrip(" \t\r")
        quote = value[0] if value.startswith(("'", '"')) else ""
        output: list[str] = []
        pos = 1 if quote else 0
        closed = not quote
        while True:
            if pos >= len(value):
                if quote and not closed:
                    if index >= len(lines):
                        raise InputError("unterminated environment quote")
                    value += lines[index]
                    index += 1
                    if len(value.encode()) > limits.line_bytes:
                        raise InputError("environment logical line limit exceeded")
                    continue
                break
            c = value[pos]
            if quote and c == quote:
                closed = True
                if value[pos + 1 :].strip():
                    raise InputError("text after environment quoted value")
                break
            if c == "\\" and quote != "'":
                pos += 1
                if pos >= len(value):
                    raise InputError("trailing environment escape")
                escaped = value[pos]
                if escaped == "\n":
                    if index < len(lines):
                        value += lines[index]
                        index += 1
                        if len(value.encode()) > limits.line_bytes:
                            raise InputError("environment logical line limit exceeded")
                elif quote == '"' and escaped not in '\\`$"':
                    output.extend(("\\", escaped))
                else:
                    output.append(escaped)
            elif c == "\n" and not quote:
                break
            else:
                output.append(c)
            pos += 1
        parsed = "".join(output)
        if not quote:
            parsed = parsed.rstrip(" \t\r")
        result.append({"name": name, "value": parsed, "path": path, "line": start})
        if len(result) > limits.directives:
            raise InputError("environment assignment limit exceeded")
    return result
