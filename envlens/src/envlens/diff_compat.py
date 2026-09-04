"""Bounded offline version, marker, and wheel compatibility evaluation."""

from __future__ import annotations

import re
from collections.abc import Mapping
from typing import Any, cast

_NORMALIZE_NAME_RE = re.compile(r"[-_.]+")
_NUMERIC_VERSION_RE = re.compile(r"^v?\d+(?:\.\d+)*$")
_MAX_VERSION_PART_DIGITS = 18
_REQ_NAME_RE = re.compile(r"^\s*([A-Za-z0-9][A-Za-z0-9._-]*)\s*(.*)$")
_SPECIFIER_RE = re.compile(r"^(==|!=|~=|>=|<=|>|<)\s*([0-9A-Za-z][^,\s]*)\s*$")
_MARKER_RE = re.compile(
    r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(==|!=|<=|>=|<|>|in|not\s+in)\s*(['\"])(.*?)\3\s*$"
)


def _version_tokens(value: str) -> tuple[int, ...] | None:
    """Return a key only for plain numeric release versions."""

    text = value.strip()
    if not _NUMERIC_VERSION_RE.fullmatch(text):
        return None
    parts = text.lstrip("v").split(".")
    if any(len(part) > _MAX_VERSION_PART_DIGITS for part in parts):
        return None
    result = tuple(int(part) for part in parts)
    while len(result) > 1 and result[-1] == 0:
        result = result[:-1]
    return result


def compare_versions(left: str, right: str) -> int | None:
    """Compare numeric release versions, or return unknown for other forms."""

    left_key = _version_tokens(left)
    right_key = _version_tokens(right)
    if left_key is None or right_key is None:
        return None
    if left_key < right_key:
        return -1
    if left_key > right_key:
        return 1
    return 0


def _parse_requirement(requirement: str) -> tuple[str, str, str | None] | None:
    # Extras and direct URLs are legal metadata but do not provide a version
    # constraint that this bounded consumer can solve.
    if not isinstance(requirement, str) or not requirement.strip():
        return None
    name_match = _REQ_NAME_RE.match(requirement)
    if name_match is None:
        return None
    name = _NORMALIZE_NAME_RE.sub("-", name_match.group(1)).lower()
    rest = name_match.group(2).strip()
    marker: str | None = None
    if ";" in rest:
        rest, marker = rest.split(";", 1)
        marker = marker.strip()
    rest = rest.strip()
    if rest.startswith("["):
        end = rest.find("]")
        if end < 0:
            return None
        rest = rest[end + 1 :].strip()
    if rest.startswith("(") and rest.endswith(")"):
        rest = rest[1:-1].strip()
    if rest.startswith("@"):
        return name, "", marker
    return name, rest, marker


def _python_version(identity: Mapping[str, Any]) -> tuple[int, ...] | None:
    value = identity.get("version_info")
    if (
        isinstance(value, list)
        and len(value) >= 3
        and all(isinstance(item, int) for item in value[:3])
    ):
        return tuple(cast(int, item) for item in value[:3])
    version = identity.get("version")
    if not isinstance(version, str):
        return None
    match = re.match(r"^(\d+)\.(\d+)(?:\.(\d+))?", version)
    return tuple(int(part or 0) for part in match.groups()) if match else None


def _version_tuple(value: str) -> tuple[int, ...] | None:
    match = re.fullmatch(r"\s*v?(\d+(?:\.\d+)*)\s*", value)
    if match is None:
        return None
    parts = match.group(1).split(".")
    if any(len(part) > _MAX_VERSION_PART_DIGITS for part in parts):
        return None
    return tuple(int(part) for part in parts)


def _compare_release(left: tuple[int, ...], right: tuple[int, ...]) -> int:
    width = max(len(left), len(right))
    left_padded = left + (0,) * (width - len(left))
    right_padded = right + (0,) * (width - len(right))
    return (left_padded > right_padded) - (left_padded < right_padded)


def _ordered_match(operator: str, ordering: int) -> bool | None:
    checks = {
        "==": ordering == 0,
        "!=": ordering != 0,
        "<": ordering < 0,
        "<=": ordering <= 0,
        ">": ordering > 0,
        ">=": ordering >= 0,
    }
    return checks.get(operator)


def _release_specifier_match(
    current: tuple[int, ...], operator: str, raw_version: str
) -> bool | None:
    if raw_version.endswith(".*"):
        return _wildcard_match(current, operator, raw_version[:-2])
    required = _version_tuple(raw_version)
    if required is None:
        return None
    ordering = _compare_release(current, required)
    if operator != "~=":
        return _ordered_match(operator, ordering)
    if len(required) < 2:
        return None
    return ordering >= 0 and _compare_release(current, _compatible_upper_bound(required)) < 0


def _wildcard_match(current: tuple[int, ...], operator: str, raw_prefix: str) -> bool | None:
    if operator not in {"==", "!="}:
        return None
    prefix = _version_tuple(raw_prefix)
    if prefix is None:
        return None
    equal = current[: len(prefix)] == prefix
    return equal if operator == "==" else not equal


def _compatible_upper_bound(release: tuple[int, ...]) -> tuple[int, ...]:
    if len(release) <= 1:
        return (release[0] + 1,)
    if len(release) == 2:
        return (release[0] + 1, 0)
    return (*release[:-2], release[-2] + 1, 0)


def _marker_values(identity: Mapping[str, Any]) -> dict[str, str]:
    python = _python_version(identity)
    platform_name = identity.get("platform")
    machine = identity.get("machine")
    return {
        "python_version": (
            f"{python[0]}.{python[1]}" if python is not None and len(python) >= 2 else ""
        ),
        "python_full_version": (
            ".".join(str(part) for part in python) if python is not None else ""
        ),
        "sys_platform": str(platform_name) if isinstance(platform_name, str) else "",
        "platform_machine": str(machine) if isinstance(machine, str) else "",
        "extra": "",
    }


def _marker_comparison(key: str, operator: str, actual: str, expected: str) -> bool | None:
    if operator in {"in", "not in"}:
        return None
    if key in {"python_version", "python_full_version"}:
        actual_release = _version_tuple(actual)
        expected_release = _version_tuple(expected)
        if actual_release is None or expected_release is None:
            return None
        ordering = _compare_release(actual_release, expected_release)
    else:
        ordering = (actual > expected) - (actual < expected)
    return _ordered_match(operator, ordering)


def _marker_matches(marker: str | None, identity: Mapping[str, Any]) -> bool | None:
    if not marker:
        return True
    # The common marker grammar is conjunctions of simple comparisons.  More
    # complex parentheses/OR expressions are intentionally marked uncertain.
    if " or " in marker.lower() or "(" in marker or ")" in marker:
        return None
    values = _marker_values(identity)
    for raw_clause in re.split(r"\s+and\s+", marker, flags=re.IGNORECASE):
        match = _MARKER_RE.match(raw_clause)
        if match is None:
            return None
        key, operator, expected = match.group(1), match.group(2), match.group(4)
        actual = values.get(key)
        if actual is None:
            return None
        result = _marker_comparison(key, operator, actual, expected)
        if result is not True:
            return result
    return True


def satisfies_requires_python(expression: str, identity: Mapping[str, Any]) -> bool | None:
    """Evaluate the common PEP 440 ``Requires-Python`` subset offline."""

    if not expression:
        return None
    python = _python_version(identity)
    if python is None:
        return None
    current = tuple(python)
    for raw_part in expression.split(","):
        part = raw_part.strip()
        if not part:
            continue
        match = _SPECIFIER_RE.match(part)
        if match is None:
            return None
        result = _release_specifier_match(current, *match.groups())
        if result is not True:
            return result
    return True


def _dist_wheel_tags(distribution: Mapping[str, Any]) -> tuple[list[str], bool]:
    metadata = distribution.get("metadata")
    value = (
        metadata.get("wheel_tags", distribution.get("wheel_tags"))
        if isinstance(metadata, dict)
        else distribution.get("wheel_tags")
    )
    if not isinstance(value, list):
        return [], False
    tags = sorted({str(item) for item in value if isinstance(item, str) and item})
    return tags, True


def _project_values(project: Mapping[str, Any] | None) -> tuple[str, list[str], list[str]]:
    if not project:
        return "", [], []
    nested = project.get("project")
    metadata = nested if isinstance(nested, dict) else project
    requires_python = metadata.get("requires_python", metadata.get("requires-python", ""))
    dependencies = metadata.get("dependencies", metadata.get("requires_dist", []))
    wheels = metadata.get("wheel_tags", [])
    return (
        str(requires_python) if isinstance(requires_python, str) else "",
        [str(item) for item in dependencies] if isinstance(dependencies, list) else [],
        [str(item) for item in wheels] if isinstance(wheels, list) else [],
    )


def _python_tag_match(python_tag: str, major: int, minor: int, implementation: str) -> bool | None:
    cp_tag = f"cp{major}{minor}"
    if python_tag == "py3":
        return major == 3
    if python_tag == f"py{major}{minor}":
        return True
    if python_tag.startswith("py") and python_tag[2:].isdigit():
        return False
    if python_tag == cp_tag:
        return implementation == "cpython"
    if python_tag.startswith("cp") and python_tag[2:].isdigit():
        return False if implementation == "cpython" else None
    return None


def _abi_tag_match(abi_tag: str, cp_tag: str, implementation: str) -> bool | None:
    if abi_tag == "none":
        return True
    if abi_tag == cp_tag:
        return implementation == "cpython"
    if abi_tag == "abi3":
        return None
    return None


def _platform_tag_match(platform_tag: str, identity: Mapping[str, Any]) -> bool | None:
    platform_name = str(identity.get("platform", "")).lower()
    machine = str(identity.get("machine", "")).lower().replace("-", "_")
    if platform_tag == "any":
        return True
    if platform_name.startswith("win"):
        expected = {
            "x86_64": "win_amd64",
            "amd64": "win_amd64",
            "aarch64": "win_arm64",
            "arm64": "win_arm64",
        }.get(machine)
        if platform_tag.startswith("win_") and expected:
            return platform_tag == expected
        return None
    if platform_name.startswith("linux"):
        if platform_tag.startswith(("manylinux", "musllinux")):
            return None
        if platform_tag.startswith("linux_") and machine:
            return platform_tag == f"linux_{machine}"
        return None
    if platform_name == "darwin":
        return None
    return None


def _wheel_tag_match(tag: str, identity: Mapping[str, Any]) -> bool | None:
    parts = tag.split("-")
    if len(parts) != 3 or any(not part or "." in part for part in parts):
        return None
    python_version = _python_version(identity)
    if python_version is None:
        return None
    python_tag, abi_tag, platform_tag = parts
    major, minor = python_version[:2]
    implementation = str(identity.get("implementation", "")).lower()
    python_match = _python_tag_match(python_tag, major, minor, implementation)
    if python_match is False:
        return None if python_tag.startswith("cp") and abi_tag == "abi3" else False
    if python_match is None:
        return None
    abi_match = _abi_tag_match(abi_tag, f"cp{major}{minor}", implementation)
    if abi_match is not True:
        return abi_match
    return _platform_tag_match(platform_tag, identity)


def _compatibility_check(
    label: str,
    expression: str,
    identity: Mapping[str, Any],
    *,
    kind: str,
) -> dict[str, Any]:
    if not expression:
        return {
            "kind": kind,
            "name": label,
            "expression": "",
            "status": "unknown",
            "certainty": "unknown",
            "reason": "metadata does not provide a compatibility expression",
        }
    result = satisfies_requires_python(expression, identity)
    status_reason = {
        True: ("compatible", "interpreter satisfies Requires-Python", "certain"),
        False: ("incompatible", "interpreter does not satisfy Requires-Python", "certain"),
        None: (
            "unknown",
            "Requires-Python uses a syntax outside the bounded evaluator",
            "unknown",
        ),
    }[result]
    return {
        "kind": kind,
        "name": label,
        "expression": expression,
        "interpreter": identity.get("version", ""),
        "status": status_reason[0],
        "certainty": status_reason[2],
        "reason": status_reason[1],
    }


def _wheel_compatibility(
    label: str, tags: list[str], identity: Mapping[str, Any], *, kind: str = "wheel"
) -> dict[str, Any]:
    if not tags:
        return {
            "kind": kind,
            "name": label,
            "tags": [],
            "status": "unknown",
            "certainty": "unknown",
            "reason": "no wheel tag evidence is present",
        }
    matches = [_wheel_tag_match(tag, identity) for tag in tags]
    if any(match is True for match in matches):
        status, certainty, reason = "compatible", "certain", "at least one wheel tag matches"
    elif all(match is False for match in matches):
        status, certainty, reason = "incompatible", "certain", "no wheel tag matches the target"
    else:
        status, certainty, reason = (
            "unknown",
            "unknown",
            "one or more wheel tags are not recognized",
        )
    return {
        "kind": kind,
        "name": label,
        "tags": tags,
        "status": status,
        "certainty": certainty,
        "platform": identity.get("platform", ""),
        "machine": identity.get("machine", ""),
        "reason": reason,
    }
