"""Offline snapshot comparison and compatibility evidence.

The comparison code intentionally does not resolve or download anything.  It
uses only the metadata captured in ``envlens.snapshot/v1`` and, optionally, a
small project metadata mapping supplied by :mod:`envlens.project`.
"""

from __future__ import annotations

import json
import os
import re
from collections.abc import Mapping
from typing import Any, cast

from envlens.snapshot_input import (
    MAX_REQUIREMENTS,
    DiffError,
    _object,
    load_snapshot,
    validate_snapshot,
)

MAX_COMPATIBILITY_ITEMS = 100_000

__all__ = [
    "DiffError",
    "check_compatibility",
    "check_snapshot_compatibility",
    "compare_snapshots",
    "compare_versions",
    "diff_snapshots",
    "load_snapshot",
    "satisfies_requires_python",
    "validate_snapshot",
]


_NORMALIZE_NAME_RE = re.compile(r"[-_.]+")


def _normalized_name(distribution: Mapping[str, Any]) -> str:
    value = distribution.get("normalized_name")
    if isinstance(value, str) and value:
        return value
    name = distribution.get("name", "")
    return _NORMALIZE_NAME_RE.sub("-", str(name)).lower()


_NUMERIC_VERSION_RE = re.compile(r"^v?\d+(?:\.\d+)*$")


def _version_tokens(value: str) -> tuple[int, ...] | None:
    """Return a key only for plain numeric release versions.

    Pre/dev/post releases, epochs, local labels, and alternate separators are
    deliberately outside this bounded evaluator.  Treating those forms as
    unknown is safer than claiming an incorrect PEP 440 ordering.
    """

    text = value.strip()
    if not _NUMERIC_VERSION_RE.fullmatch(text):
        return None
    result = tuple(int(part) for part in text.lstrip("v").split("."))
    while len(result) > 1 and result[-1] == 0:
        result = result[:-1]
    return result


def compare_versions(left: str, right: str) -> int | None:
    """Compare two version strings, returning ``None`` when ordering is unsafe."""

    left_key = _version_tokens(left)
    right_key = _version_tokens(right)
    if left_key is None or right_key is None:
        return None
    if left_key < right_key:
        return -1
    if left_key > right_key:
        return 1
    return 0


_REQ_NAME_RE = re.compile(r"^\s*([A-Za-z0-9][A-Za-z0-9._-]*)\s*(.*)$")
_SPECIFIER_RE = re.compile(r"^(==|!=|~=|>=|<=|>|<)\s*([0-9A-Za-z][^,\s]*)\s*$")
_MARKER_RE = re.compile(
    r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*(==|!=|<=|>=|<|>|in|not\s+in)\s*(['\"])(.*?)\3\s*$"
)


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
    if isinstance(version, str):
        match = re.match(r"^(\d+)\.(\d+)(?:\.(\d+))?", version)
        if match:
            return tuple(int(part or 0) for part in match.groups())
    return None


def _version_tuple(value: str) -> tuple[int, ...] | None:
    match = re.fullmatch(r"\s*v?(\d+(?:\.\d+)*)\s*", value)
    if match is None:
        return None
    return tuple(int(part) for part in match.group(1).split("."))


def _compare_release(left: tuple[int, ...], right: tuple[int, ...]) -> int:
    width = max(len(left), len(right))
    left_padded = left + (0,) * (width - len(left))
    right_padded = right + (0,) * (width - len(right))
    return (left_padded > right_padded) - (left_padded < right_padded)


def _marker_matches(marker: str | None, identity: Mapping[str, Any]) -> bool | None:
    if not marker:
        return True
    # The common marker grammar is conjunctions of simple comparisons.  More
    # complex parentheses/OR expressions are intentionally marked uncertain.
    if " or " in marker.lower() or "(" in marker or ")" in marker:
        return None
    python = _python_version(identity)
    platform_name = identity.get("platform")
    machine = identity.get("machine")
    values: dict[str, str] = {
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
    for raw_clause in re.split(r"\s+and\s+", marker, flags=re.IGNORECASE):
        match = _MARKER_RE.match(raw_clause)
        if match is None:
            return None
        key, operator, expected = match.group(1), match.group(2), match.group(4)
        actual = values.get(key)
        if actual is None:
            return None
        if operator in {"in", "not in"}:
            candidates = [part.strip() for part in expected.split(",")]
            result = actual in candidates
            if operator == "not in":
                result = not result
        elif key in {"python_version", "python_full_version"}:
            actual_release = _version_tuple(actual)
            expected_release = _version_tuple(expected)
            if actual_release is None or expected_release is None:
                return None
            ordering = _compare_release(actual_release, expected_release)
            result = {
                "==": ordering == 0,
                "!=": ordering != 0,
                "<": ordering < 0,
                "<=": ordering <= 0,
                ">": ordering > 0,
                ">=": ordering >= 0,
            }[operator]
        else:
            result = {
                "==": actual == expected,
                "!=": actual != expected,
                "<": actual < expected,
                "<=": actual <= expected,
                ">": actual > expected,
                ">=": actual >= expected,
            }[operator]
        if not result:
            return False
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
        operator, raw_version = match.groups()
        if raw_version.endswith(".*"):
            prefix = _version_tuple(raw_version[:-2])
            if prefix is None:
                return None
            equal = current[: len(prefix)] == prefix
            if operator == "==" and not equal:
                return False
            if operator == "!=" and equal:
                return False
            if operator not in {"==", "!="}:
                return None
            continue
        required = _version_tuple(raw_version)
        if required is None:
            return None
        ordering = _compare_release(current, required)
        if operator == "==" and ordering != 0:
            return False
        if operator == "!=" and ordering == 0:
            return False
        if operator == ">=" and ordering < 0:
            return False
        if operator == "<=" and ordering > 0:
            return False
        if operator == ">" and ordering <= 0:
            return False
        if operator == "<" and ordering >= 0:
            return False
        if operator == "~=" and ordering < 0:
            return False
        if operator == "~=":
            upper = _compatible_upper_bound(required)
            if _compare_release(current, upper) >= 0:
                return False
    return True


def _dist_import_names(distribution: Mapping[str, Any]) -> tuple[list[str], bool]:
    value = distribution.get("import_names")
    if not isinstance(value, list):
        value = _object(distribution.get("metadata"), "distribution.metadata").get("import_names")
    if not isinstance(value, list):
        return [], False
    names = sorted({str(item) for item in value if isinstance(item, str) and item})
    return names, True


def _dist_wheel_tags(distribution: Mapping[str, Any]) -> tuple[list[str], bool]:
    metadata = distribution.get("metadata")
    if not isinstance(metadata, dict):
        value = distribution.get("wheel_tags")
    else:
        value = metadata.get("wheel_tags", distribution.get("wheel_tags"))
    if not isinstance(value, list):
        return [], False
    tags = sorted({str(item) for item in value if isinstance(item, str) and item})
    return tags, True


def _dist_public(distribution: Mapping[str, Any]) -> dict[str, Any]:
    result: dict[str, Any] = {
        "name": str(distribution.get("name", "")),
        "normalized_name": _normalized_name(distribution),
        "version": str(distribution.get("version", "")),
        "location": str(distribution.get("location", "")),
    }
    imports, available = _dist_import_names(distribution)
    if available:
        result["import_names"] = imports
    metadata = distribution.get("metadata")
    if isinstance(metadata, dict):
        requires_python = metadata.get("requires_python")
        if isinstance(requires_python, str) and requires_python:
            result["requires_python"] = requires_python
        wheel_tags, wheel_available = _dist_wheel_tags(distribution)
        if wheel_available:
            result["wheel_tags"] = wheel_tags
    return result


def _group_distributions(snapshot: Mapping[str, Any]) -> dict[str, list[Mapping[str, Any]]]:
    grouped: dict[str, list[Mapping[str, Any]]] = {}
    for raw in cast(list[Any], snapshot.get("distributions", [])):
        distribution = _object(raw, "distribution")
        grouped.setdefault(_normalized_name(distribution), []).append(distribution)
    for items in grouped.values():
        items.sort(
            key=lambda item: (str(item.get("version", "")), json.dumps(item, sort_keys=True))
        )
    return grouped


def _certainty(snapshot: Mapping[str, Any]) -> str:
    collection = snapshot.get("collection")
    if not isinstance(collection, dict) or collection.get("status") != "complete":
        return "unknown"
    distributions = snapshot.get("distributions")
    if not isinstance(distributions, list):
        return "unknown"
    if any(
        isinstance(item, dict) and item.get("status") not in {None, "ok"} for item in distributions
    ):
        return "unknown"
    if any(len(items) > 1 for items in _group_distributions(snapshot).values()):
        return "unknown"
    return "certain"


def _identity(snapshot: Mapping[str, Any]) -> Mapping[str, Any]:
    source = snapshot.get("source")
    if not isinstance(source, dict) or not isinstance(source.get("identity"), dict):
        return {}
    return cast(Mapping[str, Any], source["identity"])


def _interpreter_summary(snapshot: Mapping[str, Any]) -> dict[str, Any]:
    source = snapshot.get("source")
    if not isinstance(source, dict):
        return {}
    identity = source.get("identity") if isinstance(source.get("identity"), dict) else {}
    result = {
        "captured_at": snapshot.get("captured_at", ""),
        "requested_executable": source.get("requested_executable", ""),
        "resolved_executable": source.get("resolved_executable", ""),
    }
    if isinstance(identity, dict):
        result["identity"] = {
            key: identity.get(key, "")
            for key in ("implementation", "version", "cache_tag", "platform", "machine")
        }
    return result


def _snapshot_input(value: Mapping[str, Any] | str | os.PathLike[str]) -> dict[str, Any]:
    if isinstance(value, Mapping):
        return validate_snapshot(value)
    return load_snapshot(value)


def _with_certainty(value: dict[str, Any], certainty: str) -> dict[str, Any]:
    value["certainty"] = certainty
    return value


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


def _wheel_tag_match(tag: str, identity: Mapping[str, Any]) -> bool | None:
    parts = tag.split("-")
    if len(parts) != 3 or any(not part for part in parts):
        return None
    python_tag, abi_tag, platform_tag = parts
    # A dotted field represents a compound tag.  The snapshot stores only the
    # WHEEL record, not the full packaging tag set, so refusing to choose one
    # candidate avoids a false certain result.
    if any("." in part for part in parts):
        return None
    python_version = _python_version(identity)
    if python_version is None:
        return None
    major, minor = python_version[:2]
    implementation = str(identity.get("implementation", "")).lower()
    cp_tag = f"cp{major}{minor}"
    if python_tag == "py3":
        python_match: bool | None = major == 3
    elif python_tag == f"py{major}{minor}":
        python_match = True
    elif python_tag.startswith("py") and python_tag[2:].isdigit():
        python_match = False
    elif python_tag == cp_tag:
        python_match = implementation == "cpython"
    elif python_tag.startswith("cp") and python_tag[2:].isdigit():
        python_match = False if implementation == "cpython" else None
    else:
        python_match = None
    if python_match is False:
        if python_tag.startswith("cp") and abi_tag == "abi3":
            return None
        return False
    if python_match is None:
        return None

    if abi_tag == "none":
        abi_match: bool | None = True
    elif abi_tag == cp_tag:
        abi_match = implementation == "cpython"
    elif abi_tag == "abi3":
        # ABI3 compatibility depends on the wheel's minimum CPython ABI and
        # platform support; neither is represented by this snapshot field.
        return None
    else:
        abi_match = None
    if abi_match is False:
        return False
    if abi_match is None:
        return None

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
        # A plain linux tag can be matched from sys.platform/machine.  The
        # glibc/musl family and manylinux policy version require dimensions
        # that the offline identity does not capture.
        if platform_tag.startswith(("manylinux", "musllinux")):
            return None
        if platform_tag.startswith("linux_") and machine:
            return platform_tag == f"linux_{machine}"
        return None
    if platform_name == "darwin":
        return None
    return None


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
    if result is True:
        status, reason = "compatible", "interpreter satisfies Requires-Python"
        certainty = "certain"
    elif result is False:
        status, reason = "incompatible", "interpreter does not satisfy Requires-Python"
        certainty = "certain"
    else:
        status, reason = "unknown", "Requires-Python uses a syntax outside the bounded evaluator"
        certainty = "unknown"
    return {
        "kind": kind,
        "name": label,
        "expression": expression,
        "interpreter": identity.get("version", ""),
        "status": status,
        "certainty": certainty,
        "reason": reason,
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


def _dependency_issue(
    *,
    name: str,
    requirement: str,
    installed: list[Mapping[str, Any]],
    certainty: str,
    source: str,
    marker_result: bool | None = True,
) -> dict[str, Any] | None:
    parsed = _parse_requirement(requirement)
    if parsed is None:
        return {
            "kind": "unknown-requirement",
            "name": name,
            "requirement": requirement,
            "installed": [],
            "certainty": "unknown",
            "source": source,
            "reason": "requirement syntax is outside the bounded evaluator",
        }
    normalized, specifier, _marker = parsed
    if marker_result is False:
        return None
    if marker_result is None:
        return {
            "kind": "unknown-requirement",
            "name": normalized,
            "requirement": requirement,
            "installed": [str(item.get("version", "")) for item in installed],
            "certainty": "unknown",
            "source": source,
            "reason": "environment marker could not be evaluated offline",
        }
    if not installed:
        return {
            "kind": "missing",
            "name": normalized,
            "requirement": requirement,
            "installed": [],
            "certainty": certainty,
            "source": source,
            "reason": "no installed distribution with this normalized project name",
        }
    versions = [str(item.get("version", "")) for item in installed]
    if not specifier:
        return None
    checks: list[bool | None] = []
    for version in versions:
        checks.append(_satisfies_specifier(version, specifier))
    if any(check is True for check in checks):
        return None
    result_certainty = certainty if all(check is False for check in checks) else "unknown"
    return {
        "kind": "version-conflict",
        "name": normalized,
        "requirement": requirement,
        "installed": versions,
        "certainty": result_certainty,
        "source": source,
        "reason": "installed version does not satisfy the recorded requirement"
        if result_certainty == "certain"
        else "version ordering is uncertain for the recorded requirement",
    }


def _satisfies_specifier(version: str, specifier: str) -> bool | None:
    for part in specifier.split(","):
        match = _SPECIFIER_RE.match(part.strip())
        if match is None:
            return None
        operator, expected_text = match.groups()
        if expected_text.endswith(".*"):
            actual_release = _version_tuple(version)
            expected_release = _version_tuple(expected_text[:-2])
            if actual_release is None or expected_release is None:
                return None
            equal = actual_release[: len(expected_release)] == expected_release
            if operator == "==" and not equal:
                return False
            if operator == "!=" and equal:
                return False
            if operator not in {"==", "!="}:
                return None
            continue
        if _version_tokens(version) is None or _version_tokens(expected_text) is None:
            return None
        ordering = compare_versions(version, expected_text)
        if ordering is None:
            return None
        if operator == "==" and ordering != 0:
            return False
        if operator == "!=" and ordering == 0:
            return False
        if operator == ">=" and ordering < 0:
            return False
        if operator == "<=" and ordering > 0:
            return False
        if operator == ">" and ordering <= 0:
            return False
        if operator == "<" and ordering >= 0:
            return False
        if operator == "~=":
            release = _version_tuple(expected_text)
            if release is None:
                return None
            upper = _compatible_upper_bound(release)
            upper_text = ".".join(str(part) for part in upper)
            upper_ordering = compare_versions(version, upper_text)
            if upper_ordering is None:
                return None
            if upper_ordering >= 0:
                return False
    return True


def _compatible_upper_bound(release: tuple[int, ...]) -> tuple[int, ...]:
    if len(release) <= 1:
        return (release[0] + 1,)
    if len(release) == 2:
        return (release[0] + 1, 0)
    return (*release[:-2], release[-2] + 1, 0)


def _dependency_issues(
    snapshot: Mapping[str, Any],
    project: Mapping[str, Any] | None,
) -> list[dict[str, Any]]:
    grouped = _group_distributions(snapshot)
    certainty = _certainty(snapshot)
    identity = _identity(snapshot)
    issues: list[dict[str, Any]] = []
    for normalized, distributions in sorted(grouped.items()):
        for distribution in distributions:
            metadata = distribution.get("metadata")
            if not isinstance(metadata, dict):
                continue
            requirements = metadata.get("requires_dist")
            if not isinstance(requirements, list):
                continue
            for requirement_value in requirements[:MAX_REQUIREMENTS]:
                requirement = str(requirement_value)
                parsed = _parse_requirement(requirement)
                marker_result = (
                    _marker_matches(parsed[2], identity)
                    if parsed is not None and parsed[2]
                    else True
                )
                issue = _dependency_issue(
                    name=normalized,
                    requirement=requirement,
                    installed=grouped.get(parsed[0], []) if parsed is not None else [],
                    certainty=certainty,
                    source=str(distribution.get("name", normalized)),
                    marker_result=marker_result,
                )
                if issue is not None:
                    issues.append(issue)
    _requires_python, project_dependencies, _project_wheels = _project_values(project)
    for requirement_value in project_dependencies[:MAX_REQUIREMENTS]:
        requirement = str(requirement_value)
        parsed = _parse_requirement(requirement)
        marker_result = (
            _marker_matches(parsed[2], identity) if parsed is not None and parsed[2] else True
        )
        issue = _dependency_issue(
            name="project",
            requirement=requirement,
            installed=grouped.get(parsed[0], []) if parsed is not None else [],
            certainty=certainty,
            source="project",
            marker_result=marker_result,
        )
        if issue is not None:
            issues.append(issue)
    issues.sort(
        key=lambda item: (
            str(item.get("kind", "")),
            str(item.get("name", "")),
            str(item.get("requirement", "")),
            str(item.get("source", "")),
        )
    )
    return issues


def _import_comparison(
    before: Mapping[str, list[Mapping[str, Any]]], after: Mapping[str, list[Mapping[str, Any]]]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    evidence: list[dict[str, Any]] = []
    changes: list[dict[str, Any]] = []
    for normalized in sorted(set(before) | set(after)):
        old = before.get(normalized, [])
        new = after.get(normalized, [])
        old_evidence = [_dist_import_names(item) for item in old]
        new_evidence = [_dist_import_names(item) for item in new]
        old_available = any(available for _names, available in old_evidence)
        new_available = any(available for _names, available in new_evidence)
        old_names = sorted({name for names, _available in old_evidence for name in names})
        new_names = sorted({name for names, _available in new_evidence for name in names})
        ambiguous = len(old) > 1 or len(new) > 1
        record: dict[str, Any] = {
            "project_name": str((new or old)[0].get("name", normalized)),
            "normalized_project_name": normalized,
            "import_names": new_names,
            "before": old_names,
            "after": new_names,
            "status": (
                "ambiguous"
                if ambiguous
                else "observed"
                if new_available or old_available
                else "unknown"
            ),
            "certainty": (
                "unknown"
                if ambiguous
                else "observed"
                if new_available or old_available
                else "unknown"
            ),
        }
        if ambiguous:
            record["reason"] = "multiple distributions share this normalized project name"
        elif not new_available and not old_available:
            record["reason"] = "snapshot has no import-name evidence"
        evidence.append(record)
        if old_available and new_available and old_names != new_names:
            changes.append(record)
    return evidence, changes


def compare_snapshots(
    before: Mapping[str, Any] | str | os.PathLike[str],
    after: Mapping[str, Any] | str | os.PathLike[str],
    *,
    project: Mapping[str, Any] | None = None,
    project_metadata: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Compare two snapshots and produce deterministic JSON-compatible evidence."""

    left = _snapshot_input(before)
    right = _snapshot_input(after)
    if project is not None and project_metadata is not None:
        raise DiffError("invalid-project", "pass only one of project or project_metadata")
    project_input = project if project is not None else project_metadata
    old_grouped = _group_distributions(left)
    new_grouped = _group_distributions(right)
    left_certainty = _certainty(left)
    right_certainty = _certainty(right)
    evidence_certainty = "certain" if left_certainty == right_certainty == "certain" else "unknown"
    added: list[dict[str, Any]] = []
    removed: list[dict[str, Any]] = []
    upgraded: list[dict[str, Any]] = []
    downgraded: list[dict[str, Any]] = []
    changed: list[dict[str, Any]] = []
    for normalized in sorted(set(old_grouped) | set(new_grouped)):
        old_items = old_grouped.get(normalized, [])
        new_items = new_grouped.get(normalized, [])
        if len(old_items) > 1 or len(new_items) > 1:
            changed.append(
                {
                    "name": str((new_items or old_items)[0].get("name", normalized)),
                    "normalized_name": normalized,
                    "before": [_dist_public(item) for item in old_items],
                    "after": [_dist_public(item) for item in new_items],
                    "certainty": "unknown",
                    "reason": (
                        "multiple distributions share this normalized project name; "
                        "version transition is ambiguous"
                    ),
                }
            )
            continue
        if not old_items:
            added.extend(
                _with_certainty(_dist_public(item), evidence_certainty) for item in new_items
            )
            continue
        if not new_items:
            removed.extend(
                _with_certainty(_dist_public(item), evidence_certainty) for item in old_items
            )
            continue
        old = old_items[-1]
        new = new_items[-1]
        old_version, new_version = str(old.get("version", "")), str(new.get("version", ""))
        ordering = compare_versions(old_version, new_version)
        if ordering is None:
            if old_version != new_version:
                changed.append(
                    {
                        "name": str(new.get("name", normalized)),
                        "normalized_name": normalized,
                        "from_version": old_version,
                        "to_version": new_version,
                        "before": _dist_public(old),
                        "after": _dist_public(new),
                        "certainty": "unknown",
                        "reason": "version ordering is outside the bounded evaluator",
                    }
                )
        elif ordering < 0:
            upgraded.append(
                {
                    "name": str(new.get("name", normalized)),
                    "normalized_name": normalized,
                    "from_version": old_version,
                    "to_version": new_version,
                    "before": _dist_public(old),
                    "after": _dist_public(new),
                    "certainty": evidence_certainty,
                }
            )
        elif ordering > 0:
            downgraded.append(
                {
                    "name": str(new.get("name", normalized)),
                    "normalized_name": normalized,
                    "from_version": old_version,
                    "to_version": new_version,
                    "before": _dist_public(old),
                    "after": _dist_public(new),
                    "certainty": evidence_certainty,
                }
            )

    identity = _identity(right)
    compatibility: list[dict[str, Any]] = []
    project_requires_python, _project_dependencies, project_wheel_tags = _project_values(
        project_input
    )
    if project_input is not None:
        compatibility.append(
            _compatibility_check(
                "project",
                project_requires_python,
                identity,
                kind="requires-python",
            )
        )
        if project_wheel_tags:
            compatibility.append(_wheel_compatibility("project", project_wheel_tags, identity))
    for normalized in sorted(new_grouped):
        for distribution in new_grouped[normalized]:
            metadata = distribution.get("metadata")
            requires_python = (
                metadata.get("requires_python", "") if isinstance(metadata, dict) else ""
            )
            compatibility.append(
                _compatibility_check(
                    str(distribution.get("name", normalized)),
                    str(requires_python),
                    identity,
                    kind="requires-python",
                )
            )
            tags, tags_available = _dist_wheel_tags(distribution)
            if tags_available:
                compatibility.append(
                    _wheel_compatibility(str(distribution.get("name", normalized)), tags, identity)
                )
    compatibility.sort(key=lambda item: (str(item.get("kind", "")), str(item.get("name", ""))))
    import_evidence, import_changes = _import_comparison(old_grouped, new_grouped)
    dependencies = _dependency_issues(right, project_input)
    summary = {
        "added": len(added),
        "removed": len(removed),
        "upgraded": len(upgraded),
        "downgraded": len(downgraded),
        "changed": len(changed),
        "compatibility_issues": sum(item.get("status") == "incompatible" for item in compatibility),
        "compatibility_unknown": sum(item.get("status") == "unknown" for item in compatibility),
        "dependency_issues": len(dependencies),
    }
    has_changes = any(
        summary[key] for key in ("added", "removed", "upgraded", "downgraded", "changed")
    )
    if summary["compatibility_issues"] or any(
        item.get("kind") in {"missing", "version-conflict"} and item.get("certainty") == "certain"
        for item in dependencies
    ):
        status = "incompatible"
    elif summary["compatibility_unknown"] or dependencies or changed:
        status = "unknown"
    elif has_changes:
        status = "changed"
    else:
        status = "unchanged"
    return {
        "schema_version": "envlens.diff/v1",
        "before": _interpreter_summary(left),
        "after": _interpreter_summary(right),
        "status": status,
        "summary": summary,
        "changes": {
            "added": added,
            "removed": removed,
            "upgraded": upgraded,
            "downgraded": downgraded,
            "changed": changed,
        },
        "added": added,
        "removed": removed,
        "upgraded": upgraded,
        "downgraded": downgraded,
        "changed": changed,
        "project_imports": import_evidence,
        "import_name_changes": import_changes,
        "compatibility": compatibility,
        "dependencies": dependencies,
    }


def diff_snapshots(
    before: Mapping[str, Any] | str | os.PathLike[str],
    after: Mapping[str, Any] | str | os.PathLike[str],
    *,
    project: Mapping[str, Any] | None = None,
    project_metadata: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Compatibility alias for :func:`compare_snapshots`."""

    return compare_snapshots(
        before,
        after,
        project=project,
        project_metadata=project_metadata,
    )


def check_compatibility(
    snapshot: Mapping[str, Any] | str | os.PathLike[str],
    *,
    project: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Return offline compatibility/dependency evidence for one snapshot."""

    value = _snapshot_input(snapshot)
    report = compare_snapshots(value, value, project=project)
    status = report["status"]
    if status == "unchanged":
        status = "compatible"
    return {
        "schema_version": "envlens.compatibility/v1",
        "status": status,
        "compatibility": report["compatibility"],
        "dependencies": report["dependencies"],
        "summary": report["summary"],
    }


def check_snapshot_compatibility(
    snapshot: Mapping[str, Any] | str | os.PathLike[str],
    *,
    project: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Compatibility alias for :func:`check_compatibility`."""

    return check_compatibility(snapshot, project=project)
