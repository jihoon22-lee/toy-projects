"""Compiler flag metadata extraction with ordered, lossless results."""

from __future__ import annotations

import re
from pathlib import PurePosixPath
from typing import Any

_DEFINE_NAME = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_SOURCE_LANGUAGES = {
    ".c": "c",
    ".cc": "c++",
    ".cpp": "c++",
    ".cxx": "c++",
    ".m": "objective-c",
    ".mm": "objective-c++",
}
_LANGUAGE_ALIASES = {
    "c": "c",
    "c-header": "c",
    "c++": "c++",
    "c++-header": "c++",
    "objective-c": "objective-c",
    "objective-c++": "objective-c++",
}


def diagnostic(code: str, message: str, *, severity: str = "warning") -> dict[str, str]:
    """Construct a stable diagnostic object."""

    return {"code": code, "message": message, "severity": severity}


def _separated(argv: list[str], index: int, option: str) -> tuple[str | None, int] | None:
    if argv[index] != option:
        return None
    if index + 1 < len(argv):
        return argv[index + 1], index + 2
    return None, index + 1


def _equals(argv: list[str], index: int, option: str) -> tuple[str | None, int] | None:
    token = argv[index]
    if token == option:
        return None, index + 1
    prefix = option + "="
    if token.startswith(prefix):
        return token[len(prefix) :] or None, index + 1
    return None


def _joined(argv: list[str], index: int, option: str) -> tuple[str | None, int] | None:
    token = argv[index]
    if token == option:
        return _separated(argv, index, option)
    if token.startswith(option):
        return token[len(option) :] or None, index + 1
    return None


def _first_match(
    argv: list[str],
    index: int,
    options: tuple[tuple[str, str], ...],
) -> tuple[str, str | None, int] | None:
    for option, mode in options:
        parser = {
            "equals": _equals,
            "joined": _joined,
            "separated": _separated,
        }[mode]
        match = parser(argv, index, option)
        if match is not None:
            value, next_index = match
            return option, value, next_index
    return None


def _definition(argv: list[str], index: int) -> tuple[dict[str, Any] | None, int] | None:
    for option, action in (
        ("-D", "define"),
        ("-U", "undefine"),
        ("/D", "define"),
        ("/U", "undefine"),
    ):
        match = _joined(argv, index, option)
        if match is None:
            continue
        raw, next_index = match
        name, separator, value = (raw or "").partition("=")
        if not raw or _DEFINE_NAME.fullmatch(name) is None:
            return None, next_index
        return {
            "action": action,
            "name": name,
            "value": value if separator else None,
        }, next_index
    return None


def _include(argv: list[str], index: int) -> tuple[str, str | None, int] | None:
    options = (
        ("/external:I", "system", "joined"),
        ("/imsvc", "system", "joined"),
        ("-isystem", "system", "joined"),
        ("-iquote", "quote", "joined"),
        ("-idirafter", "after", "joined"),
        ("-iframework", "framework", "joined"),
        ("-F", "framework", "joined"),
        ("-I", "include", "joined"),
        ("/I", "include", "joined"),
    )
    for option, kind, mode in options:
        match = _first_match(argv, index, ((option, mode),))
        if match is not None:
            _, value, next_index = match
            return kind, value, next_index
    return None


def _target(argv: list[str], index: int) -> tuple[str | None, int] | None:
    token = argv[index]
    if token.startswith("/clang:"):
        forwarded = token[len("/clang:") :]
        for prefix in ("--target=", "-target="):
            if forwarded.startswith(prefix):
                return forwarded[len(prefix) :] or None, index + 1
    match = _first_match(
        argv,
        index,
        (
            ("--target", "separated"),
            ("--target", "equals"),
            ("-target", "separated"),
            ("-target", "equals"),
        ),
    )
    if match is None:
        return None
    return match[1], match[2]


def _consume_language(argv: list[str], index: int, result: dict[str, Any]) -> int | None:
    token = argv[index]
    match = _joined(argv, index, "-x")
    if match is not None:
        value, next_index = match
        language = _LANGUAGE_ALIASES.get((value or "").casefold())
        if language:
            result["language"] = language
        else:
            result["diagnostics"].append(
                diagnostic("unknown-language", "Compiler language flag is unsupported.")
            )
        return next_index
    if token.startswith("/Tc"):
        result["language"] = "c"
        return index + 1
    if token.startswith("/Tp"):
        result["language"] = "c++"
        return index + 1
    return None


def _consume_standard(argv: list[str], index: int, result: dict[str, Any]) -> int | None:
    token = argv[index]
    if token.startswith("/std:"):
        result["standard"] = token[len("/std:") :]
        return index + 1
    match = _equals(argv, index, "-std")
    if match is None:
        return None
    value, next_index = match
    if value:
        result["standard"] = value
    else:
        result["diagnostics"].append(
            diagnostic("missing-standard", "Compiler standard flag has no value.")
        )
    return next_index


def _consume_definition(argv: list[str], index: int, result: dict[str, Any]) -> int | None:
    match = _definition(argv, index)
    if match is None:
        return None
    value, next_index = match
    if value is None:
        result["diagnostics"].append(
            diagnostic("invalid-define", "Compiler definition is malformed.")
        )
    else:
        result["defines"].append(value)
    return next_index


def _consume_include(argv: list[str], index: int, result: dict[str, Any]) -> int | None:
    match = _include(argv, index)
    if match is None:
        return None
    kind, value, next_index = match
    if value:
        result["include_paths"].append({"kind": kind, "value": value})
    else:
        result["diagnostics"].append(
            diagnostic("missing-include", "Compiler include flag has no value.")
        )
    return next_index


def _consume_sysroot(argv: list[str], index: int, result: dict[str, Any]) -> int | None:
    match = _first_match(
        argv,
        index,
        (
            ("--sysroot", "separated"),
            ("--sysroot", "equals"),
            ("-isysroot", "joined"),
        ),
    )
    if match is None:
        return None
    _, value, next_index = match
    if value:
        result["sysroot"] = value
    else:
        result["diagnostics"].append(
            diagnostic("missing-sysroot", "Compiler sysroot flag has no value.")
        )
    return next_index


def _consume_target(argv: list[str], index: int, result: dict[str, Any]) -> int | None:
    match = _target(argv, index)
    if match is None:
        return None
    value, next_index = match
    if value:
        result["target_triple"] = value
    else:
        result["diagnostics"].append(
            diagnostic("missing-target", "Compiler target flag has no value.")
        )
    return next_index


def extract_metadata(argv: list[str], source: str) -> dict[str, Any]:
    """Extract normalized compiler metadata while retaining argv as authority."""

    result: dict[str, Any] = {
        "defines": [],
        "diagnostics": [],
        "include_paths": [],
        "language": _SOURCE_LANGUAGES.get(PurePosixPath(source).suffix.casefold(), ""),
        "standard": "",
        "sysroot": "",
        "target_triple": "",
    }
    consumers = (
        _consume_standard,
        _consume_language,
        _consume_definition,
        _consume_include,
        _consume_sysroot,
        _consume_target,
    )
    index = 1
    while index < len(argv):
        token = argv[index]
        if token == "--":
            break
        if token.startswith("@"):
            codes = {item["code"] for item in result["diagnostics"]}
            if "response-file-opaque" not in codes:
                result["diagnostics"].append(
                    diagnostic("response-file-opaque", "Response-file contents were not expanded.")
                )
            index += 1
            continue
        for consumer in consumers:
            next_index = consumer(argv, index, result)
            if next_index is not None:
                index = next_index
                break
        else:
            index += 1
    return result


def _output_option(argv: list[str], index: int) -> tuple[str | None, int] | None:
    token = argv[index]
    if token in {"-o", "/Fo", "/Fo:"}:
        return _separated(argv, index, token)
    for prefix in ("/Fo:", "/Fo"):
        if token.startswith(prefix):
            return token[len(prefix) :] or None, index + 1
    return None


def output_from_argv(argv: list[str]) -> str:
    """Return the final compiler output flag value, when present."""

    output = ""
    index = 1
    while index < len(argv) and argv[index] != "--":
        match = _output_option(argv, index)
        if match is not None:
            output_value, index = match
            if output_value:
                output = output_value
            continue
        index += 1
    return output


def cmake_target(output: str) -> str:
    """Infer a CMake target from its conventional object path."""

    parts = PurePosixPath(output.replace("\\", "/")).parts
    for index, part in enumerate(parts[:-1]):
        if part == "CMakeFiles" and parts[index + 1].endswith(".dir"):
            return parts[index + 1][: -len(".dir")]
    return ""
