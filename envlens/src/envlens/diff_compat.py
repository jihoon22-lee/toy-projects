"""Bounded offline version, marker, and wheel compatibility evaluation."""

from __future__ import annotations

import re
from collections.abc import Mapping
from typing import Any, cast

from packaging.markers import InvalidMarker, Marker
from packaging.requirements import InvalidRequirement, Requirement
from packaging.specifiers import InvalidSpecifier, SpecifierSet
from packaging.tags import compatible_tags, mac_platforms, parse_tag
from packaging.utils import canonicalize_name
from packaging.version import InvalidVersion, Version


def _bounded_packaging_text(value: str) -> bool:
    return len(value) <= 8192 and not re.search(r"[0-9]{19}", value)


def compare_versions(left: str, right: str) -> int | None:
    """Compare PEP 440 versions without executing the target environment."""
    if not _bounded_packaging_text(left) or not _bounded_packaging_text(right):
        return None
    try:
        a, b = Version(left), Version(right)
        return (a > b) - (a < b)
    except InvalidVersion:
        return None


def _parse_requirement(requirement: str) -> tuple[str, str, str | None] | None:
    if not isinstance(requirement, str) or not _bounded_packaging_text(requirement):
        return None
    try:
        parsed = Requirement(requirement)
        return (
            str(canonicalize_name(parsed.name)),
            "@" + parsed.url if parsed.url else str(parsed.specifier),
            str(parsed.marker) if parsed.marker else None,
        )
    except (InvalidRequirement, RecursionError):
        return None


def _python_version(identity: Mapping[str, Any]) -> tuple[int, ...] | None:
    value = identity.get("version_info")
    if (
        isinstance(value, list)
        and len(value) >= 3
        and all(isinstance(item, int) and not isinstance(item, bool) for item in value[:3])
        and all(0 <= item <= 99 for item in value[:2])
        and 0 <= value[2] <= 1_000_000
    ):
        return tuple(cast(int, item) for item in value[:3])
    version = identity.get("version")
    if not isinstance(version, str) or not _bounded_packaging_text(version):
        return None
    match = re.match(r"^(\d+)\.(\d+)(?:\.(\d+))?", version)
    parsed = tuple(int(part or 0) for part in match.groups()) if match else None
    return parsed if parsed and parsed[0] <= 99 and parsed[1] <= 99 else None


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


def _marker_matches(marker: str | None, identity: Mapping[str, Any]) -> bool | None:
    if not marker:
        return True
    if not _bounded_packaging_text(marker):
        return None
    values = _marker_values(identity)
    values["implementation_name"] = str(identity.get("implementation", ""))
    if values["implementation_name"] == "cpython":
        values["platform_python_implementation"] = "CPython"
        values["implementation_version"] = values["python_full_version"]
    platform = values.get("sys_platform", "")
    if platform:
        values["os_name"] = "nt" if platform == "win32" else "posix"
    for key in ("platform_release", "platform_system", "platform_version"):
        if isinstance(identity.get(key), str):
            values[key] = str(identity[key])
    values["extra"] = str(identity.get("extra", ""))
    # Do not let packaging's host defaults stand in for missing target evidence.
    unquoted = re.sub(r"(['\"])(?:\\.|(?!\1).)*?\1", "", marker)
    names = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", unquoted)) - {"and", "or", "in", "not"}
    if any(not values.get(name) and name != "extra" for name in names):
        return None
    try:
        return Marker(marker).evaluate(environment=values)
    except (InvalidMarker, InvalidVersion, KeyError, ValueError, RecursionError):
        return None


def satisfies_requires_python(expression: str, identity: Mapping[str, Any]) -> bool | None:
    """Evaluate PEP 440 against the recorded target Python, never the host."""
    if not expression or not _bounded_packaging_text(expression):
        return None
    python = _python_version(identity)
    if python is None:
        return None
    version = str(identity.get("version") or ".".join(map(str, python)))
    try:
        return SpecifierSet(expression).contains(Version(version))
    except (InvalidSpecifier, InvalidVersion):
        return None


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
            aliases = {"manylinux1": (2, 5), "manylinux2010": (2, 12), "manylinux2014": (2, 17)}
            parsed = re.fullmatch(r"(manylinux|musllinux)_(\d+)_(\d+)_(.+)", platform_tag)
            prefix, _, arch = platform_tag.partition("_")
            if parsed:
                family, major, minor, arch = parsed.groups()
                floor = (int(major), int(minor))
            elif prefix in aliases:
                family, floor = "manylinux", aliases[prefix]
            else:
                return None
            if machine and arch != machine:
                return False
            libc = str(identity.get("libc_name", "")).lower()
            version = re.fullmatch(r"(\d+)\.(\d+)(?:\.\d+)?", str(identity.get("libc_version", "")))
            if not libc or not version or not machine:
                return None
            if family == "manylinux" and libc not in {"glibc", "gnu libc"}:
                return False
            if family == "musllinux" and libc != "musl":
                return False
            return tuple(map(int, version.groups())) >= floor
        if platform_tag.startswith("linux_") and machine:
            return platform_tag == f"linux_{machine}"
        return None
    if platform_name == "darwin":
        version = re.match(r"^(\d+)\.(\d+)", str(identity.get("macos_version", "")))
        if not version or not machine:
            return None
        return platform_tag in set(
            mac_platforms((int(version.group(1)), int(version.group(2))), machine)
        )
    return None


def _wheel_tag_match(tag: str, identity: Mapping[str, Any]) -> bool | None:
    if len(tag) > 1024 or tag.count(".") > 24:
        return None
    try:
        tags = parse_tag(tag)
    except ValueError:
        return None
    python_version = _python_version(identity)
    if python_version is None:
        return None
    major, minor = python_version[:2]
    implementation = str(identity.get("implementation", "")).lower()
    generic = set(compatible_tags(python_version=(major, minor), platforms=["any"]))
    checks: list[bool | None] = []
    for candidate in tags:
        python_tag, abi_tag, platform_tag = candidate.interpreter, candidate.abi, candidate.platform
        if candidate in generic:
            checks.append(True)
            continue
        if python_tag.startswith("py") and abi_tag == "none":
            language = any(item.interpreter == python_tag for item in generic)
            checks.append(_platform_tag_match(platform_tag, identity) if language else False)
            continue
        if major == 3 and minor >= 13 and "free_threaded" not in identity:
            checks.append(None)
            continue
        match = _python_tag_match(python_tag, major, minor, implementation)
        if abi_tag == "abi3" and python_tag.startswith("cp") and python_tag[2:].isdigit():
            floor = python_tag[2:]
            match = (
                implementation == "cpython"
                and len(floor) >= 2
                and (int(floor[0]), int(floor[1:])) <= (major, minor)
            )
            if identity.get("free_threaded"):
                match = False
        elif match is True:
            match = _abi_tag_match(
                abi_tag,
                f"cp{major}{minor}" + ("t" if identity.get("free_threaded") else ""),
                implementation,
            )
        checks.append(_platform_tag_match(platform_tag, identity) if match is True else match)
    return True if True in checks else None if None in checks else False


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
