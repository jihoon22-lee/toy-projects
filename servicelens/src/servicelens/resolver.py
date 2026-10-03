"""Unit selection and drop-in discovery, separate from directive evaluation."""

from __future__ import annotations

import posixpath
import re
from typing import Any

from .fs import RootFS
from .model import InputError, diagnostic

SYSTEM_PATHS = (
    "/etc/systemd/system.control",
    "/run/systemd/system.control",
    "/run/systemd/transient",
    "/run/systemd/generator.early",
    "/etc/systemd/system",
    "/etc/systemd/system.attached",
    "/run/systemd/system",
    "/run/systemd/system.attached",
    "/run/systemd/generator",
    "/usr/local/lib/systemd/system",
    "/usr/lib/systemd/system",
    "/lib/systemd/system",
    "/run/systemd/generator.late",
)
UNIT_NAME = re.compile(
    r"[A-Za-z0-9:_.@\\-]+\.(service|target|socket|timer|path|mount|automount|slice|scope|device|swap)"
)


def validate_name(name: str) -> None:
    if len(name.encode()) > 255 or not UNIT_NAME.fullmatch(name) or name.count("@") > 1:
        raise InputError("invalid unit name")
    if re.search(r"\\(?!x[0-9a-fA-F]{2})", name):
        raise InputError("invalid escape in unit name")


def template(name: str) -> str | None:
    if "@" not in name:
        return None
    prefix = name.split("@", 1)[0]
    return prefix + "@." + name.rsplit(".", 1)[1]


def hierarchy(name: str) -> list[str]:
    """Least to most specific; same-name drop-ins from the latter win."""
    stem, suffix = name.rsplit(".", 1)
    result = [suffix]
    prefix = stem.split("@", 1)[0]
    result.extend(prefix[: i + 1] + "." + suffix for i, c in enumerate(prefix) if c == "-")
    base = template(name)
    if base and base != name:
        result.append(base)
    result.append(name)
    return list(dict.fromkeys(result))


def resolve_unit(fs: RootFS, name: str, paths: tuple[str, ...]) -> dict[str, Any]:
    validate_name(name)
    issues: list[dict[str, Any]] = []
    candidates: list[dict[str, Any]] = []
    chosen: str | None = None
    masked = False
    names = [name]
    base = template(name)
    if base and base != name:
        names.append(base)
    for sought in names:
        for directory in paths:
            path = directory + "/" + sought
            try:
                link = fs.link(path)
                canonical = (
                    "/dev/null" if link == "/dev/null" else fs.resolve(path, allow_mask=True)
                )
                candidates.append({"path": path, "resolved": canonical, "selected": chosen is None})
                if chosen is None:
                    chosen = path
                    masked = canonical == "/dev/null"
            except FileNotFoundError:
                continue
            except (OSError, InputError) as exc:
                # A broken higher priority entry must not silently select the vendor file.
                issues.append(diagnostic("unit-unreadable", type(exc).__name__, path=path))
                if chosen is None:
                    chosen = path
        if chosen is not None:
            break
    if chosen is None:
        issues.append(diagnostic("unit-missing", "Unit not found in configured load paths"))
        return {
            "requested": name,
            "canonical": name,
            "selected": None,
            "masked": False,
            "candidates": candidates,
            "dropins": [],
            "names": [name],
            "diagnostics": issues,
        }
    canonical_name = name
    try:
        resolved = "/dev/null" if masked else fs.resolve(chosen)
        candidate_name = posixpath.basename(resolved)
        if candidate_name != name and not masked:
            validate_name(candidate_name)
            suffix = "." + name.rsplit(".", 1)[1]
            if candidate_name.endswith("@" + suffix) and "@" in name:
                canonical_name = candidate_name.split("@", 1)[0] + "@" + name.split("@", 1)[1]
            else:
                canonical_name = candidate_name
    except (OSError, InputError):
        pass
    aliases = list(dict.fromkeys([canonical_name, name]))
    # Discover aliases in configured directories, including aliases not used to request this unit.
    if not masked:
        for directory in paths:
            try:
                for entry in fs.listdir(directory):
                    if not UNIT_NAME.fullmatch(entry):
                        continue
                    path = directory + "/" + entry
                    try:
                        if fs.link(path) is not None and fs.resolve(path) == fs.resolve(chosen):
                            if "@" in name and entry.endswith("@." + name.rsplit(".", 1)[1]):
                                entry = entry.split("@", 1)[0] + "@" + name.split("@", 1)[1]
                            if entry not in aliases:
                                if len(aliases) >= fs.limits.nodes:
                                    issues.append(
                                        diagnostic(
                                            "alias-budget",
                                            "Alias count limit exceeded",
                                            path=directory,
                                        )
                                    )
                                    break
                                aliases.append(entry)
                    except (OSError, InputError):
                        continue
            except FileNotFoundError:
                continue
            except (OSError, InputError) as exc:
                issues.append(
                    diagnostic("alias-discovery-incomplete", type(exc).__name__, path=directory)
                )
    choices: dict[str, tuple[tuple[int, int, int], dict[str, Any]]] = {}
    shadowed: list[dict[str, Any]] = []
    for alias_index, alias in enumerate(aliases):
        for specificity, scope in enumerate(hierarchy(alias)):
            for rank, directory in enumerate(paths):
                folder = directory + "/" + scope + ".d"
                try:
                    for filename in fs.listdir(folder):
                        if not filename.endswith(".conf"):
                            continue
                        if len(choices) + len(shadowed) >= fs.limits.files:
                            raise InputError("drop-in count limit exceeded")
                        path = folder + "/" + filename
                        record = {
                            "path": path,
                            "name": filename,
                            "selected": True,
                            "masked": fs.resolve(path, allow_mask=True) == "/dev/null",
                        }
                        priority = (specificity, -rank, -alias_index)
                        previous = choices.get(filename)
                        if previous is None or priority > previous[0]:
                            if previous:
                                previous[1]["selected"] = False
                                shadowed.append(previous[1])
                            choices[filename] = (priority, record)
                        elif previous[1]["path"] != path:
                            record["selected"] = False
                            shadowed.append(record)
                except FileNotFoundError:
                    continue
                except (OSError, InputError) as exc:
                    issues.append(diagnostic("dropin-unreadable", type(exc).__name__, path=folder))
    dropins = [choices[key][1] for key in sorted(choices)] + shadowed
    return {
        "requested": name,
        "canonical": canonical_name,
        "selected": chosen,
        "masked": masked,
        "candidates": candidates,
        "dropins": dropins,
        "names": aliases,
        "diagnostics": issues,
    }
