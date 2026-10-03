"""Bounded offline dependency evidence for snapshot diffs."""

from __future__ import annotations

from collections.abc import Mapping
from typing import Any

from packaging.specifiers import InvalidSpecifier, SpecifierSet
from packaging.utils import canonicalize_name
from packaging.version import InvalidVersion, Version

from envlens.dependency_graph import applicable_marker, dependency_context
from envlens.diff_compat import (
    _bounded_packaging_text,
    _marker_matches,
    _parse_requirement,
    _project_values,
)
from envlens.snapshot_input import MAX_REQUIREMENTS


def dependency_issue(
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
    if specifier.startswith("@"):
        url = specifier[1:]
        for item in installed:
            origin = item.get("origin")
            if (
                isinstance(origin, dict)
                and origin.get("available")
                and not origin.get("redacted")
                and "<REDACTED>" not in url
            ):
                recorded = origin.get("url", "")
                if recorded == url and "<REDACTED>" not in str(recorded):
                    return None
                base, separator, fragment = url.partition("#")
                if separator and recorded == base and origin.get("hash") == fragment:
                    return None
        return {
            "kind": "source-unverified",
            "name": normalized,
            "requirement": requirement,
            "installed": versions,
            "certainty": "unknown",
            "source": source,
            "reason": "installed origin does not verify the requested direct URL",
        }
    if not specifier:
        return None
    checks = [_satisfies_specifier(version, specifier) for version in versions]
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
        "reason": (
            "installed version does not satisfy the recorded requirement"
            if result_certainty == "certain"
            else "version ordering is uncertain for the recorded requirement"
        ),
    }


def _satisfies_specifier(version: str, specifier: str) -> bool | None:
    if not _bounded_packaging_text(version) or not _bounded_packaging_text(specifier):
        return None
    try:
        return SpecifierSet(specifier).contains(Version(version))
    except (InvalidSpecifier, InvalidVersion):
        return None


def _distribution_dependency_issues(
    normalized: str,
    distribution: Mapping[str, Any],
    grouped: Mapping[str, list[Mapping[str, Any]]],
    certainty: str,
    identity: Mapping[str, Any],
    extras: set[str],
) -> list[dict[str, Any]]:
    metadata = distribution.get("metadata")
    if not isinstance(metadata, dict):
        return []
    requirements = metadata.get("requires_dist")
    if not isinstance(requirements, list):
        return []
    issues: list[dict[str, Any]] = []
    for requirement_value in requirements[:MAX_REQUIREMENTS]:
        requirement = str(requirement_value)
        parsed = _parse_requirement(requirement)
        marker_result = (
            applicable_marker(parsed[2], identity, extras)
            if parsed is not None and parsed[2]
            else True
        )
        issue = dependency_issue(
            name=normalized,
            requirement=requirement,
            installed=grouped.get(parsed[0], []) if parsed is not None else [],
            certainty=certainty,
            source=str(distribution.get("name", normalized)),
            marker_result=marker_result,
        )
        if issue is not None:
            issues.append(issue)
    external = metadata.get("requires_external")
    if isinstance(external, list):
        for item in external[:MAX_REQUIREMENTS]:
            issues.append(
                {
                    "kind": "external-requirement",
                    "name": normalized,
                    "requirement": str(item),
                    "installed": [],
                    "certainty": "unknown",
                    "source": str(distribution.get("name", normalized)),
                    "reason": "external requirement cannot be verified offline",
                }
            )
    return issues


def _project_dependency_issues(
    project: Mapping[str, Any] | None,
    grouped: Mapping[str, list[Mapping[str, Any]]],
    certainty: str,
    identity: Mapping[str, Any],
) -> list[dict[str, Any]]:
    _requires_python, project_dependencies, _project_wheels = _project_values(project)
    issues: list[dict[str, Any]] = []
    for requirement_value in project_dependencies[:MAX_REQUIREMENTS]:
        requirement = str(requirement_value)
        parsed = _parse_requirement(requirement)
        marker_result = (
            _marker_matches(parsed[2], identity) if parsed is not None and parsed[2] else True
        )
        issue = dependency_issue(
            name="project",
            requirement=requirement,
            installed=grouped.get(parsed[0], []) if parsed is not None else [],
            certainty=certainty,
            source="project",
            marker_result=marker_result,
        )
        if issue is not None:
            issues.append(issue)
    return issues


def dependency_issues(
    project: Mapping[str, Any] | None,
    grouped: Mapping[str, list[Mapping[str, Any]]],
    certainty: str,
    identity: Mapping[str, Any],
) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    extras, paths, limited = dependency_context(project, grouped, identity)
    for normalized, distributions in sorted(grouped.items()):
        for distribution in distributions:
            issues.extend(
                _distribution_dependency_issues(
                    normalized,
                    distribution,
                    grouped,
                    certainty,
                    identity,
                    extras.get(normalized, set()),
                )
            )
    issues.extend(_project_dependency_issues(project, grouped, certainty, identity))
    for normalized, distributions in sorted(grouped.items()):
        if len(distributions) > 1:
            issues.append(
                {
                    "kind": "duplicate-distribution",
                    "name": normalized,
                    "requirement": "",
                    "installed": [str(item.get("version", "")) for item in distributions],
                    "source": normalized,
                    "certainty": "unknown",
                    "reason": "multiple installations share the same normalized project name",
                }
            )
    if limited:
        issues.append(
            {
                "kind": "graph-limit",
                "name": "project",
                "source": "project",
                "requirement": "",
                "installed": [],
                "certainty": "unknown",
                "reason": "dependency graph reached its evaluation budget",
            }
        )
    for issue in issues:
        source = str(canonicalize_name(str(issue.get("source", "project"))))
        path = paths.get(source, [source])
        name = str(issue.get("name", ""))
        issue["dependency_path"] = path if path[-1] == name else [*path, name]
    issues.sort(
        key=lambda item: (
            str(item.get("kind", "")),
            str(item.get("name", "")),
            str(item.get("requirement", "")),
            str(item.get("source", "")),
        )
    )
    return issues
