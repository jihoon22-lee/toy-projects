"""Typed static interpretation. Unknown evidence never becomes a successful guess."""

from __future__ import annotations

import posixpath
import re
from collections import deque
from dataclasses import asdict
from pathlib import Path
from typing import Any

from .defaults import add_service_defaults
from .fs import RootFS
from .model import SEMANTICS, SNAPSHOT, VERSION, InputError, Limits, diagnostic
from .resolver import SYSTEM_PATHS, resolve_unit, validate_name
from .syntax import environment_file, parse_unit, words

DEPENDENCIES = {
    "Wants",
    "Requires",
    "Requisite",
    "BindsTo",
    "PartOf",
    "Conflicts",
    "Before",
    "After",
    "OnFailure",
    "OnSuccess",
}
COMMANDS = {
    "ExecStart",
    "ExecStartPre",
    "ExecStartPost",
    "ExecCondition",
    "ExecStop",
    "ExecStopPost",
    "ExecReload",
}
SCALARS = {
    "Unit": {
        "Description",
        "DefaultDependencies",
        "StopWhenUnneeded",
        "RefuseManualStart",
        "RefuseManualStop",
        "IgnoreOnIsolate",
    },
    "Service": {
        "Type",
        "User",
        "Group",
        "WorkingDirectory",
        "RootDirectory",
        "Restart",
        "RestartSec",
        "TimeoutStartSec",
        "TimeoutStopSec",
        "RemainAfterExit",
        "PIDFile",
        "BusName",
        "UMask",
        "NoNewPrivileges",
        "PrivateTmp",
        "ProtectSystem",
        "ProtectHome",
        "DynamicUser",
        "StandardOutput",
        "StandardError",
        "KillMode",
        "Delegate",
    },
    "Install": {"DefaultInstance"},
}
LISTS = {
    "Unit": {"Documentation", "ConditionPathExists", "AssertPathExists"},
    "Service": {
        "Environment",
        "EnvironmentFile",
        "PassEnvironment",
        "UnsetEnvironment",
        "SupplementaryGroups",
        "ExecSearchPath",
    },
    "Install": {"WantedBy", "RequiredBy", "Also", "Alias"},
}
REDACTED = "<redacted>"


def unescape_name(value: str) -> str:
    value = value.replace("-", "/")
    output = bytearray()
    for part in re.split(r"(\\x[0-9a-fA-F]{2})", value):
        if part.startswith("\\x") and len(part) == 4:
            output.append(int(part[2:], 16))
        else:
            output.extend(part.encode("utf-8"))
    decoded = output.decode("utf-8", errors="strict")
    if "\x00" in decoded:
        raise InputError("NUL in unescaped unit name")
    return decoded


def specifiers(value: str, name: str) -> tuple[str, list[str]]:
    stem = name.rsplit(".", 1)[0]
    prefix, _, instance = stem.partition("@")
    mapping = {
        "%": "%",
        "n": name,
        "N": stem,
        "p": prefix,
        "i": instance,
    }
    for code, escaped in (("P", prefix), ("I", instance)):
        try:
            mapping[code] = unescape_name(escaped)
        except (InputError, UnicodeError):
            pass
    unresolved: list[str] = []

    def replace(match: re.Match[str]) -> str:
        code = match[1]
        if code not in mapping:
            unresolved.append("%" + code)
            return match[0]
        return mapping[code]

    expanded = re.sub(r"%(.)", replace, value)
    if value.endswith("%") and not value.endswith("%%"):
        unresolved.append("trailing-percent")
    return expanded, unresolved


def _origin(record: dict[str, Any]) -> dict[str, Any]:
    return {"path": record["path"], "line": record["line"]}


def _interpret(fs: RootFS, name: str, resolved: dict[str, Any]) -> dict[str, Any]:
    issues = list(resolved.pop("diagnostics"))
    ledger: list[dict[str, Any]] = []
    settings: dict[str, dict[str, Any]] = {}
    environments: dict[str, dict[str, Any]] = {}
    commands: dict[str, list[dict[str, Any]]] = {}
    dependencies: list[dict[str, Any]] = []
    dependency_values: dict[str, set[str]] = {}
    sources = [resolved["selected"]] if resolved["selected"] and not resolved["masked"] else []
    if sources:
        sources.extend(d["path"] for d in resolved["dropins"] if d["selected"] and not d["masked"])
    for path in sources:
        try:
            content = fs.read(path)
            if path == resolved["selected"] and not content:
                resolved["masked"] = True
                break
            records, errors = parse_unit(content, path, fs.limits)
            issues.extend(errors)
        except (OSError, UnicodeError, InputError) as exc:
            issues.append(diagnostic("source-unreadable", type(exc).__name__, path=path))
            continue
        for record in records:
            if fs.directives_seen >= fs.limits.directives:
                issues.append(diagnostic("directive-budget", "Directive limit exceeded"))
                break
            fs.directives_seen += 1
            section, key, raw = record["section"], record["key"], record["value"]
            dotted = section + "." + key
            entry = {**record, "id": len(ledger), "action": "unsupported", "status": "unknown"}
            ledger.append(entry)
            if section.startswith("X-") or key.startswith("X-"):
                entry.update(action="ignored-extension", status="known")
                continue
            scalar = key in SCALARS.get(section, set())
            dependency = section == "Unit" and key in DEPENDENCIES
            command = section == "Service" and key in COMMANDS
            listing = key in LISTS.get(section, set()) or command or dependency
            if not scalar and not listing:
                issues.append(
                    diagnostic(
                        "unsupported-directive",
                        "Directive is outside supported semantics",
                        path=path,
                        line=record["line"],
                    )
                )
                settings[dotted] = {
                    "kind": "unknown",
                    "value": raw,
                    "status": "unknown",
                    "origins": [_origin(record)],
                }
                continue
            expanded, unresolved = specifiers(raw, name)
            status = "unknown" if unresolved else "known"
            if unresolved:
                issues.append(
                    diagnostic(
                        "unresolved-specifier",
                        "Context-dependent or unknown specifier",
                        path=path,
                        line=record["line"],
                    )
                )
            try:
                # Unquote before substitution: an instance containing a space must
                # remain one argument when used through %I.
                tokens = (
                    [specifiers(token, name)[0] for token in words(raw)] if listing else [expanded]
                )
            except InputError:
                entry.update(action="invalid", status="unknown")
                issues.append(
                    diagnostic(
                        "invalid-value",
                        "Cannot parse directive value",
                        severity="error",
                        path=path,
                        line=record["line"],
                    )
                )
                settings[dotted] = {
                    "kind": "unknown",
                    "value": raw,
                    "status": "unknown",
                    "origins": [_origin(record)],
                }
                continue
            previous = settings.get(dotted)
            if scalar:
                enumerations = {
                    "Service.Type": {
                        "simple",
                        "exec",
                        "forking",
                        "oneshot",
                        "dbus",
                        "notify",
                        "notify-reload",
                        "idle",
                    },
                    "Service.Restart": {
                        "no",
                        "on-success",
                        "on-failure",
                        "on-abnormal",
                        "on-watchdog",
                        "on-abort",
                        "always",
                    },
                }
                if dotted in enumerations and expanded not in enumerations[dotted]:
                    status = "unknown"
                    issues.append(
                        diagnostic(
                            "invalid-enum",
                            "Unrecognized setting value",
                            severity="error",
                            path=path,
                            line=record["line"],
                        )
                    )
                settings[dotted] = {
                    "kind": "scalar",
                    "value": expanded,
                    "status": status,
                    "origins": [_origin(record)],
                }
                entry.update(action="replace", status=status)
            elif dependency:
                if not expanded:
                    entry.update(action="ignored-empty-dependency", status=status)
                    issues.append(
                        diagnostic(
                            "dependency-not-reset",
                            "Empty dependency assignment does not clear earlier dependencies",
                            severity="warning",
                            path=path,
                            line=record["line"],
                        )
                    )
                    continue
                for token in tokens:
                    try:
                        validate_name(token)
                    except InputError:
                        issues.append(
                            diagnostic(
                                "invalid-dependency",
                                "Invalid or unresolved unit reference",
                                path=path,
                                line=record["line"],
                            )
                        )
                        continue
                    edge = {
                        "source": name,
                        "target": token,
                        "relation": key,
                        "origin": _origin(record),
                        "status": status,
                    }
                    if len(dependencies) < fs.limits.edges:
                        dependencies.append(edge)
                    else:
                        issues.append(diagnostic("edge-budget", "Dependency edge limit exceeded"))
                collected = dependency_values.setdefault(dotted, set())
                collected.update(tokens)
                origins = previous["origins"] if previous else []
                origins.append(_origin(record))
                settings[dotted] = {
                    "kind": "dependency",
                    "value": [],
                    "status": status,
                    "origins": origins,
                }
                entry.update(action="union", status=status)
            else:
                # Commands retain their full value; they are not a token list at the merge layer.
                additions = [raw] if command and raw else tokens
                prior = (
                    previous["value"] if previous and isinstance(previous["value"], list) else []
                )
                merged = prior if expanded else []
                merged.extend(additions if expanded else [])
                origins = previous["origins"] if previous and expanded else []
                origins.append(_origin(record))
                settings[dotted] = {
                    "kind": "commands" if command else "list",
                    "value": merged,
                    "status": "unknown"
                    if status == "unknown"
                    or (expanded and previous and previous["status"] == "unknown")
                    else "known",
                    "origins": origins,
                }
                entry.update(action="append" if expanded else "reset", status=status)
            # Environment= is a variable map with reset, not just an ordinary list.
            if dotted == "Service.Environment":
                if not expanded:
                    environments.clear()
                for token in tokens:
                    variable, separator, value = token.partition("=")
                    if not separator or not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", variable):
                        issues.append(
                            diagnostic(
                                "invalid-environment",
                                "Invalid Environment assignment",
                                severity="error",
                                path=path,
                                line=record["line"],
                            )
                        )
                        continue
                    environments[variable] = {
                        "value": value,
                        "status": status,
                        "origin": _origin(record),
                        "via": "Environment",
                    }
    for dotted, values in dependency_values.items():
        if settings[dotted]["kind"] == "dependency":
            settings[dotted]["value"] = sorted(values)
    environment_history = [{"name": variable, **value} for variable, value in environments.items()]
    envfile_setting = settings.get("Service.EnvironmentFile", {})
    envfiles = envfile_setting.get("value", []) if envfile_setting.get("kind") == "list" else []
    environment_complete = envfile_setting.get("status", "known") == "known"
    for pattern in envfiles:
        optional = pattern.startswith("-")
        pattern = pattern.removeprefix("-")
        try:
            if "%" in pattern:
                raise InputError("unresolved environment path")
            matches = fs.glob(pattern)
            if not matches and not optional:
                environment_complete = False
                issues.append(
                    diagnostic(
                        "environment-file-missing",
                        "Required environment file missing at capture time",
                        severity="error",
                        path=pattern,
                    )
                )
            for path in matches:
                for assignment in environment_file(fs.read(path), path, fs.limits):
                    if fs.directives_seen >= fs.limits.directives:
                        raise InputError("global assignment limit exceeded")
                    fs.directives_seen += 1
                    variable = assignment["name"]
                    env_entry = {
                        "value": assignment["value"],
                        "status": "known",
                        "origin": _origin(assignment),
                        "via": "EnvironmentFile",
                    }
                    environments[variable] = env_entry
                    environment_history.append({"name": variable, **env_entry})
        except (OSError, UnicodeError, InputError) as exc:
            environment_complete = False
            issues.append(
                diagnostic("environment-file-unreadable", type(exc).__name__, path=pattern)
            )
    if "Service.PAMName" in settings:
        environment_complete = False
    if not environment_complete:
        for environment in environments.values():
            environment["status"] = "unknown"
    pre_unset = dict(environments)
    unset_setting = settings.get("Service.UnsetEnvironment", {})
    unset_values = unset_setting.get("value", []) if unset_setting.get("kind") == "list" else []
    for item in unset_values:
        key, separator, value = item.partition("=")
        if key in environments and (not separator or environments[key]["value"] == value):
            del environments[key]
    if settings.get("Service.PassEnvironment", {}).get("value"):
        issues.append(
            diagnostic("manager-environment-unknown", "Manager environment was not collected")
        )
    for key in sorted(COMMANDS):
        setting = settings.get("Service." + key)
        if not setting or setting["kind"] != "commands":
            continue
        commands[key] = []
        for value in setting["value"]:
            try:
                tokens = [specifiers(token, name)[0] for token in words(value)]
                executable = tokens[0] if tokens else ""
                prefixes = ""
                while executable and executable[0] in "-@:+!|":
                    prefixes += executable[0]
                    executable = executable[1:]
                if not executable:
                    raise InputError("missing executable")
                unknown = setting["status"] == "unknown" or "|" in prefixes
                arguments: list[str] = []
                for token in tokens[1:]:
                    if ":" in prefixes:
                        arguments.append(token)
                        continue
                    if "$" in token:

                        def expand(match: re.Match[str]) -> str:
                            nonlocal unknown
                            variable = match[1]
                            if variable not in pre_unset:
                                unknown = True
                                return match[0]
                            if pre_unset[variable]["status"] != "known":
                                unknown = True
                            return str(pre_unset[variable]["value"])

                        token = re.sub(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}", expand, token)
                        if "$" in token:
                            # $VAR splitting and manager-provided variables need further context.
                            unknown = True
                    arguments.append(token)
                existence = "unknown"
                if executable.startswith("/") and not settings.get("Service.RootDirectory"):
                    try:
                        existence = "present" if fs.exists(executable) else "missing-at-capture"
                    except (OSError, InputError):
                        pass
                commands[key].append(
                    {
                        "executable": executable,
                        "prefixes": prefixes,
                        "arguments": arguments,
                        "status": "unknown" if unknown else "known",
                        "existence": existence,
                    }
                )
                if unknown:
                    issues.append(
                        diagnostic(
                            "command-context-unknown",
                            "Command has unresolved expansion or unsupported shell prefix",
                        )
                    )
            except InputError:
                issues.append(
                    diagnostic("invalid-command", "Cannot tokenize command", severity="error")
                )
    starts = commands.get("ExecStart", [])
    service_type = settings.get("Service.Type", {}).get("value", "simple")
    if name.endswith(".service") and not resolved["masked"] and resolved["selected"]:
        if len(starts) > 1 and service_type != "oneshot":
            issues.append(
                diagnostic(
                    "multiple-execstart",
                    "Multiple ExecStart commands require Type=oneshot",
                    severity="error",
                )
            )
        if not starts and not (
            commands.get("ExecStop")
            and settings.get("Service.RemainAfterExit", {}).get("value") in ("yes", "true", "1")
        ):
            issues.append(
                diagnostic(
                    "missing-execstart",
                    "Service has no supported start/stop configuration",
                    severity="error",
                )
            )
    # Dependency links are configuration facts, not claims about active daemon state.
    if not resolved["masked"]:
        for alias in resolved["names"]:
            for directory in fs_paths(fs):
                for suffix, relation in (("wants", "Wants"), ("requires", "Requires")):
                    folder = directory + "/" + alias + "." + suffix
                    try:
                        for target in fs.listdir(folder):
                            validate_name(target)
                            link_path = folder + "/" + target
                            link = fs.link(link_path)
                            if link is None:
                                continue
                            destination = posixpath.normpath(posixpath.join(folder, link))
                            if not any(destination.startswith(p + "/") for p in fs_paths(fs)):
                                issues.append(
                                    diagnostic(
                                        "dependency-link-target",
                                        "Dependency link target is outside unit load paths",
                                        path=link_path,
                                    )
                                )
                                continue
                            try:
                                validate_name(posixpath.basename(destination))
                            except InputError:
                                issues.append(
                                    diagnostic(
                                        "dependency-link-target",
                                        "Dependency link target is not a unit filename",
                                        path=link_path,
                                    )
                                )
                                continue
                            if "@" in name and "@." in target:
                                target = target.split("@", 1)[0] + "@" + name.split("@", 1)[1]
                            if len(dependencies) >= fs.limits.edges:
                                raise InputError("dependency edge limit exceeded")
                            dependencies.append(
                                {
                                    "source": name,
                                    "target": target,
                                    "relation": relation,
                                    "origin": {"path": link_path, "line": None},
                                    "status": "known",
                                }
                            )
                    except FileNotFoundError:
                        continue
                    except (OSError, InputError) as exc:
                        issues.append(
                            diagnostic(
                                "dependency-links-incomplete", type(exc).__name__, path=folder
                            )
                        )
    return {
        "resolution": resolved,
        "settings": settings,
        "ledger": ledger,
        "environment": environments,
        "environment_history": environment_history,
        "commands": commands,
        "dependencies": dependencies,
        "diagnostics": issues,
        "dependency_scope": "explicit-only; implicit/default/runtime edges not collected",
    }


# Keep load paths on the capture object without exposing them to untrusted input metadata.
class CaptureFS(RootFS):
    def __init__(self, root: str | Path, limits: Limits, paths: tuple[str, ...]) -> None:
        super().__init__(root, limits)
        self.paths = paths


def fs_paths(fs: RootFS) -> tuple[str, ...]:
    return fs.paths if isinstance(fs, CaptureFS) else SYSTEM_PATHS


def _redact(snapshot: dict[str, Any]) -> None:
    for unit in snapshot["units"].values():
        for entry in unit["ledger"]:
            # Whitelist structural values only. Unknown settings and all command/environment
            # evidence are private by default, including superseded assignments.
            safe = entry["section"] == "Unit" and entry["key"] in DEPENDENCIES
            safe |= entry["section"] == "Service" and entry["key"] in {
                "Type",
                "Restart",
                "RestartSec",
                "TimeoutStartSec",
                "TimeoutStopSec",
                "RemainAfterExit",
                "NoNewPrivileges",
                "PrivateTmp",
                "ProtectSystem",
                "ProtectHome",
            }
            if not safe and entry["value"]:
                entry["value"] = REDACTED
        for key, setting in unit["settings"].items():
            safe = setting["kind"] == "dependency" or key in {
                "Service.Type",
                "Service.Restart",
                "Service.RestartSec",
                "Service.TimeoutStartSec",
                "Service.TimeoutStopSec",
                "Service.RemainAfterExit",
                "Service.NoNewPrivileges",
                "Service.PrivateTmp",
                "Service.ProtectSystem",
                "Service.ProtectHome",
            }
            if not safe:
                setting["value"] = REDACTED
                setting["redacted"] = True
        for value in unit["environment"].values():
            value["value"] = REDACTED
        for entry in unit["environment_history"]:
            entry["value"] = REDACTED
        for commands in unit["commands"].values():
            for command in commands:
                command["arguments"] = [REDACTED] * len(command["arguments"])


def inspect(
    unit: str,
    *,
    root: str | Path = "/",
    paths: tuple[str, ...] = SYSTEM_PATHS,
    limits: Limits | None = None,
    redact: bool = True,
    include_defaults: bool = False,
) -> dict[str, Any]:
    """Collect disk configuration only. Returned snapshots are redacted by default."""
    validate_name(unit)
    limits = limits or Limits()
    if not paths or any(not p.startswith("/") or ".." in p.split("/") for p in paths):
        raise InputError("load paths must be absolute rootfs paths without '..'")
    fs = CaptureFS(root, limits, paths)
    units: dict[str, Any] = {}
    queue = deque([(unit, 0)])
    queued = {unit}
    seen: set[str] = set()
    edge_count = 0
    top_issues: list[dict[str, Any]] = []
    try:
        while queue:
            name, depth = queue.popleft()
            if name in seen:
                continue
            if len(seen) >= limits.nodes:
                top_issues.append(diagnostic("node-budget", "Unit count limit exceeded"))
                break
            seen.add(name)
            try:
                result = _interpret(fs, name, resolve_unit(fs, name, paths))
                if include_defaults:
                    add_service_defaults(name, result, limits.edges)
                units[name] = result
                remaining = limits.edges - edge_count
                if len(result["dependencies"]) > remaining:
                    result["dependencies"] = result["dependencies"][:remaining]
                    top_issues.append(diagnostic("edge-budget", "Global edge limit exceeded"))
                edge_count += len(result["dependencies"])
                for edge in result["dependencies"]:
                    target = edge["target"]
                    if depth < limits.depth and target not in queued:
                        if len(queued) < limits.nodes:
                            queue.append((target, depth + 1))
                            queued.add(target)
                        else:
                            top_issues.append(
                                diagnostic("node-budget", "Unit count limit exceeded")
                            )
                if depth == limits.depth and any(
                    e["target"] not in seen for e in result["dependencies"]
                ):
                    top_issues.append(
                        diagnostic("graph-depth", "Dependency traversal depth reached")
                    )
            except (OSError, UnicodeError, InputError) as exc:
                top_issues.append(diagnostic("unit-analysis-incomplete", type(exc).__name__))
        # Requirement cycles are legal in isolation. Only the separate ordering
        # relation is checked here, without inferring implicit/default edges.
        successors: dict[str, set[str]] = {name: set() for name in units}
        indegree = {name: 0 for name in units}
        for result in units.values():
            for edge in result["dependencies"]:
                source, target = edge["source"], edge["target"]
                if edge["relation"] not in ("Before", "After") or target not in units:
                    continue
                if edge["relation"] == "After":
                    source, target = target, source
                if target not in successors[source]:
                    successors[source].add(target)
                    indegree[target] += 1
        ready = deque(name for name, degree in indegree.items() if degree == 0)
        ordered = 0
        while ready:
            source = ready.popleft()
            ordered += 1
            for target in successors[source]:
                indegree[target] -= 1
                if indegree[target] == 0:
                    ready.append(target)
        if ordered < len(units):
            top_issues.append(
                diagnostic(
                    "ordering-cycle", "Explicit ordering graph contains a cycle", severity="error"
                )
            )
        all_issues = top_issues + [
            issue for result in units.values() for issue in result["diagnostics"]
        ]
        snapshot: dict[str, Any] = {
            "schema": SNAPSHOT,
            "version": VERSION,
            "semantics": SEMANTICS,
            "requested": unit,
            "context": {
                "scope": "system",
                "mode": "static-disk",
                "load_paths": list(paths),
                "limits": asdict(limits),
            },
            "redacted": redact,
            "partial": any(d["severity"] in ("error", "unknown") for d in all_issues),
            "units": units,
            "files": sorted(fs.files.values(), key=lambda x: x["path"]),
            "diagnostics": top_issues,
        }
        if redact:
            _redact(snapshot)
        return snapshot
    finally:
        fs.close()
