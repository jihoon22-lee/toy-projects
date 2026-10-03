"""Directive-specific systemd 255 scalar grammar, with no runtime default guesses.

Parser families follow v255 load-fragment-gperf.gperf.in. In particular, a blank
boolean, timespan, enum or mode is invalid, not a generic assignment reset.
"""

from __future__ import annotations

import posixpath
import re
from dataclasses import dataclass

from .model import InputError
from .syntax import words

BOOLEAN_DIRECTIVES = {
    "Unit.DefaultDependencies",
    "Unit.StopWhenUnneeded",
    "Unit.RefuseManualStart",
    "Unit.RefuseManualStop",
    "Unit.IgnoreOnIsolate",
    "Service.RemainAfterExit",
    "Service.NoNewPrivileges",
    "Service.PrivateTmp",
    "Service.DynamicUser",
}
ENUM_DIRECTIVES = {
    "Service.Type": {
        "simple",
        "exec",
        "forking",
        "oneshot",
        "dbus",
        "notify",
        "notify-reload",
        "idle",
    },
    "Service.Restart": {
        "no",
        "on-success",
        "on-failure",
        "on-abnormal",
        "on-watchdog",
        "on-abort",
        "always",
    },
    "Service.KillMode": {"control-group", "process", "mixed", "none"},
}
PRINTF_DIRECTIVES = {
    "Unit.Description",
    "Service.User",
    "Service.Group",
    "Service.WorkingDirectory",
    "Service.RootDirectory",
    "Service.PIDFile",
    "Service.BusName",
    "Install.DefaultInstance",
}
DURATION_DIRECTIVES = {"Service.RestartSec", "Service.TimeoutStartSec", "Service.TimeoutStopSec"}
CONTROLLERS = {
    "cpu",
    "cpuacct",
    "cpuset",
    "io",
    "blkio",
    "memory",
    "devices",
    "pids",
    "bpf-firewall",
    "bpf-devices",
    "bpf-foreign",
    "bpf-socket-bind",
    "bpf-restrict-network-interfaces",
}
OUTPUT_MODES = {
    "inherit",
    "null",
    "tty",
    "kmsg",
    "kmsg+console",
    "journal",
    "journal+console",
    "socket",
    "fd",
    "file",
    "append",
    "truncate",
    "syslog",
    "syslog+console",
}
# Longest spellings precede their prefixes, as in extract_multiplier().
_TIME_UNITS = (
    ("seconds", 1000000),
    ("second", 1000000),
    ("sec", 1000000),
    ("s", 1000000),
    ("minutes", 60000000),
    ("minute", 60000000),
    ("min", 60000000),
    ("months", 2629800000000),
    ("month", 2629800000000),
    ("M", 2629800000000),
    ("msec", 1000),
    ("ms", 1000),
    ("m", 60000000),
    ("hours", 3600000000),
    ("hour", 3600000000),
    ("hr", 3600000000),
    ("h", 3600000000),
    ("days", 86400000000),
    ("day", 86400000000),
    ("d", 86400000000),
    ("weeks", 604800000000),
    ("week", 604800000000),
    ("w", 604800000000),
    ("years", 31557600000000),
    ("year", 31557600000000),
    ("y", 31557600000000),
    ("usec", 1),
    ("us", 1),
    ("μs", 1),
    ("µs", 1),
)
_INFINITY = 2**64 - 1
_WHITESPACE = " \t\r\n\v\f"
_NUMBER = re.compile(r"(?:\+?[0-9]+(?:\.[0-9]+)?|\.[0-9]+)")


@dataclass(frozen=True)
class ScalarValue:
    value: str
    action: str = "replace"
    invalid_members: bool = False


def boolean_value(value: str) -> bool | None:
    lowered = value.lower()
    if lowered in {"1", "yes", "y", "true", "t", "on"}:
        return True
    if lowered in {"0", "no", "n", "false", "f", "off"}:
        return False
    return None


def _integer(value: str, maximum: int, base: int = 10) -> int:
    significant = value.lstrip("+0") or "0"
    # Reject out-of-range input before int(), including Python's digit conversion limit.
    if len(significant) > len(str(maximum)):
        raise InputError("numeric value exceeds the supported range")
    parsed = int(significant, base)
    if parsed > maximum:
        raise InputError("numeric value exceeds the supported range")
    return parsed


def parse_timespan(value: str) -> int:
    """Return usec using v255 parse_sec's units, fractions, concatenation and range."""
    if value.strip(_WHITESPACE) == "infinity":
        return _INFINITY
    total = 0
    offset = 0
    found = False
    while offset < len(value):
        while offset < len(value) and value[offset] in _WHITESPACE:
            offset += 1
        if offset == len(value):
            break
        match = _NUMBER.match(value, offset)
        if not match:
            raise InputError("invalid systemd timespan")
        numeric = match[0]
        whole, dot, fraction = numeric.partition(".")
        integer = _integer(whole or "0", 2**63 - 1)
        offset = match.end()
        numeric_end = offset
        while offset < len(value) and value[offset] in _WHITESPACE:
            offset += 1
        multiplier = 1000000
        has_unit = False
        for suffix, factor in _TIME_UNITS:
            if value.startswith(suffix, offset):
                multiplier = factor
                offset += len(suffix)
                has_unit = True
                break
        if not has_unit and offset == numeric_end and offset < len(value):
            raise InputError("invalid timespan suffix or separator")
        if integer >= _INFINITY // multiplier:
            raise InputError("timespan exceeds the supported range")
        contribution = integer * multiplier
        if contribution >= _INFINITY - total:
            raise InputError("timespan exceeds the supported range")
        total += contribution
        if dot:
            place = multiplier // 10
            for digit in fraction:
                contribution = int(digit) * place
                if contribution >= _INFINITY - total:
                    raise InputError("timespan exceeds the supported range")
                total += contribution
                place //= 10
                if not place:
                    break
        found = True
    if not found:
        raise InputError("empty timespan is not a reset")
    return total


def supports_specifiers(dotted: str, raw: str) -> bool:
    return dotted in PRINTF_DIRECTIVES or (
        dotted in {"Service.StandardOutput", "Service.StandardError"}
        and raw.startswith(("fd:", "file:", "append:", "truncate:"))
    )


def _path(value: str, *, absolute: bool = True) -> str:
    if not value or "\0" in value or len(value.encode("utf-8")) >= 4096:
        raise InputError("invalid path")
    if absolute and not value.startswith("/"):
        raise InputError("path must be absolute")
    if ".." in value.split("/") or any(
        len(part.encode("utf-8")) > 255 for part in value.split("/")
    ):
        raise InputError("invalid path component")
    return "/" + posixpath.normpath(value).lstrip("/") if absolute else posixpath.normpath(value)


def _user_group(value: str) -> bool:
    if not value:
        return True
    if re.fullmatch(r"[0-9]+", value):
        if len(value) > 1 and value.startswith("0"):
            return False
        numeric = _integer(value, 2**32 - 1)
        return numeric not in {65535, 2**32 - 1}
    return not (
        value in {".", ".."}
        or re.fullmatch(r"-[0-9]+", value)
        or any(ord(c) < 32 or ord(c) == 127 or c in ":/" for c in value)
        or value.startswith(" ")
        or value.endswith(" ")
    )


def _bus_name(value: str) -> bool:
    if len(value.encode("utf-8")) > 255:
        return False
    unique = value.startswith(":")
    parts = value.removeprefix(":").split(".")
    pattern = r"[A-Za-z0-9_-]+" if unique else r"[A-Za-z_-][A-Za-z0-9_-]*"
    return len(parts) >= 2 and all(re.fullmatch(pattern, part) for part in parts)


def validate_scalar(dotted: str, value: str, previous: str | None = None) -> ScalarValue:
    """Validate a resolved scalar. Values are declaration evidence, not live state."""
    if dotted in BOOLEAN_DIRECTIVES:
        boolean = boolean_value(value)
        if boolean is None:
            raise InputError("expected a systemd boolean; empty does not reset it")
        return ScalarValue("yes" if boolean else "no")
    if dotted in ENUM_DIRECTIVES:
        if value not in ENUM_DIRECTIVES[dotted]:
            raise InputError("unrecognized directive enumeration")
    elif dotted in DURATION_DIRECTIVES:
        duration = parse_timespan(value)
        if not duration and dotted != "Service.RestartSec":
            return ScalarValue("infinity")
    elif dotted == "Service.UMask":
        if not re.fullmatch(r"[0-7]+", value):
            raise InputError("expected an unsigned octal mode")
        return ScalarValue(f"{_integer(value, 0o7777, 8):04o}")
    elif dotted in {"Service.ProtectHome", "Service.ProtectSystem"}:
        boolean = boolean_value(value)
        if boolean is not None:
            return ScalarValue("yes" if boolean else "no")
        modes = {"read-only", "tmpfs"} if dotted.endswith("ProtectHome") else {"full", "strict"}
        if value not in modes:
            raise InputError("unrecognized protection mode")
    elif dotted in {"Service.User", "Service.Group"}:
        if not _user_group(value):
            raise InputError("invalid user/group name or numeric ID")
    elif dotted == "Service.WorkingDirectory":
        if value:
            path = value.removeprefix("-")
            normalized = path if path == "~" else _path(path)
            return ScalarValue(("-" if value.startswith("-") else "") + normalized)
    elif dotted == "Service.RootDirectory":
        if value:
            return ScalarValue(_path(value))
    elif dotted == "Service.PIDFile":
        if value:
            normalized = _path(posixpath.join("/run", value))
            if normalized.startswith("/var/run/"):
                normalized = normalized[4:]
            return ScalarValue(normalized)
    elif dotted == "Service.BusName":
        if not _bus_name(value):
            raise InputError("invalid D-Bus service name")
    elif dotted in {"Service.StandardOutput", "Service.StandardError"}:
        prefix, colon, target = value.partition(":")
        if colon and prefix in {"file", "append", "truncate"}:
            return ScalarValue(prefix + ":" + _path(target))
        if colon and prefix == "fd":
            if len(target) > 255 or any(ord(c) < 32 or ord(c) > 126 or c == ":" for c in target):
                raise InputError("invalid file descriptor name")
        elif value not in OUTPUT_MODES:
            raise InputError("unrecognized standard I/O destination")
    elif dotted == "Service.Delegate":
        boolean = boolean_value(value)
        if boolean is not None:
            return ScalarValue("yes" if boolean else "no")
        if not value:
            return ScalarValue("", "reset")
        controllers = words(value)
        valid_controllers = set(controllers) & CONTROLLERS
        invalid_members = len(valid_controllers) != len(set(controllers))
        if previous == "yes":
            return ScalarValue("yes", "union", invalid_members)
        prior = set(previous.split()) if previous and previous != "no" else set()
        return ScalarValue(" ".join(sorted(prior | valid_controllers)), "union", invalid_members)
    elif dotted == "Install.DefaultInstance":
        if value and not re.fullmatch(r"[A-Za-z0-9:_.@\\-]+", value):
            raise InputError("invalid default instance name")
    elif dotted != "Unit.Description":
        raise InputError("scalar directive has no validator")
    return ScalarValue(value, "reset" if value == "" else "replace")
