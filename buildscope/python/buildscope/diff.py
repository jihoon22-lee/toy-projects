"""Deterministic comparison of two normalized compilation databases."""

from __future__ import annotations

import json
from collections import Counter, defaultdict
from pathlib import Path, PurePosixPath
from typing import Any

from buildscope import __version__
from buildscope.diff_policy import (
    DiffPolicyError,
    canonical_digest,
    configuration_view,
    matching_suppression,
    parse_suppressions,
    policy_record,
    role_key,
    source_key,
    source_path,
)
from buildscope.snapshot import MAX_SNAPSHOT_BYTES, SnapshotError, load_compilation_database

DIFF_SCHEMA_VERSION = "buildscope.diff/v1"
MAX_LABEL_CHARS = 256
_CHANGE_FIELDS = (
    ("compiler", "compiler"),
    ("define", "defines"),
    ("flag", "flags"),
    ("include", "include_paths"),
    ("language", "language"),
    ("launcher", "launcher"),
    ("standard", "standard"),
    ("sysroot", "sysroot"),
    ("target", "target"),
)
_KIND_ORDER = {"changed": 0, "moved": 1, "added": 2, "removed": 3}
_UNTRUSTED_DIAGNOSTIC_CODES = frozenset(
    {
        "invalid-define",
        "missing-standard",
        "missing-sysroot",
        "missing-target",
        "output-mismatch",
        "unknown-language",
    }
)


class DiffError(ValueError):
    """Raised when an exact bounded configuration diff cannot be produced."""


def _label(value: str, name: str) -> str:
    if not isinstance(value, str) or not value or "\0" in value or len(value) > MAX_LABEL_CHARS:
        raise DiffError(f"{name} label must be a non-empty bounded string")
    return value


def _record(entry: dict[str, Any], *, project_root: str, database_parent: str) -> dict[str, Any]:
    return {
        "key": source_key(entry),
        "source": source_path(entry),
        "style": str(entry["normalized"]["command_style"]),
        "view": configuration_view(
            entry, project_root=project_root, database_parent=database_parent
        ),
    }


def _records(snapshot: dict[str, Any]) -> list[dict[str, Any]]:
    project_root = str(snapshot["source"]["project_root"])
    database_parent = str(Path(str(snapshot["source"]["path"])).parent)
    records = [
        _record(
            entry,
            project_root=project_root,
            database_parent=database_parent,
        )
        for entry in snapshot["entries"]
    ]
    records.sort(
        key=lambda record: (
            record["key"],
            record["view"]["semantic_digest"],
            json.dumps(
                record["view"]["semantic"],
                ensure_ascii=False,
                separators=(",", ":"),
                sort_keys=True,
            ),
        )
    )
    for stable_index, record in enumerate(records):
        record["view"]["entry_index"] = stable_index
    return records


def _reject_untrusted_diagnostics(snapshot: dict[str, Any]) -> None:
    for entry in snapshot["entries"]:
        for diagnostic in entry["diagnostics"]:
            code = str(diagnostic["code"])
            message = str(diagnostic["message"])
            if code in _UNTRUSTED_DIAGNOSTIC_CODES or (
                code == "missing-include" and "flag has no value" in message
            ):
                raise DiffError(
                    f"{entry['normalized']['source']['path']} cannot be compared exactly: "
                    f"{code}: {message}"
                )


def _inventory_digest(records: list[dict[str, Any]]) -> str:
    inventory = sorted((record["key"], record["view"]["semantic_digest"]) for record in records)
    return canonical_digest(inventory)


def _input_record(label: str, records: list[dict[str, Any]]) -> dict[str, Any]:
    return {
        "configuration_count": len(records),
        "label": label,
        "semantic_digest": _inventory_digest(records),
        "source_count": len({record["key"] for record in records}),
    }


def _change(category: str, before: Any, after: Any) -> dict[str, Any]:
    return {"after": after, "before": before, "category": category}


def _semantic_changes(before: dict[str, Any], after: dict[str, Any]) -> list[dict[str, Any]]:
    changes = []
    for category, field in _CHANGE_FIELDS:
        before_value = before["view"]["semantic"][field]
        after_value = after["view"]["semantic"][field]
        if before_value != after_value:
            changes.append(_change(category, before_value, after_value))
    return changes


def _unit(
    kind: str,
    before: dict[str, Any] | None,
    after: dict[str, Any] | None,
    changes: list[dict[str, Any]],
) -> dict[str, Any]:
    record = after if after is not None else before
    if record is None:
        raise DiffError("diff unit must retain at least one configuration")
    return {
        "after": after["view"] if after is not None else None,
        "before": before["view"] if before is not None else None,
        "changes": changes,
        "kind": kind,
        "source": {
            "after": after["source"] if after is not None else None,
            "before": before["source"] if before is not None else None,
            "style": record["style"],
        },
    }


def _changed_unit(before: dict[str, Any], after: dict[str, Any]) -> dict[str, Any]:
    changes = _semantic_changes(before, after)
    if not changes:
        raise DiffError("configuration pairing produced a change without semantic drift")
    return _unit("changed", before, after, changes)


def _added_unit(after: dict[str, Any]) -> dict[str, Any]:
    return _unit("added", None, after, [_change("added", None, after["view"]["semantic"])])


def _removed_unit(before: dict[str, Any]) -> dict[str, Any]:
    return _unit("removed", before, None, [_change("removed", before["view"]["semantic"], None)])


def _moved_unit(before: dict[str, Any], after: dict[str, Any]) -> dict[str, Any]:
    changes = [_change("moved", before["source"], after["source"])]
    changes.extend(_semantic_changes(before, after))
    return _unit("moved", before, after, changes)


def _pop_digest_matches(
    before: list[dict[str, Any]],
    after: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], int]:
    before_by_digest: dict[str, list[dict[str, Any]]] = defaultdict(list)
    after_by_digest: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for record in before:
        before_by_digest[record["view"]["semantic_digest"]].append(record)
    for record in after:
        after_by_digest[record["view"]["semantic_digest"]].append(record)
    matched_ids: set[int] = set()
    unchanged = 0
    for digest in sorted(set(before_by_digest) & set(after_by_digest)):
        first = sorted(before_by_digest[digest], key=lambda item: item["view"]["entry_index"])
        second = sorted(after_by_digest[digest], key=lambda item: item["view"]["entry_index"])
        count = min(len(first), len(second))
        matched_ids.update(id(item) for item in first[:count])
        matched_ids.update(id(item) for item in second[:count])
        unchanged += count
    return (
        [item for item in before if id(item) not in matched_ids],
        [item for item in after if id(item) not in matched_ids],
        unchanged,
    )


def _unique_role_pairs(
    before: list[dict[str, Any]],
    after: list[dict[str, Any]],
) -> tuple[list[tuple[dict[str, Any], dict[str, Any]]], list[dict[str, Any]], list[dict[str, Any]]]:
    before_counts = Counter(role_key(item["view"]) for item in before)
    after_counts = Counter(role_key(item["view"]) for item in after)
    before_by_role = {role_key(item["view"]): item for item in before}
    after_by_role = {role_key(item["view"]): item for item in after}
    keys = sorted(
        key
        for key in before_counts.keys() & after_counts.keys()
        if key.strip("\0") and before_counts[key] == after_counts[key] == 1
    )
    paired_ids: set[int] = set()
    pairs = []
    for key in keys:
        first, second = before_by_role[key], after_by_role[key]
        pairs.append((first, second))
        paired_ids.update((id(first), id(second)))
    return (
        pairs,
        [item for item in before if id(item) not in paired_ids],
        [item for item in after if id(item) not in paired_ids],
    )


def _same_source_units(
    before: list[dict[str, Any]],
    after: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], int, bool]:
    before, after, unchanged = _pop_digest_matches(before, after)
    pairs, before, after = _unique_role_pairs(before, after)
    if len(before) == len(after) == 1:
        pairs.append((before.pop(), after.pop()))
    units = [_changed_unit(first, second) for first, second in pairs]
    units.extend(_removed_unit(item) for item in before)
    units.extend(_added_unit(item) for item in after)
    return units, unchanged, bool(before and after)


def _source_groups(records: list[dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    groups: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for record in records:
        groups[record["key"]].append(record)
    return groups


def _move_pairs(
    removed: list[dict[str, Any]],
    added: list[dict[str, Any]],
) -> tuple[
    list[dict[str, Any]],
    list[dict[str, Any]],
    list[dict[str, Any]],
    list[dict[str, str]],
]:
    def basename(record: dict[str, Any]) -> str:
        name = PurePosixPath(record["source"].replace("\\", "/")).name
        return name.casefold() if record["style"] == "windows" else name

    def move_key(record: dict[str, Any]) -> str:
        return basename(record) + "\0" + role_key(record["view"])

    def exact_key(record: dict[str, Any]) -> str:
        return move_key(record) + "\0" + record["view"]["semantic_digest"]

    paired_ids: set[int] = set()
    units = []
    for key_function in (exact_key, move_key):
        remaining_removed = [item for item in removed if id(item) not in paired_ids]
        remaining_added = [item for item in added if id(item) not in paired_ids]
        removed_counts = Counter(key_function(item) for item in remaining_removed)
        added_counts = Counter(key_function(item) for item in remaining_added)
        removed_by_key = {key_function(item): item for item in remaining_removed}
        added_by_key = {key_function(item): item for item in remaining_added}
        keys = sorted(
            key
            for key in removed_counts.keys() & added_counts.keys()
            if key.strip("\0") and removed_counts[key] == added_counts[key] == 1
        )
        for key in keys:
            first, second = removed_by_key[key], added_by_key[key]
            units.append(_moved_unit(first, second))
            paired_ids.update((id(first), id(second)))
    remaining_removed = [item for item in removed if id(item) not in paired_ids]
    remaining_added = [item for item in added if id(item) not in paired_ids]
    ambiguous_keys = sorted(
        set(move_key(item) for item in remaining_removed)
        & set(move_key(item) for item in remaining_added)
    )
    diagnostics = [
        {
            "code": "ambiguous-move-match",
            "message": "Potential source moves could not be paired safely.",
            "severity": "warning",
            "source": key.split("\0", 1)[0],
        }
        for key in ambiguous_keys
    ]
    return (
        units,
        remaining_removed,
        remaining_added,
        diagnostics,
    )


def _apply_suppressions(units: list[dict[str, Any]], rules: tuple[dict[str, str], ...]) -> None:
    for unit in units:
        before = unit["source"]["before"] or ""
        after = unit["source"]["after"] or ""
        windows = unit["source"]["style"] == "windows"
        for change in unit["changes"]:
            rule = matching_suppression(
                rules,
                change["category"],
                before,
                after,
                windows=windows,
            )
            change["suppressed"] = bool(rule)
            change["suppression"] = rule or None
        unit["suppressed"] = all(change["suppressed"] for change in unit["changes"])


def _sort_units(units: list[dict[str, Any]]) -> None:
    units.sort(
        key=lambda unit: (
            (unit["source"]["after"] or unit["source"]["before"] or "").casefold()
            if unit["source"]["style"] == "windows"
            else unit["source"]["after"] or unit["source"]["before"] or "",
            _KIND_ORDER[unit["kind"]],
            (unit["after"] or unit["before"])["semantic_digest"],
        )
    )


def _summary(units: list[dict[str, Any]], unchanged: int) -> dict[str, int]:
    counts = Counter(unit["kind"] for unit in units)
    changes = [change for unit in units for change in unit["changes"]]
    return {
        "added": counts["added"],
        "changed": counts["changed"],
        "change_count": len(changes),
        "moved": counts["moved"],
        "removed": counts["removed"],
        "suppressed_changes": sum(change["suppressed"] for change in changes),
        "suppressed_units": sum(unit["suppressed"] for unit in units),
        "unchanged": unchanged,
        "visible_changes": sum(not change["suppressed"] for change in changes),
        "visible_units": sum(not unit["suppressed"] for unit in units),
    }


def compare_databases(
    before_path: Path,
    after_path: Path,
    *,
    before_project_root: Path | str | None = None,
    after_project_root: Path | str | None = None,
    before_label: str = "before",
    after_label: str = "after",
    suppressions: list[str] | tuple[str, ...] = (),
) -> dict[str, Any]:
    """Normalize and compare two compile databases without executing their commands."""

    before_label = _label(before_label, "before")
    after_label = _label(after_label, "after")
    try:
        rules = parse_suppressions(suppressions)
        before_snapshot = load_compilation_database(
            Path(before_path), project_root=before_project_root
        )
        after_snapshot = load_compilation_database(
            Path(after_path), project_root=after_project_root
        )
        _reject_untrusted_diagnostics(before_snapshot)
        _reject_untrusted_diagnostics(after_snapshot)
        before_records, after_records = _records(before_snapshot), _records(after_snapshot)
    except (DiffPolicyError, SnapshotError) as error:
        raise DiffError(str(error)) from error

    before_groups, after_groups = _source_groups(before_records), _source_groups(after_records)
    units: list[dict[str, Any]] = []
    removed: list[dict[str, Any]] = []
    added: list[dict[str, Any]] = []
    unchanged = 0
    diagnostics: list[dict[str, str]] = []
    for key in sorted(before_groups.keys() | after_groups.keys()):
        first, second = before_groups.get(key, []), after_groups.get(key, [])
        if not first:
            added.extend(second)
        elif not second:
            removed.extend(first)
        else:
            source_units, source_unchanged, ambiguous = _same_source_units(first, second)
            units.extend(source_units)
            unchanged += source_unchanged
            if ambiguous:
                diagnostics.append(
                    {
                        "code": "ambiguous-configuration-match",
                        "message": "Multiple configurations could not be paired safely.",
                        "severity": "warning",
                        "source": first[0]["source"],
                    }
                )
    moved, removed, added, move_diagnostics = _move_pairs(removed, added)
    diagnostics.extend(move_diagnostics)
    units.extend(moved)
    units.extend(_removed_unit(item) for item in removed)
    units.extend(_added_unit(item) for item in added)
    _apply_suppressions(units, rules)
    _sort_units(units)
    return {
        "diagnostics": diagnostics,
        "inputs": {
            "after": _input_record(after_label, after_records),
            "before": _input_record(before_label, before_records),
        },
        "policy": policy_record(rules),
        "producer": {"name": "buildscope", "version": __version__},
        "schema_version": DIFF_SCHEMA_VERSION,
        "summary": _summary(units, unchanged),
        "units": units,
    }


def dumps_diff(report: dict[str, Any], *, pretty: bool = False) -> str:
    """Serialize a diff deterministically under the snapshot-sized output bound."""

    if pretty:
        rendered = json.dumps(
            report,
            allow_nan=False,
            ensure_ascii=False,
            indent=2,
            sort_keys=True,
        )
    else:
        rendered = json.dumps(
            report,
            allow_nan=False,
            ensure_ascii=False,
            separators=(",", ":"),
            sort_keys=True,
        )
    rendered += "\n"
    if len(rendered.encode("utf-8")) > MAX_SNAPSHOT_BYTES:
        raise DiffError(f"diff report exceeds {MAX_SNAPSHOT_BYTES} byte limit")
    return rendered
