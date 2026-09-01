"""Versioned semantic policy for deterministic compilation configuration diffs."""

from __future__ import annotations

import fnmatch
import hashlib
import json
import re
from pathlib import PurePosixPath
from typing import Any

POLICY_VERSION = "buildscope.diff-policy/v1"
MAX_SUPPRESSIONS = 256
MAX_SUPPRESSION_CHARS = 1024
CHANGE_CATEGORIES = frozenset(
    {
        "added",
        "compiler",
        "define",
        "flag",
        "include",
        "language",
        "launcher",
        "moved",
        "removed",
        "standard",
        "sysroot",
        "target",
    }
)
IGNORED_FIELDS = (
    "raw command spelling",
    "compilation directory",
    "output path and filename",
    "entry index and duplicate annotation",
    "filesystem existence and stale status",
    "snapshot diagnostics and include-analysis observations",
    "absolute compiler path when family/name are unchanged",
)

_ASSIGNMENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*=.*$")
_NO_VALUE_OPTIONS = frozenset(
    {
        "-c",
        "-MD",
        "-MMD",
        "-MP",
        "-MG",
        "/c",
        "/showIncludes",
    }
)
_VALUE_OPTIONS = frozenset({"-MF", "-MT", "-MQ", "/sourceDependencies"})
_MODELED_SEPARATED = frozenset(
    {
        "-D",
        "-U",
        "-F",
        "-I",
        "-idirafter",
        "-iframework",
        "-iquote",
        "-isystem",
        "-isysroot",
        "-std",
        "-target",
        "--sysroot",
        "--target",
        "-x",
        "/D",
        "/I",
        "/Tc",
        "/Tp",
        "/U",
        "/external:I",
        "/imsvc",
    }
)
_MODELED_PREFIXES = (
    "/external:I",
    "--sysroot=",
    "--target=",
    "-idirafter",
    "-iframework",
    "-iquote",
    "-isystem",
    "-isysroot",
    "/clang:--target=",
    "/clang:-target=",
    "-target=",
    "-std=",
    "/std:",
    "/imsvc",
    "-D",
    "-U",
    "-F",
    "-I",
    "-x",
    "/D",
    "/I",
    "/Tc",
    "/Tp",
    "/U",
)


class DiffPolicyError(ValueError):
    """Raised when a configuration cannot be compared without inventing evidence."""


def canonical_digest(value: Any) -> str:
    """Return the stable SHA-256 identity of a JSON-compatible semantic value."""

    encoded = json.dumps(
        value,
        allow_nan=False,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return "sha256:" + hashlib.sha256(encoded).hexdigest()


def source_key(entry: dict[str, Any]) -> str:
    """Match the snapshot producer's platform-aware source identity."""

    normalized = entry["normalized"]
    style = str(normalized["command_style"])
    path = str(normalized["source"]["path"])
    comparable = path.casefold() if style == "windows" else path
    return f"{style}\0{comparable}"


def source_path(entry: dict[str, Any]) -> str:
    return str(entry["normalized"]["source"]["path"])


def _path_value(record: dict[str, Any] | None) -> str:
    if record is None:
        return ""
    path = str(record["path"])
    return path.casefold() if record.get("style") == "windows" else path


def _compiler_index(argv: list[str], compiler_path: str) -> int:
    for index, token in enumerate(argv):
        if token == compiler_path:
            return index
    raise DiffPolicyError("normalized compiler path is not present in argv")


def _launcher_token(token: str) -> str:
    if token.startswith("-") or _ASSIGNMENT.fullmatch(token):
        return token
    return PurePosixPath(token.replace("\\", "/")).name


def _source_spellings(entry: dict[str, Any]) -> frozenset[str]:
    normalized = entry["normalized"]
    values = {str(entry["file"]), str(normalized["source"]["path"])}
    if normalized["command_style"] == "windows":
        values.update(value.casefold() for value in tuple(values))
    return frozenset(values)


def _is_source_operand(token: str, entry: dict[str, Any]) -> bool:
    values = _source_spellings(entry)
    if token in values:
        return True
    if entry["normalized"]["command_style"] == "windows":
        return token.casefold() in values
    return False


def _modeled_option(argv: list[str], index: int) -> int | None:
    token = argv[index]
    if token in _MODELED_SEPARATED:
        return min(index + 2, len(argv))
    if any(token.startswith(prefix) and token != prefix for prefix in _MODELED_PREFIXES):
        return index + 1
    return None


def _output_option(argv: list[str], index: int) -> int | None:
    token = argv[index]
    if token == "-o" or token in {"/Fo", "/Fo:"}:
        return min(index + 2, len(argv))
    if token.startswith(("/Fo:", "/Fo")) and token not in {"/Fo", "/Fo:"}:
        return index + 1
    return None


def _residual_flags(entry: dict[str, Any], compiler_index: int) -> list[str]:
    argv = list(entry["normalized"]["argv"])
    residual: list[str] = []
    index = compiler_index + 1
    terminated = False
    while index < len(argv):
        token = argv[index]
        if token == "--":
            residual.append(token)
            terminated = True
            index += 1
            continue
        if _is_source_operand(token, entry):
            index += 1
            continue
        if not terminated and token in _NO_VALUE_OPTIONS:
            index += 1
            continue
        if not terminated and token in _VALUE_OPTIONS:
            index = min(index + 2, len(argv))
            continue
        if not terminated:
            output_index = _output_option(argv, index)
            if output_index is not None:
                index = output_index
                continue
            modeled_index = _modeled_option(argv, index)
            if modeled_index is not None:
                index = modeled_index
                continue
        residual.append(token)
        index += 1
    return residual


def _definitions(values: list[dict[str, Any]]) -> list[dict[str, str]]:
    result: list[dict[str, str]] = []
    for value in values:
        action = str(value["action"])
        raw = value.get("value")
        semantic_value = "1" if action == "define" and raw is None else str(raw or "")
        result.append({"action": action, "name": str(value["name"]), "value": semantic_value})
    return result


def semantic_configuration(entry: dict[str, Any]) -> dict[str, Any]:
    """Project a normalized entry onto the documented diff semantics."""

    normalized = entry["normalized"]
    argv = list(normalized["argv"])
    compiler = normalized["compiler"]
    compiler_index = _compiler_index(argv, str(compiler["path"]))
    if any(token.startswith("@") for token in argv[compiler_index + 1 :]):
        raise DiffPolicyError(
            f"{source_path(entry)} uses an opaque response file and cannot be compared exactly"
        )
    windows = normalized["command_style"] == "windows"
    compiler_name = str(compiler["name"])
    wrappers = [str(value) for value in compiler["wrappers"]]
    if windows:
        compiler_name = compiler_name.casefold()
        wrappers = [value.casefold() for value in wrappers]
    includes = [
        {"kind": str(value["kind"]), "path": _path_value(value)}
        for value in normalized["include_paths"]
    ]
    return {
        "compiler": {
            "command_style": str(normalized["command_style"]),
            "family": str(compiler["family"]),
            "name": compiler_name,
            "wrappers": wrappers,
        },
        "defines": _definitions(normalized["defines"]),
        "flags": _residual_flags(entry, compiler_index),
        "include_paths": includes,
        "language": str(normalized["language"]),
        "launcher": [_launcher_token(token) for token in argv[:compiler_index]],
        "standard": str(normalized["standard"]),
        "sysroot": _path_value(normalized["sysroot"]),
        "target": {
            "build_target": str(normalized["target"]["build_target"]),
            "triple": str(normalized["target"]["triple"]),
        },
    }


def configuration_view(entry: dict[str, Any]) -> dict[str, Any]:
    semantic = semantic_configuration(entry)
    return {
        "entry_index": int(entry["state"]["entry_index"]),
        "semantic": semantic,
        "semantic_digest": canonical_digest(semantic),
    }


def role_key(view: dict[str, Any]) -> str:
    semantic = view["semantic"]
    target = semantic["target"]
    return "\0".join(
        (str(semantic["language"]), str(target["build_target"]), str(target["triple"]))
    )


def parse_suppressions(values: list[str] | tuple[str, ...]) -> tuple[dict[str, str], ...]:
    """Validate, canonicalize, and de-duplicate category:path-glob rules."""

    if len(values) > MAX_SUPPRESSIONS:
        raise DiffPolicyError(f"suppression count exceeds {MAX_SUPPRESSIONS}")
    rules: list[dict[str, str]] = []
    seen: set[tuple[str, str]] = set()
    for raw in values:
        if not raw or "\0" in raw or len(raw) > MAX_SUPPRESSION_CHARS:
            raise DiffPolicyError("suppression must be a non-empty bounded string")
        category, separator, pattern = raw.partition(":")
        if category != "*" and category not in CHANGE_CATEGORIES:
            raise DiffPolicyError(f"unknown suppression category: {category}")
        pattern = pattern if separator else "*"
        if not pattern:
            raise DiffPolicyError("suppression path glob must not be empty")
        identity = (category, pattern)
        if identity in seen:
            raise DiffPolicyError(f"duplicate suppression rule: {raw}")
        seen.add(identity)
        rules.append({"category": category, "path": pattern})
    return tuple(sorted(rules, key=lambda rule: (rule["category"], rule["path"])))


def matching_suppression(
    rules: tuple[dict[str, str], ...],
    category: str,
    before: str,
    after: str,
    *,
    windows: bool = False,
) -> str:
    for rule in rules:
        if rule["category"] not in {"*", category}:
            continue
        pattern = rule["path"].casefold() if windows else rule["path"]
        for raw_path in (before, after):
            path = raw_path.casefold() if windows else raw_path
            if path and fnmatch.fnmatchcase(path, pattern):
                return f"{rule['category']}:{rule['path']}"
    return ""


def policy_record(rules: tuple[dict[str, str], ...]) -> dict[str, Any]:
    return {
        "ignored_fields": list(IGNORED_FIELDS),
        "suppression_rules": [dict(rule) for rule in rules],
        "version": POLICY_VERSION,
    }
