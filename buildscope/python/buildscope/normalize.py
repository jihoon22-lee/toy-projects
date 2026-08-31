"""Deterministic compilation database normalization for snapshot v2."""

from __future__ import annotations

import hashlib
import json
from collections import Counter, defaultdict
from typing import Any

from buildscope._command import CommandError, compiler_record, parse_invocation
from buildscope._metadata import cmake_target, diagnostic, extract_metadata, output_from_argv
from buildscope._paths import native_mtime, normalize_lexical, path_record


class NormalizationError(ValueError):
    """Raised when a compilation entry cannot be normalized safely."""


def _canonical_digest(value: Any) -> str:
    encoded = json.dumps(
        value,
        allow_nan=False,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return "sha256:" + hashlib.sha256(encoded).hexdigest()


def _normalized_paths(
    entry: dict[str, Any], style: str, project_root: str, database_parent: str
) -> tuple[str, dict[str, Any], dict[str, Any]]:
    base = normalize_lexical(database_parent, database_parent, style)
    directory_value = entry["directory"]
    absolute_directory = normalize_lexical(directory_value, base, style)
    directory = path_record(
        directory_value,
        base=base,
        project_root=project_root,
        style=style,
        expected="directory",
    )
    source = path_record(
        entry["file"],
        base=absolute_directory,
        project_root=project_root,
        style=style,
        expected="file",
    )
    return absolute_directory, directory, source


def _include_paths(
    raw_includes: list[dict[str, str]],
    *,
    base: str,
    project_root: str,
    style: str,
    diagnostics: list[dict[str, str]],
) -> list[dict[str, Any]]:
    includes: list[dict[str, Any]] = []
    for order, raw_include in enumerate(raw_includes):
        record = path_record(
            raw_include["value"],
            base=base,
            project_root=project_root,
            style=style,
            expected="directory",
        )
        record.update({"kind": raw_include["kind"], "order": order})
        includes.append(record)
        if record["exists"] is False:
            diagnostics.append(
                diagnostic("missing-include", "Compiler include directory is missing.")
            )
    return includes


def _source_status(
    entry: dict[str, Any],
    *,
    absolute_directory: str,
    source: dict[str, Any],
    output: dict[str, Any] | None,
    output_value: str,
    style: str,
) -> str:
    if source["exists"] is False:
        return "missing"
    if source["exists"] is None:
        return "unknown"
    if output is None:
        return "present"
    if output["exists"] is False:
        return "stale"
    source_mtime = native_mtime(entry["file"], absolute_directory, style)
    output_mtime = native_mtime(output_value, absolute_directory, style)
    if source_mtime is not None and output_mtime is not None and source_mtime > output_mtime:
        return "stale"
    return "present"


def _output_record(
    value: str,
    *,
    base: str,
    project_root: str,
    style: str,
) -> dict[str, Any] | None:
    if not value:
        return None
    return path_record(
        value,
        base=base,
        project_root=project_root,
        style=style,
        expected="path",
    )


def _output_context(
    entry: dict[str, Any],
    argv: list[str],
    *,
    base: str,
    project_root: str,
    style: str,
    diagnostics: list[dict[str, str]],
) -> tuple[str | None, str, dict[str, Any] | None]:
    declared_output = entry.get("output")
    argv_output = output_from_argv(argv)
    declared_record = _output_record(
        declared_output or "", base=base, project_root=project_root, style=style
    )
    argv_record = _output_record(argv_output, base=base, project_root=project_root, style=style)
    declared_path = declared_record["path"] if declared_record else ""
    argv_path = argv_record["path"] if argv_record else ""
    if style == "windows":
        paths_differ = declared_path.casefold() != argv_path.casefold()
    else:
        paths_differ = declared_path != argv_path
    if declared_record and argv_record and paths_differ:
        diagnostics.append(
            diagnostic("output-mismatch", "Declared and compiler output paths differ.")
        )
    return declared_output, declared_output or argv_output, declared_record or argv_record


def _sysroot_record(
    value: str,
    *,
    base: str,
    project_root: str,
    style: str,
) -> dict[str, Any] | None:
    if not value:
        return None
    return path_record(
        value,
        base=base,
        project_root=project_root,
        style=style,
        expected="directory",
    )


def _append_status_diagnostics(
    diagnostics: list[dict[str, str]], directory: dict[str, Any], status: str
) -> None:
    if directory["exists"] is False:
        diagnostics.append(diagnostic("missing-directory", "Compilation directory is missing."))
    if status == "missing":
        diagnostics.append(diagnostic("missing-source", "Compilation source is missing."))
    elif status == "stale":
        diagnostics.append(diagnostic("stale-output", "Compilation output is missing or stale."))


def normalize_entry(
    entry: dict[str, Any],
    *,
    index: int,
    project_root: str,
    database_parent: str,
) -> dict[str, Any]:
    """Normalize one validated compile database entry into the v2 contract."""

    try:
        argv, style, arguments, command = parse_invocation(entry, index)
    except CommandError as error:
        raise NormalizationError(str(error)) from error
    absolute_directory, directory, source = _normalized_paths(
        entry, style, project_root, database_parent
    )
    metadata = extract_metadata(argv, source["path"])
    diagnostics = metadata["diagnostics"]
    includes = _include_paths(
        metadata["include_paths"],
        base=absolute_directory,
        project_root=project_root,
        style=style,
        diagnostics=diagnostics,
    )
    declared_output, output_value, output = _output_context(
        entry,
        argv,
        base=absolute_directory,
        project_root=project_root,
        style=style,
        diagnostics=diagnostics,
    )
    sysroot = _sysroot_record(
        metadata["sysroot"],
        base=absolute_directory,
        project_root=project_root,
        style=style,
    )
    status = _source_status(
        entry,
        absolute_directory=absolute_directory,
        source=source,
        output=output,
        output_value=output_value,
        style=style,
    )
    _append_status_diagnostics(diagnostics, directory, status)
    configuration = _canonical_digest(
        {
            "argv": argv,
            "directory": directory["path"],
            "output": output["path"] if output else "",
        }
    )
    return {
        "arguments": arguments,
        "command": command,
        "directory": entry["directory"],
        "file": entry["file"],
        "output": declared_output,
        "normalized": {
            "argv": argv,
            "command_style": style,
            "compiler": compiler_record(argv),
            "configuration": configuration,
            "defines": metadata["defines"],
            "directory": directory,
            "include_paths": includes,
            "invocation_source": "arguments" if arguments is not None else "command",
            "language": metadata["language"],
            "output": output,
            "source": source,
            "standard": metadata["standard"],
            "sysroot": sysroot,
            "target": {
                "build_target": cmake_target(output["path"] if output else ""),
                "triple": metadata["target_triple"],
            },
        },
        "state": {
            "duplicate": False,
            "entry_index": index,
            "source_configuration_count": 1,
            "source_status": status,
        },
        "diagnostics": diagnostics,
    }


def annotate_entry_sets(entries: list[dict[str, Any]]) -> None:
    """Annotate duplicates and preserved multi-configuration sources in place."""

    def source_key(entry: dict[str, Any]) -> str:
        style = str(entry["normalized"]["command_style"])
        key = str(entry["normalized"]["source"]["path"])
        if style == "windows":
            key = key.casefold()
        return f"{style}\0{key}"

    configuration_keys = [
        (source_key(entry), entry["normalized"]["configuration"]) for entry in entries
    ]
    duplicate_counts = Counter(configuration_keys)
    source_configurations: dict[str, set[str]] = defaultdict(set)
    for entry in entries:
        source_configurations[source_key(entry)].add(entry["normalized"]["configuration"])
    for entry in entries:
        configuration = entry["normalized"]["configuration"]
        key = source_key(entry)
        duplicate = duplicate_counts[(key, configuration)] > 1
        entry["state"]["duplicate"] = duplicate
        entry["state"]["source_configuration_count"] = len(source_configurations[key])
        if duplicate:
            entry["diagnostics"].append(
                diagnostic(
                    "duplicate-entry",
                    "Compilation entry duplicates another configuration.",
                )
            )
