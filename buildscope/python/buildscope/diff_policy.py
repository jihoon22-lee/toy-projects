"""Versioned semantic policy for deterministic compilation configuration diffs."""

from __future__ import annotations

import hashlib
import json
import re
from typing import Any

from buildscope._paths import normalize_lexical, project_relative_lexical
from buildscope.diff_glob import glob_matches

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
    "original entry index and duplicate annotation",
    "filesystem existence and stale status",
    "snapshot diagnostics and include-analysis observations",
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
_PATH_VALUE_OPTIONS = {
    "--gcc-toolchain": "--gcc-toolchain",
    "-B": "-B",
    "-include": "-include",
    "-include-pch": "-include-pch",
    "-imacros": "-imacros",
    "-resource-dir": "-resource-dir",
    "/FI": "/FI",
}
_PATH_EQUALS_OPTIONS = {
    "--gcc-toolchain=": "--gcc-toolchain",
    "-fsanitize-blacklist=": "-fsanitize-blacklist",
    "-fsanitize-ignorelist=": "-fsanitize-ignorelist",
    "-resource-dir=": "-resource-dir",
}
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


def _context_path(
    value: str,
    entry: dict[str, Any],
    *,
    project_root: str,
    database_parent: str,
) -> str:
    style = str(entry["normalized"]["command_style"])
    directory = normalize_lexical(str(entry["directory"]), database_parent, style)
    path = project_relative_lexical(
        value,
        base=directory,
        project_root=project_root,
        style=style,
    )
    return path.casefold() if style == "windows" else path


def _launcher_token(
    token: str,
    entry: dict[str, Any],
    *,
    project_root: str,
    database_parent: str,
) -> str:
    if token.startswith("-") or _ASSIGNMENT.fullmatch(token):
        return token
    if "/" in token or "\\" in token or token.startswith("."):
        return _context_path(
            token,
            entry,
            project_root=project_root,
            database_parent=database_parent,
        )
    return token.casefold() if entry["normalized"]["command_style"] == "windows" else token


def _source_spellings(
    entry: dict[str, Any], *, project_root: str, database_parent: str
) -> frozenset[str]:
    normalized = entry["normalized"]
    values = {
        str(normalized["source"]["path"]),
        _context_path(
            str(entry["file"]),
            entry,
            project_root=project_root,
            database_parent=database_parent,
        ),
    }
    if normalized["command_style"] == "windows":
        values.update(value.casefold() for value in tuple(values))
    return frozenset(values)


def _is_source_operand(
    token: str,
    entry: dict[str, Any],
    *,
    project_root: str,
    database_parent: str,
) -> bool:
    comparable = _context_path(
        token,
        entry,
        project_root=project_root,
        database_parent=database_parent,
    )
    return comparable in _source_spellings(
        entry,
        project_root=project_root,
        database_parent=database_parent,
    )


def _modeled_option(argv: list[str], index: int) -> int | None:
    token = argv[index]
    if token in _MODELED_SEPARATED:
        if index + 1 >= len(argv):
            raise DiffPolicyError(f"modeled compiler option {token} has no value")
        return index + 2
    if any(token.startswith(prefix) and token != prefix for prefix in _MODELED_PREFIXES):
        return index + 1
    return None


def _output_option(argv: list[str], index: int) -> int | None:
    token = argv[index]
    if token == "-o" or token in {"/Fo", "/Fo:"}:
        if index + 1 >= len(argv):
            raise DiffPolicyError(f"compiler output option {token} has no value")
        return index + 2
    if token.startswith(("/Fo:", "/Fo")) and token not in {"/Fo", "/Fo:"}:
        return index + 1
    return None


def _path_residual_option(
    argv: list[str],
    index: int,
    entry: dict[str, Any],
    *,
    project_root: str,
    database_parent: str,
) -> tuple[list[str], int] | None:
    token = argv[index]
    if token in _PATH_VALUE_OPTIONS:
        if index + 1 >= len(argv):
            raise DiffPolicyError(f"path-bearing compiler option {token} has no value")
        return [
            _PATH_VALUE_OPTIONS[token],
            _context_path(
                argv[index + 1],
                entry,
                project_root=project_root,
                database_parent=database_parent,
            ),
        ], index + 2
    for prefix, option in sorted(
        _PATH_EQUALS_OPTIONS.items(), key=lambda item: len(item[0]), reverse=True
    ):
        if token.startswith(prefix):
            raw_path = token[len(prefix) :]
            if not raw_path:
                raise DiffPolicyError(f"path-bearing compiler option {option} has no value")
            return [
                option,
                _context_path(
                    raw_path,
                    entry,
                    project_root=project_root,
                    database_parent=database_parent,
                ),
            ], index + 1
    for prefix in ("/FI", "-B"):
        if token.startswith(prefix) and token != prefix:
            return [
                _PATH_VALUE_OPTIONS[prefix],
                _context_path(
                    token[len(prefix) :],
                    entry,
                    project_root=project_root,
                    database_parent=database_parent,
                ),
            ], index + 1
    return None


def _nonsemantic_option(
    argv: list[str],
    index: int,
    entry: dict[str, Any],
    *,
    project_root: str,
    database_parent: str,
) -> tuple[list[str], int] | None:
    token = argv[index]
    if token in _NO_VALUE_OPTIONS:
        return [], index + 1
    if token in _VALUE_OPTIONS:
        if index + 1 >= len(argv):
            raise DiffPolicyError(f"compiler option {token} has no value")
        return [], index + 2
    path_option = _path_residual_option(
        argv,
        index,
        entry,
        project_root=project_root,
        database_parent=database_parent,
    )
    if path_option is not None:
        return path_option
    output_index = _output_option(argv, index)
    if output_index is not None:
        return [], output_index
    modeled_index = _modeled_option(argv, index)
    if modeled_index is not None:
        return [], modeled_index
    return None


def _residual_flags(
    entry: dict[str, Any],
    compiler_index: int,
    *,
    project_root: str,
    database_parent: str,
) -> list[str]:
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
        if _is_source_operand(
            token,
            entry,
            project_root=project_root,
            database_parent=database_parent,
        ):
            index += 1
            continue
        if not terminated:
            option = _nonsemantic_option(
                argv,
                index,
                entry,
                project_root=project_root,
                database_parent=database_parent,
            )
            if option is not None:
                values, index = option
                residual.extend(values)
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


def semantic_configuration(
    entry: dict[str, Any], *, project_root: str, database_parent: str
) -> dict[str, Any]:
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
            "path": _launcher_token(
                str(compiler["path"]),
                entry,
                project_root=project_root,
                database_parent=database_parent,
            ),
            "wrappers": wrappers,
        },
        "defines": _definitions(normalized["defines"]),
        "flags": _residual_flags(
            entry,
            compiler_index,
            project_root=project_root,
            database_parent=database_parent,
        ),
        "include_paths": includes,
        "language": str(normalized["language"]),
        "launcher": [
            _launcher_token(
                token,
                entry,
                project_root=project_root,
                database_parent=database_parent,
            )
            for token in argv[:compiler_index]
        ],
        "standard": str(normalized["standard"]),
        "sysroot": _path_value(normalized["sysroot"]),
        "target": {
            "build_target": str(normalized["target"]["build_target"]),
            "triple": str(normalized["target"]["triple"]),
        },
    }


def configuration_view(
    entry: dict[str, Any], *, project_root: str, database_parent: str
) -> dict[str, Any]:
    semantic = semantic_configuration(
        entry, project_root=project_root, database_parent=database_parent
    )
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
        if any(character in pattern for character in ("\\", "[", "]")):
            raise DiffPolicyError(
                "suppression path glob supports only /, literal characters, *, **, and ?"
            )
        identity = (category, pattern)
        if identity in seen:
            raise DiffPolicyError(f"duplicate suppression rule: {raw}")
        seen.add(identity)
        rules.append({"category": category, "path": pattern})
    return tuple(
        sorted(
            rules,
            key=lambda rule: (
                rule["category"].encode("utf-8"),
                rule["path"].encode("utf-8"),
            ),
        )
    )


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
        for raw_path in (before, after):
            if raw_path and glob_matches(raw_path, rule["path"], windows=windows):
                return f"{rule['category']}:{rule['path']}"
    return ""


def policy_record(rules: tuple[dict[str, str], ...]) -> dict[str, Any]:
    return {
        "ignored_fields": list(IGNORED_FIELDS),
        "suppression_rules": [dict(rule) for rule in rules],
        "version": POLICY_VERSION,
    }
