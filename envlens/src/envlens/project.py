"""Bounded, read-only inspection of project metadata in ``pyproject.toml``."""

from __future__ import annotations

import ast
import re
from collections.abc import Mapping
from pathlib import Path
from typing import Any, cast

from envlens.snapshot import normalize_project_name

MAX_PYPROJECT_BYTES = 2 * 1024 * 1024
MAX_PROJECT_ITEMS = 100_000
MAX_ENTRY_POINTS = 10_000
MAX_STRING_LENGTH = 65_536


class ProjectError(ValueError):
    """A stable, user-facing project inspection failure."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message


def _strip_comment(value: str) -> str:
    quote = ""
    escaped = False
    for index, char in enumerate(value):
        if quote:
            if quote == '"' and escaped:
                escaped = False
            elif quote == '"' and char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
        elif char in {'"', "'"}:
            quote = char
        elif char == "#":
            return value[:index].rstrip()
    return value.rstrip()


def _split_dotted(value: str) -> list[str]:
    parts: list[str] = []
    current: list[str] = []
    quote = ""
    escaped = False
    for char in value.strip():
        if quote:
            current.append(char)
            if quote == '"' and escaped:
                escaped = False
            elif quote == '"' and char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
        elif char in {'"', "'"}:
            quote = char
            current.append(char)
        elif char == ".":
            part = "".join(current).strip()
            if not part:
                raise ProjectError("invalid-pyproject", "empty dotted TOML key")
            parts.append(_decode_key(part))
            current = []
        else:
            current.append(char)
    part = "".join(current).strip()
    if not part:
        raise ProjectError("invalid-pyproject", "empty dotted TOML key")
    parts.append(_decode_key(part))
    return parts


def _decode_key(value: str) -> str:
    value = value.strip()
    if value[:1] in {'"', "'"} and value[-1:] == value[:1]:
        try:
            decoded = ast.literal_eval(value)
        except (SyntaxError, ValueError) as error:
            raise ProjectError("invalid-pyproject", f"invalid quoted TOML key: {value}") from error
        if not isinstance(decoded, str):
            raise ProjectError("invalid-pyproject", "TOML key must decode to a string")
        return decoded
    if not re.fullmatch(r"[A-Za-z0-9_-]+", value):
        raise ProjectError("invalid-pyproject", f"invalid bare TOML key: {value}")
    return value


def _bracket_balance(value: str) -> int:
    quote = ""
    escaped = False
    balance = 0
    for char in value:
        if quote:
            if quote == '"' and escaped:
                escaped = False
            elif quote == '"' and char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
        elif char in {'"', "'"}:
            quote = char
        elif char == "[":
            balance += 1
        elif char == "]":
            balance -= 1
    return balance


def _split_top_level(value: str) -> list[str]:
    parts: list[str] = []
    start = 0
    quote = ""
    escaped = False
    braces = brackets = 0
    for index, char in enumerate(value):
        if quote:
            if quote == '"' and escaped:
                escaped = False
            elif quote == '"' and char == "\\":
                escaped = True
            elif char == quote:
                quote = ""
        elif char in {'"', "'"}:
            quote = char
        elif char == "{":
            braces += 1
        elif char == "}":
            braces -= 1
        elif char == "[":
            brackets += 1
        elif char == "]":
            brackets -= 1
        elif char == "," and braces == 0 and brackets == 0:
            parts.append(value[start:index].strip())
            start = index + 1
    tail = value[start:].strip()
    if tail:
        parts.append(tail)
    return parts


def _parse_inline_table(value: str, *, line: int) -> dict[str, Any]:
    if not (value.startswith("{") and value.endswith("}")):
        raise ProjectError("invalid-pyproject", f"invalid inline TOML table on line {line}")
    result: dict[str, Any] = {}
    for item in _split_top_level(value[1:-1]):
        if "=" not in item:
            raise ProjectError("invalid-pyproject", f"invalid inline TOML table on line {line}")
        key_text, raw_value = item.split("=", 1)
        key = _decode_key(key_text.strip())
        if key in result:
            raise ProjectError("invalid-pyproject", f"duplicate inline TOML key {key}")
        result[key] = _parse_value(raw_value.strip(), line=line)
    return result


def _parse_value(value: str, *, line: int) -> Any:
    cleaned = _strip_comment(value).strip()
    if len(cleaned) > MAX_STRING_LENGTH:
        raise ProjectError("project-field-too-large", f"line {line} exceeds 65536 characters")
    if not cleaned:
        raise ProjectError("invalid-pyproject", f"line {line} has an empty TOML value")
    if cleaned in {"true", "false"}:
        return cleaned == "true"
    if cleaned.startswith("["):
        try:
            parsed = ast.literal_eval(cleaned)
        except (SyntaxError, ValueError):
            try:
                parsed = [
                    _parse_inline_table(item, line=line)
                    if item.startswith("{")
                    else _parse_value(item, line=line)
                    for item in _split_top_level(cleaned[1:-1])
                ]
            except ProjectError:
                raise
            except (SyntaxError, ValueError) as error:
                raise ProjectError(
                    "invalid-pyproject", f"invalid TOML array on line {line}"
                ) from error
        if not isinstance(parsed, list):
            raise ProjectError("invalid-pyproject", f"TOML array expected on line {line}")
        if len(parsed) > MAX_PROJECT_ITEMS:
            raise ProjectError("project-field-too-large", "project array exceeds 100000 items")
        return parsed
    if cleaned.startswith("{"):
        return _parse_inline_table(cleaned, line=line)
    if cleaned[:1] in {'"', "'"}:
        try:
            parsed = ast.literal_eval(cleaned)
        except (SyntaxError, ValueError) as error:
            raise ProjectError(
                "invalid-pyproject", f"invalid TOML string on line {line}"
            ) from error
        if not isinstance(parsed, str):
            raise ProjectError("invalid-pyproject", f"TOML string expected on line {line}")
        if len(parsed) > MAX_STRING_LENGTH:
            raise ProjectError("project-field-too-large", "project string exceeds 65536 characters")
        return parsed
    # The fields consumed by envlens are string/list values.  Retaining simple
    # numeric values makes the parser useful for harmless unrelated TOML while
    # avoiding an unsafe/easily ambiguous evaluator.
    if re.fullmatch(r"[-+]?\d+", cleaned):
        return int(cleaned)
    if re.fullmatch(r"[-+]?(?:\d+\.\d*|\d*\.\d+)", cleaned):
        return float(cleaned)
    return cleaned


def _set_nested(
    root: dict[str, Any],
    path: list[str],
    value: Any,
    *,
    line: int,
    locations: dict[tuple[str, ...], int],
) -> None:
    current = root
    for part in path[:-1]:
        existing = current.get(part)
        if existing is None:
            existing = {}
            current[part] = existing
        if not isinstance(existing, dict):
            raise ProjectError("invalid-pyproject", f"line {line} redefines a TOML table")
        current = existing
    key = path[-1]
    if key in current:
        raise ProjectError("invalid-pyproject", f"line {line} duplicates TOML key {'.'.join(path)}")
    current[key] = value
    locations[tuple(path)] = line


def _parse_toml(text: str) -> tuple[dict[str, Any], dict[tuple[str, ...], int]]:
    root: dict[str, Any] = {}
    locations: dict[tuple[str, ...], int] = {}
    current: list[str] = []
    lines = text.splitlines()
    index = 0
    while index < len(lines):
        line_number = index + 1
        line = _strip_comment(lines[index]).strip()
        index += 1
        if not line:
            continue
        if line.startswith("[[") and line.endswith("]]"):
            raise ProjectError(
                "unsupported-pyproject",
                f"array tables are unsupported on line {line_number}",
            )
        if line.startswith("[") and line.endswith("]"):
            if line.count("[") != 1 or line.count("]") != 1:
                raise ProjectError("invalid-pyproject", f"invalid TOML table on line {line_number}")
            current = _split_dotted(line[1:-1])
            existing: Any = root
            for part in current:
                if part not in existing:
                    existing[part] = {}
                existing = existing[part]
                if not isinstance(existing, dict):
                    raise ProjectError(
                        "invalid-pyproject", f"line {line_number} redefines a TOML table"
                    )
            continue
        if "=" not in line:
            raise ProjectError("invalid-pyproject", f"expected key/value on line {line_number}")
        key_text, raw_value = line.split("=", 1)
        key_parts = _split_dotted(key_text.strip())
        value_text = raw_value.strip()
        balance = _bracket_balance(value_text)
        while balance > 0 and index < len(lines):
            continuation = _strip_comment(lines[index]).strip()
            index += 1
            value_text += " " + continuation
            balance += _bracket_balance(continuation)
        if balance != 0:
            raise ProjectError(
                "invalid-pyproject", f"unterminated TOML array on line {line_number}"
            )
        _set_nested(
            root,
            [*current, *key_parts],
            _parse_value(value_text, line=line_number),
            line=line_number,
            locations=locations,
        )
    return root, locations


def _get_mapping(root: Mapping[str, Any], key: str) -> dict[str, Any]:
    value = root.get(key, {})
    return value if isinstance(value, dict) else {}


def _string_field(value: Any, label: str, *, required: bool = False) -> str:
    if value is None:
        if required:
            raise ProjectError("invalid-project-metadata", f"project.{label} is required")
        return ""
    if not isinstance(value, str):
        raise ProjectError("invalid-project-metadata", f"project.{label} must be a string")
    if required and not value:
        raise ProjectError("invalid-project-metadata", f"project.{label} must not be empty")
    return value


def _string_list(value: Any, label: str) -> list[str]:
    if value is None:
        return []
    if not isinstance(value, list) or len(value) > MAX_PROJECT_ITEMS:
        raise ProjectError("invalid-project-metadata", f"{label} must be a bounded string array")
    result: list[str] = []
    for item in value:
        if not isinstance(item, str) or not item:
            raise ProjectError("invalid-project-metadata", f"{label} contains a non-empty string")
        if len(item) > MAX_STRING_LENGTH:
            raise ProjectError("project-field-too-large", f"{label} item exceeds 65536 characters")
        result.append(item)
    return sorted(result)


def _entry_point_target(value: str) -> tuple[str, str] | None:
    target = value.split("[", 1)[0].strip()
    if ":" not in target:
        return None
    module, attribute = target.split(":", 1)
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*", module):
        return None
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.]*", attribute):
        return None
    return module, attribute


def _entry_points(
    project: Mapping[str, Any], locations: Mapping[tuple[str, ...], int], path: Path
) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    sections = (("scripts", "console_scripts"), ("gui-scripts", "gui_scripts"))
    for key, group in sections:
        table = _get_mapping(project, key)
        for name, value in sorted(table.items()):
            if not isinstance(name, str) or not isinstance(value, str):
                raise ProjectError(
                    "invalid-project-metadata",
                    f"project.{key} entry points must be strings",
                )
            location_key = ("project", key, name)
            target = _entry_point_target(value)
            records.append(
                {
                    "group": group,
                    "name": name,
                    "value": value,
                    "target": {"module": target[0], "attribute": target[1]} if target else None,
                    "status": "inspectable" if target else "unsupported",
                    "location": {"path": str(path), "line": locations.get(location_key, 0)},
                }
            )
    custom = _get_mapping(project, "entry-points")
    for group, table_value in sorted(custom.items()):
        if not isinstance(table_value, dict):
            raise ProjectError(
                "invalid-project-metadata",
                f"project.entry-points.{group} must be a table",
            )
        for name, value in sorted(table_value.items()):
            if not isinstance(name, str) or not isinstance(value, str):
                raise ProjectError(
                    "invalid-project-metadata",
                    "entry-point names and values must be strings",
                )
            custom_location_key = ("project", "entry-points", group, name)
            target = _entry_point_target(value)
            records.append(
                {
                    "group": group,
                    "name": name,
                    "value": value,
                    "target": {"module": target[0], "attribute": target[1]} if target else None,
                    "status": "inspectable" if target else "unsupported",
                    "location": {
                        "path": str(path),
                        "line": locations.get(custom_location_key, 0),
                    },
                }
            )
    if len(records) > MAX_ENTRY_POINTS:
        raise ProjectError("project-field-too-large", "project entry points exceed 10000 items")
    return records


def _tool_configuration(root: Mapping[str, Any]) -> dict[str, list[str]]:
    tool = _get_mapping(root, "tool")
    envlens = _get_mapping(tool, "envlens")
    runtime = _get_mapping(envlens, "runtime")
    configuration: dict[str, list[str]] = {}
    for output_key, keys in (
        ("interpreters", ("interpreters",)),
        ("imports", ("imports", "modules")),
        ("compile_paths", ("compile_paths", "compile")),
        ("entry_points", ("entry_points",)),
    ):
        value: Any = None
        for key in keys:
            if key in runtime:
                value = runtime[key]
                break
            if key in envlens:
                value = envlens[key]
                break
        values = _string_list(value, f"tool.envlens.{output_key}")
        if values:
            configuration[output_key] = values
    return configuration


def inspect_pyproject(path: str | Path = "pyproject.toml") -> dict[str, Any]:
    """Read project metadata and entry points without importing or executing code."""

    project_path = Path(path)
    try:
        stat = project_path.stat()
    except OSError as error:
        raise ProjectError("project-read-failed", str(error)) from error
    if not project_path.is_file():
        raise ProjectError("project-read-failed", "pyproject.toml must be a regular file")
    if stat.st_size > MAX_PYPROJECT_BYTES:
        raise ProjectError("project-too-large", "pyproject.toml exceeds 2 MiB")
    try:
        data = project_path.read_bytes()
        text = data.decode("utf-8")
    except (OSError, UnicodeError) as error:
        raise ProjectError("project-read-failed", str(error)) from error
    if len(data) > MAX_PYPROJECT_BYTES:
        raise ProjectError("project-too-large", "pyproject.toml exceeds 2 MiB")
    root, locations = _parse_toml(text)
    project = _get_mapping(root, "project")
    name = _string_field(project.get("name"), "name", required=False)
    version = _string_field(project.get("version"), "version", required=False)
    requires_python = _string_field(project.get("requires-python"), "requires-python")
    dependencies = _string_list(project.get("dependencies"), "project.dependencies")
    entries = _entry_points(project, locations, project_path)
    configuration = _tool_configuration(root)
    result: dict[str, Any] = {
        "schema_version": "envlens.project/v1",
        "path": str(project_path),
        "project": {
            "name": name,
            "normalized_name": normalize_project_name(name) if name else "",
            "version": version,
            "requires_python": requires_python,
            "dependencies": dependencies,
        },
        "entry_points": entries,
        "warnings": [
            "entry-point inspection is dry; code is not imported or executed",
        ],
    }
    if configuration:
        result["configuration"] = configuration
    return result


def inspect_project(path: str | Path = "pyproject.toml") -> dict[str, Any]:
    """Compatibility alias for :func:`inspect_pyproject`."""

    return inspect_pyproject(path)


def load_pyproject(path: str | Path = "pyproject.toml") -> dict[str, Any]:
    """Compatibility alias for :func:`inspect_pyproject`."""

    return inspect_pyproject(path)


def inspect_entry_points(path: str | Path = "pyproject.toml") -> list[dict[str, Any]]:
    """Return only the dry-inspected entry-point records for one project."""

    return cast(list[dict[str, Any]], inspect_pyproject(path)["entry_points"])
