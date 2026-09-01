"""Bounded, shell-free compiler invocation parsing."""

from __future__ import annotations

import re
import shlex
from pathlib import PurePosixPath
from typing import Any

MAX_ARGUMENTS = 32_768
MAX_ARGUMENT_CHARS = 1024 * 1024
MAX_COMMAND_CHARS = 4 * 1024 * 1024

_WINDOWS_ABSOLUTE = re.compile(r"^(?:[A-Za-z]:[\\/]|\\\\)")
_COMPILER_FAMILIES = {
    "c89": "gcc",
    "c99": "gcc",
    "cc": "gcc",
    "c++": "gcc",
    "gcc": "gcc",
    "g++": "gcc",
    "clang": "clang",
    "clang++": "clang",
    "clang-cl": "clang-cl",
    "cl": "msvc",
    "emcc": "emscripten",
    "em++": "emscripten",
}
_COMPILER_WRAPPERS = frozenset({"ccache", "distcc", "icecc", "sccache"})
_ENV_OPTIONS_WITH_VALUE = frozenset({"-u", "--unset"})
_ASSIGNMENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*=.*$")
_VERSION_SUFFIX = re.compile(r"(?:[-.]\d+(?:\.\d+)*)$")


class CommandError(ValueError):
    """Raised when a compiler invocation cannot be parsed safely."""


def looks_windows_path(value: str) -> bool:
    """Return whether a path uses an unambiguous Windows spelling."""

    return bool(_WINDOWS_ABSOLUTE.match(value)) or ("\\" in value and "/" not in value)


def _windows_quote(
    command: str,
    index: int,
    slashes: int,
    quoted: bool,
    value: list[str],
) -> tuple[int, bool]:
    value.extend("\\" for _ in range(slashes // 2))
    if slashes % 2:
        value.append('"')
    elif quoted and index + 1 < len(command) and command[index + 1] == '"':
        value.append('"')
        index += 1
    else:
        quoted = not quoted
    return index + 1, quoted


def _windows_argument(command: str, index: int) -> tuple[str, int]:
    value: list[str] = []
    quoted = False
    while index < len(command) and (quoted or command[index] not in " \t"):
        slashes = 0
        while index < len(command) and command[index] == "\\":
            slashes += 1
            index += 1
        if index < len(command) and command[index] == '"':
            index, quoted = _windows_quote(command, index, slashes, quoted, value)
            continue
        value.extend("\\" for _ in range(slashes))
        if index < len(command) and (quoted or command[index] not in " \t"):
            value.append(command[index])
            index += 1
    if quoted:
        raise CommandError("command contains an unclosed Windows quote")
    return "".join(value), index


def split_windows_command(command: str) -> list[str]:
    """Parse Microsoft C runtime argv syntax without executing anything."""

    argv: list[str] = []
    index = 0
    while index < len(command):
        while index < len(command) and command[index] in " \t":
            index += 1
        if index < len(command):
            value, index = _windows_argument(command, index)
            argv.append(value)
    return argv


def _command_style(entry: dict[str, Any]) -> str:
    for key in ("directory", "file"):
        value = entry.get(key)
        if isinstance(value, str) and looks_windows_path(value):
            return "windows"
    command = entry.get("command")
    windows_compiler = r"(?:^|\s)(?:cl|clang-cl)(?:\.exe)?(?:\s|$)"
    if isinstance(command, str):
        stripped_command = command.lstrip()
        if re.search(windows_compiler, command, re.IGNORECASE) or re.match(
            r'^"?(?:[A-Za-z]:[\\/]|\\\\)', stripped_command
        ):
            return "windows"
    arguments = entry.get("arguments")
    if isinstance(arguments, list) and arguments:
        raw_compiler = str(arguments[0])
        if looks_windows_path(raw_compiler):
            return "windows"
        compiler = raw_compiler.replace("\\", "/").rsplit("/", 1)[-1]
        if compiler.casefold().removesuffix(".exe") in {"cl", "clang-cl"}:
            return "windows"
    return "posix"


def _arguments(value: Any, index: int) -> list[str] | None:
    if value is None:
        return None
    if not isinstance(value, list) or not value or len(value) > MAX_ARGUMENTS:
        raise CommandError(f"entry[{index}].arguments exceeds the bounded argv contract")
    if any(not isinstance(item, str) or "\0" in item for item in value):
        raise CommandError(f"entry[{index}].arguments must be a bounded string array")
    if not value[0]:
        raise CommandError(f"entry[{index}].arguments must name a compiler")
    if sum(len(item) for item in value) > MAX_ARGUMENT_CHARS:
        raise CommandError(f"entry[{index}].arguments exceeds the character limit")
    return list(value)


def _command(value: Any, index: int) -> str | None:
    if value is None:
        return None
    if not isinstance(value, str) or not value or "\0" in value:
        raise CommandError(f"entry[{index}].command must be a non-empty string")
    if len(value) > MAX_COMMAND_CHARS:
        raise CommandError(f"entry[{index}].command exceeds the character limit")
    return value


def _split_command(command: str, style: str, index: int) -> list[str]:
    try:
        argv = split_windows_command(command) if style == "windows" else shlex.split(command)
    except ValueError as error:
        raise CommandError(f"entry[{index}].command could not be parsed: {error}") from error
    if (
        not argv
        or len(argv) > MAX_ARGUMENTS
        or not argv[0]
        or any("\0" in item for item in argv)
        or sum(len(item) for item in argv) > MAX_ARGUMENT_CHARS
    ):
        raise CommandError(f"entry[{index}].command produced an invalid bounded argv")
    return argv


def parse_invocation(
    entry: dict[str, Any], index: int
) -> tuple[list[str], str, list[str] | None, str | None]:
    """Return bounded argv, preferring arguments when both forms are present."""

    arguments = _arguments(entry.get("arguments"), index)
    command = _command(entry.get("command"), index)
    if arguments is None and command is None:
        raise CommandError(f"entry[{index}] must contain arguments or command")
    style = _command_style(entry)
    if arguments is not None:
        return arguments, style, arguments, command
    argv = _split_command(command or "", style, index)
    return argv, style, arguments, command


def _program_name(token: str) -> str:
    name = PurePosixPath(token.replace("\\", "/")).name
    return name.casefold().removesuffix(".exe")


def _env_prefix(argv: list[str]) -> tuple[int, list[str]]:
    if _program_name(argv[0]) != "env":
        return 0, []
    wrappers = [PurePosixPath(argv[0].replace("\\", "/")).name]
    index = 1
    while index < len(argv):
        token = argv[index]
        if token in _ENV_OPTIONS_WITH_VALUE:
            index += 2
            continue
        if (
            token.startswith("--unset=")
            or token in {"-i", "--ignore-environment"}
            or _ASSIGNMENT.fullmatch(token)
        ):
            index += 1
            continue
        break
    return index, wrappers


def _compiler_family(stem: str) -> str:
    unversioned = _VERSION_SUFFIX.sub("", stem)
    family = _COMPILER_FAMILIES.get(unversioned)
    if family is not None:
        return family
    if unversioned.endswith("clang-cl"):
        return "clang-cl"
    if unversioned.endswith(("clang++", "clang")):
        return "clang"
    if unversioned.endswith(("g++", "gcc", "c++", "cc")):
        return "gcc"
    if unversioned.endswith(("em++", "emcc")):
        return "emscripten"
    return "unknown"


def compiler_record(argv: list[str]) -> dict[str, Any]:
    """Identify common launch wrappers and the actual compiler token."""

    index, wrappers = _env_prefix(argv)
    while index < len(argv) and _program_name(argv[index]) in _COMPILER_WRAPPERS:
        wrappers.append(PurePosixPath(argv[index].replace("\\", "/")).name)
        index += 1
    if index >= len(argv):
        index = len(argv) - 1
    path = argv[index]
    name = PurePosixPath(path.replace("\\", "/")).name
    return {
        "family": _compiler_family(_program_name(path)),
        "name": name,
        "path": path,
        "wrappers": wrappers,
    }
