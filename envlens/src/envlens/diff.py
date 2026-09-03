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

from envlens.diff_compat import (  # noqa: F401
    _compatibility_check,
    _dist_wheel_tags,
    _marker_matches,
    _project_values,
    _wheel_compatibility,
    _wheel_tag_match,
    compare_versions,
    satisfies_requires_python,
)
from envlens.diff_dependencies import (
    dependency_issues as _dependency_issues_impl,
)
from envlens.snapshot_input import (
    MAX_INPUT_BYTES,
    DiffError,
    _object,
    load_snapshot,
    validate_snapshot,
)

__all__ = [
    "MAX_INPUT_BYTES",
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


def _dist_import_names(distribution: Mapping[str, Any]) -> tuple[list[str], bool]:
    value = distribution.get("import_names")
    if not isinstance(value, list):
        value = _object(distribution.get("metadata"), "distribution.metadata").get("import_names")
    if not isinstance(value, list):
        return [], False
    names = sorted({str(item) for item in value if isinstance(item, str) and item})
    return names, True


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


def _import_names(evidence: list[tuple[list[str], bool]]) -> tuple[list[str], bool]:
    return (
        sorted({name for names, _available in evidence for name in names}),
        any(available for _names, available in evidence),
    )


def _import_record(
    normalized: str,
    old: list[Mapping[str, Any]],
    new: list[Mapping[str, Any]],
) -> tuple[dict[str, Any], bool]:
    old_names, old_available = _import_names([_dist_import_names(item) for item in old])
    new_names, new_available = _import_names([_dist_import_names(item) for item in new])
    ambiguous = len(old) > 1 or len(new) > 1
    observed = new_available or old_available
    record: dict[str, Any] = {
        "project_name": str((new or old)[0].get("name", normalized)),
        "normalized_project_name": normalized,
        "import_names": new_names,
        "before": old_names,
        "after": new_names,
        "status": "ambiguous" if ambiguous else "observed" if observed else "unknown",
        "certainty": "unknown" if ambiguous else "observed" if observed else "unknown",
    }
    if ambiguous:
        record["reason"] = "multiple distributions share this normalized project name"
    elif not observed:
        record["reason"] = "snapshot has no import-name evidence"
    return record, old_available and new_available and old_names != new_names


def _import_comparison(
    before: Mapping[str, list[Mapping[str, Any]]], after: Mapping[str, list[Mapping[str, Any]]]
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    evidence: list[dict[str, Any]] = []
    changes: list[dict[str, Any]] = []
    for normalized in sorted(set(before) | set(after)):
        record, changed = _import_record(
            normalized, before.get(normalized, []), after.get(normalized, [])
        )
        evidence.append(record)
        if changed:
            changes.append(record)
    return evidence, changes


def _ambiguous_distribution_change(
    normalized: str,
    old_items: list[Mapping[str, Any]],
    new_items: list[Mapping[str, Any]],
) -> dict[str, Any]:
    return {
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


def _version_change(
    normalized: str,
    old: Mapping[str, Any],
    new: Mapping[str, Any],
    certainty: str,
) -> tuple[str, dict[str, Any]] | None:
    old_version, new_version = str(old.get("version", "")), str(new.get("version", ""))
    ordering = compare_versions(old_version, new_version)
    record = {
        "name": str(new.get("name", normalized)),
        "normalized_name": normalized,
        "from_version": old_version,
        "to_version": new_version,
        "before": _dist_public(old),
        "after": _dist_public(new),
        "certainty": certainty if ordering is not None else "unknown",
    }
    if ordering is None and old_version != new_version:
        record["reason"] = "version ordering is outside the bounded evaluator"
        return "changed", record
    if ordering is not None and ordering < 0:
        return "upgraded", record
    if ordering is not None and ordering > 0:
        return "downgraded", record
    return None


def _distribution_changes(
    before: Mapping[str, list[Mapping[str, Any]]],
    after: Mapping[str, list[Mapping[str, Any]]],
    certainty: str,
) -> dict[str, list[dict[str, Any]]]:
    changes: dict[str, list[dict[str, Any]]] = {
        "added": [],
        "removed": [],
        "upgraded": [],
        "downgraded": [],
        "changed": [],
    }
    for normalized in sorted(set(before) | set(after)):
        old_items = before.get(normalized, [])
        new_items = after.get(normalized, [])
        for category, record in _group_changes(normalized, old_items, new_items, certainty):
            changes[category].append(record)
    return changes


def _group_changes(
    normalized: str,
    old_items: list[Mapping[str, Any]],
    new_items: list[Mapping[str, Any]],
    certainty: str,
) -> list[tuple[str, dict[str, Any]]]:
    if len(old_items) > 1 or len(new_items) > 1:
        return [("changed", _ambiguous_distribution_change(normalized, old_items, new_items))]
    if not old_items:
        return [("added", _with_certainty(_dist_public(item), certainty)) for item in new_items]
    if not new_items:
        return [("removed", _with_certainty(_dist_public(item), certainty)) for item in old_items]
    version_change = _version_change(normalized, old_items[0], new_items[0], certainty)
    return [] if version_change is None else [version_change]


def _compatibility_evidence(
    grouped: Mapping[str, list[Mapping[str, Any]]],
    identity: Mapping[str, Any],
    project: Mapping[str, Any] | None,
) -> list[dict[str, Any]]:
    evidence: list[dict[str, Any]] = []
    project_python, _project_dependencies, project_wheels = _project_values(project)
    if project is not None:
        evidence.append(
            _compatibility_check("project", project_python, identity, kind="requires-python")
        )
        if project_wheels:
            evidence.append(_wheel_compatibility("project", project_wheels, identity))
    for normalized in sorted(grouped):
        for distribution in grouped[normalized]:
            metadata = distribution.get("metadata")
            requires_python = (
                metadata.get("requires_python", "") if isinstance(metadata, dict) else ""
            )
            name = str(distribution.get("name", normalized))
            evidence.append(
                _compatibility_check(name, str(requires_python), identity, kind="requires-python")
            )
            tags, available = _dist_wheel_tags(distribution)
            if available:
                evidence.append(_wheel_compatibility(name, tags, identity))
    evidence.sort(key=lambda item: (str(item.get("kind", "")), str(item.get("name", ""))))
    return evidence


def _dependency_issues(
    snapshot: Mapping[str, Any],
    project: Mapping[str, Any] | None,
) -> list[dict[str, Any]]:
    grouped = _group_distributions(snapshot)
    return _dependency_issues_impl(project, grouped, _certainty(snapshot), _identity(snapshot))


def _diff_status(
    summary: Mapping[str, int],
    dependencies: list[dict[str, Any]],
) -> str:
    certain_dependency_issue = any(
        item.get("kind") in {"missing", "version-conflict"} and item.get("certainty") == "certain"
        for item in dependencies
    )
    if summary["compatibility_issues"] or certain_dependency_issue:
        return "incompatible"
    if summary["compatibility_unknown"] or dependencies or summary["changed"]:
        return "unknown"
    if any(summary[key] for key in ("added", "removed", "upgraded", "downgraded")):
        return "changed"
    return "unchanged"


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
    changes = _distribution_changes(old_grouped, new_grouped, evidence_certainty)
    identity = _identity(right)
    compatibility = _compatibility_evidence(new_grouped, identity, project_input)
    import_evidence, import_changes = _import_comparison(old_grouped, new_grouped)
    dependencies = _dependency_issues(right, project_input)
    summary = {
        **{name: len(values) for name, values in changes.items()},
        "compatibility_issues": sum(item.get("status") == "incompatible" for item in compatibility),
        "compatibility_unknown": sum(item.get("status") == "unknown" for item in compatibility),
        "dependency_issues": len(dependencies),
    }
    return {
        "schema_version": "envlens.diff/v1",
        "before": _interpreter_summary(left),
        "after": _interpreter_summary(right),
        "status": _diff_status(summary, dependencies),
        "summary": summary,
        **changes,
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
