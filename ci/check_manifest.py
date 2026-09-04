#!/usr/bin/env python3
"""Validate the toy-projects CI manifest and emit matrix JSON.

The workflow deliberately keeps project/build metadata in one checked-in JSON
file. This script is dependency-free so the discovery job can run before any
toolchain setup, and its path validation keeps manifest-controlled values safe
to pass to later shell steps.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

MANIFEST_PATH = Path("ci/projects.json")
SAFE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]*$")
SAFE_PATH = re.compile(r"^[A-Za-z0-9._/-]+$")
SAFE_TARGET = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.+@-]*$")
# Every enabled GUI project gets one leg for each supported major. Keeping this
# in discovery, rather than duplicating entries in projects.json, means a new
# project cannot accidentally enter only half of the GUI matrix.
SUPPORTED_QT_MAJORS = (5, 6)

# Changes in these paths can alter the meaning of every project check.  Keep
# the list deliberately conservative: a false full run costs time, while a
# missed shared contract can let an affected project pass without being
# exercised.  The path values are repository-relative POSIX paths, matching
# ``git diff --name-only`` output on GitHub's Linux runners.
COMMON_PROJECT_PREFIXES = (
    "ci/",
    ".github/workflows/",
    ".github/actions/",
    "quality-zoo/runner/",
    "quality-zoo/tests/",
)
COMMON_PROJECT_PATHS = frozenset(
    {
        "quality-zoo/manifest.json",
        "quality-zoo/candidate-manifest.json",
    }
)

QUALITY_ZOO_ROOT = "quality-zoo/"
QUALITY_ZOO_SCENARIO_PREFIX = "quality-zoo/scenarios/"
QUALITY_ZOO_FULL_PREFIXES = (
    "quality-zoo/runner/",
    "quality-zoo/tests/",
)
QUALITY_ZOO_MANIFESTS = frozenset(
    {
        "quality-zoo/manifest.json",
        "quality-zoo/candidate-manifest.json",
    }
)
SAFE_SHA = re.compile(r"^[0-9a-fA-F]{7,64}$")


@dataclass(frozen=True)
class ScopeSelection:
    """The explicit project and Quality Zoo scope for one CI invocation.

    ``selected_names`` is the only set passed to project matrices and report
    publication.  ``skipped_names`` is retained separately so a skipped
    project can never accidentally be represented as a green matrix result.
    Quality Zoo has its own scope because its scenarios are not one-to-one
    with the user-facing projects.
    """

    mode: str
    all_names: tuple[str, ...]
    selected_names: tuple[str, ...]
    skipped_names: tuple[str, ...]
    reason: str
    changed_paths: tuple[str, ...]
    quality_zoo_mode: str
    quality_zoo_scenarios: tuple[str, ...]
    quality_zoo_reason: str

    @property
    def is_full(self) -> bool:
        return self.mode == "full"

    @property
    def selected_count(self) -> int:
        return len(self.selected_names)

    @property
    def skipped_count(self) -> int:
        return len(self.skipped_names)


def _required_path(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value or not SAFE_PATH.fullmatch(value):
        raise ValueError(f"invalid {label}: {value!r}")
    path = Path(value)
    if value.startswith("/") or ".." in path.parts:
        raise ValueError(f"unsafe {label}: {value!r}")
    return value


def _validate_project_path(
    project_dir: Path,
    value: str,
    label: str,
    *,
    require_regular_file: bool,
) -> None:
    """Require an existing project-relative path with optional file semantics."""

    project_root = project_dir.resolve()
    try:
        resolved = (project_dir / value).resolve(strict=True)
    except (OSError, RuntimeError) as error:
        raise ValueError(f"{label} does not exist: {value!r}") from error

    try:
        resolved.relative_to(project_root)
    except ValueError as error:
        raise ValueError(f"unsafe {label}: {value!r}") from error

    if require_regular_file and not resolved.is_file():
        raise ValueError(f"{label} must be a regular file: {value!r}")


def discover(
    root: Path = Path("."),
) -> tuple[list[dict[str, str]], list[dict[str, Any]], list[str]]:
    """Validate the manifest and expand every GUI project across Qt majors.

    ``root`` is injectable for the dependency-free unit tests. The workflow
    still calls the default, repository-relative location.
    """

    root = Path(root)
    payload = json.loads((root / MANIFEST_PATH).read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise TypeError("ci/projects.json root must be an object")
    if payload.get("schema") != 1:
        raise ValueError("ci/projects.json must declare schema 1")
    entries = payload.get("projects")
    if not isinstance(entries, list) or not entries:
        raise ValueError("ci/projects.json must contain a non-empty projects list")

    names: list[str] = []
    verify_projects: list[dict[str, str]] = []
    gui_projects: list[dict[str, Any]] = []
    for entry in entries:
        if not isinstance(entry, dict):
            raise TypeError("every project manifest entry must be an object")
        name = entry.get("name")
        if not isinstance(name, str) or not SAFE_NAME.fullmatch(name):
            raise ValueError(f"invalid project name: {name!r}")
        if name in names:
            raise ValueError(f"duplicate project name: {name}")
        project_dir = root / name
        if not project_dir.is_dir() or not (project_dir / "ici.toml").is_file():
            raise ValueError(f"{name} must be a directory containing ici.toml")
        if entry.get("verify") is not True:
            raise ValueError(f"{name} must opt into ici verification")
        # Validate optional native metadata even when callers request only the
        # historical verify/gui tuple. This keeps one manifest parser as the
        # source of truth for every visible CI matrix.
        _native_entry(name, entry)

        names.append(name)
        verify_projects.append({"name": name})

        gui = entry.get("gui")
        if not isinstance(gui, dict) or not isinstance(gui.get("enabled"), bool):
            raise TypeError(f"{name} must declare gui.enabled true or false")
        if gui["enabled"] is False:
            continue

        build_system = gui.get("build_system")
        if build_system not in ("cmake", "qmake"):
            raise ValueError(
                f"{name} has unsupported GUI build system: {build_system!r}"
            )
        descriptor = _required_path(
            gui.get("build_descriptor"), f"{name}.gui.build_descriptor"
        )
        binary = _required_path(gui.get("binary"), f"{name}.gui.binary")
        smoke_arg = _required_path(gui.get("smoke_arg"), f"{name}.gui.smoke_arg")
        if build_system == "cmake" and descriptor != "CMakeLists.txt":
            raise ValueError(
                f"{name} CMake GUI descriptor must be CMakeLists.txt: {descriptor!r}"
            )
        if build_system == "qmake" and not descriptor.endswith(".pro"):
            raise ValueError(
                f"{name} qmake GUI descriptor must end in .pro: {descriptor!r}"
            )
        _validate_project_path(
            project_dir,
            descriptor,
            f"{name}.gui.build_descriptor",
            require_regular_file=True,
        )
        _validate_project_path(
            project_dir,
            smoke_arg,
            f"{name}.gui.smoke_arg",
            require_regular_file=False,
        )
        for qt_major in SUPPORTED_QT_MAJORS:
            gui_projects.append(
                {
                    "name": name,
                    "build_system": build_system,
                    "build_descriptor": descriptor,
                    "gui_binary": binary,
                    "smoke_arg": smoke_arg,
                    "qt_major": qt_major,
                }
            )

    discovered_projects = sorted(
        path.name
        for path in root.iterdir()
        if path.is_dir() and (path / "ici.toml").is_file()
    )
    if sorted(names) != discovered_projects:
        raise ValueError(
            "manifest/project mismatch: "
            f"manifest={sorted(names)!r} discovered={discovered_projects!r}"
        )
    return verify_projects, gui_projects, names


def _native_entry(name: str, entry: Mapping[str, Any]) -> dict[str, str] | None:
    """Validate optional native product metadata without accepting a shell."""

    native = entry.get("native")
    if native is None:
        return None
    if not isinstance(native, dict) or native.get("enabled") is not True:
        raise TypeError(f"{name}.native must declare enabled=true when present")
    if native.get("build_system") != "make":
        raise ValueError(f"{name}.native.build_system must be make")
    build_target = native.get("build_target", "all")
    test_target = native.get("test_target", "test")
    for target, label in (
        (build_target, f"{name}.native.build_target"),
        (test_target, f"{name}.native.test_target"),
    ):
        if not isinstance(target, str) or not SAFE_TARGET.fullmatch(target):
            raise ValueError(f"invalid {label}: {target!r}")
    return {
        "name": name,
        "build_system": "make",
        "build_target": build_target,
        "test_target": test_target,
    }


def discover_native(root: Path = Path(".")) -> list[dict[str, str]]:
    """Validate and return optional native product checks from the manifest."""

    discover(root)
    payload = json.loads((Path(root) / MANIFEST_PATH).read_text(encoding="utf-8"))
    entries = payload["projects"]
    result: list[dict[str, str]] = []
    for entry in entries:
        result_entry = _native_entry(entry["name"], entry)
        if result_entry is not None:
            result.append(result_entry)
    return result


def _normalise_changed_path(value: Any) -> str:
    """Validate one repository-relative path returned by Git."""

    if (
        not isinstance(value, str)
        or not value
        or "\\" in value
        or any(ord(char) < 0x20 for char in value)
    ):
        raise ValueError(f"invalid changed path: {value!r}")
    path = Path(value)
    if path.is_absolute() or "." in path.parts or ".." in path.parts:
        raise ValueError(f"unsafe changed path: {value!r}")
    return value


def _quality_zoo_scenarios(root: Path) -> dict[str, str]:
    """Load the stable scenario registry for safe path-to-scenario mapping."""

    manifest_path = root / "quality-zoo" / "manifest.json"
    payload = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict) or payload.get("schema") != 1:
        raise ValueError("quality-zoo/manifest.json must declare schema 1")
    entries = payload.get("scenarios")
    if not isinstance(entries, list) or not entries:
        raise ValueError("quality-zoo/manifest.json needs a non-empty scenarios list")

    scenarios: dict[str, str] = {}
    scenario_id_pattern = re.compile(r"^[a-z0-9]+(?:[.-][a-z0-9]+)*$")
    for entry in entries:
        if not isinstance(entry, dict) or set(entry) != {"id", "path"}:
            raise ValueError("quality-zoo scenario entry must contain only id and path")
        scenario_id = entry["id"]
        scenario_path = entry["path"]
        if (
            not isinstance(scenario_id, str)
            or not scenario_id_pattern.fullmatch(scenario_id)
            or scenario_id in scenarios
        ):
            raise ValueError(
                f"invalid/duplicate quality-zoo scenario ID: {scenario_id!r}"
            )
        if not isinstance(scenario_path, str) or not scenario_path:
            raise ValueError(f"invalid quality-zoo scenario path: {scenario_path!r}")
        path = Path(scenario_path)
        if (
            path.is_absolute()
            or "\\" in scenario_path
            or "." in path.parts
            or ".." in path.parts
            or not scenario_path.startswith("scenarios/")
        ):
            raise ValueError(f"unsafe quality-zoo scenario path: {scenario_path!r}")
        scenarios[scenario_id] = scenario_path.rstrip("/")
    return scenarios


def _is_common_project_path(path: str) -> bool:
    return path in COMMON_PROJECT_PATHS or any(
        path.startswith(prefix) for prefix in COMMON_PROJECT_PREFIXES
    )


def _scenario_for_path(path: str, scenarios: Mapping[str, str]) -> str | None:
    """Return the manifest scenario owning ``path``, if it is unambiguous."""

    if not path.startswith(QUALITY_ZOO_SCENARIO_PREFIX):
        return None
    relative = path[len("quality-zoo/") :]
    matches = [
        scenario_id
        for scenario_id, scenario_path in scenarios.items()
        if relative == scenario_path or relative.startswith(scenario_path + "/")
    ]
    if len(matches) > 1:
        raise ValueError(f"changed path belongs to multiple scenarios: {path!r}")
    return matches[0] if matches else None


def select_scope(
    names: Sequence[str],
    changed_paths: Iterable[str],
    *,
    event_name: str,
    quality_zoo_scenarios: Mapping[str, str] | None = None,
    force_full: bool = False,
) -> ScopeSelection:
    """Select project and known-answer scopes from an event and changed paths.

    A push/manual/explicit-full run is always full. For a pull request, shared
    CI, runner, manifest, and workflow paths fan out to every project. Ordinary
    project paths select only that project's matrix entries. Quality Zoo
    scenario paths are narrowed independently; unknown Quality Zoo paths use
    an explicit full fallback so a scenario can never be silently omitted.
    """

    all_names = tuple(names)
    raw_paths = tuple(changed_paths)
    if force_full or event_name != "pull_request":
        reason = (
            "explicit full scope requested"
            if force_full
            else f"{event_name} runs the complete project scope"
        )
        return ScopeSelection(
            mode="full",
            all_names=all_names,
            selected_names=all_names,
            skipped_names=(),
            reason=reason,
            changed_paths=raw_paths,
            quality_zoo_mode="all",
            quality_zoo_scenarios=(),
            quality_zoo_reason="full project event runs every Quality Zoo scenario",
        )

    try:
        paths = tuple(_normalise_changed_path(path) for path in raw_paths)
    except ValueError as error:
        return ScopeSelection(
            mode="full",
            all_names=all_names,
            selected_names=all_names,
            skipped_names=(),
            reason=f"{error}; fail-closed full scope",
            changed_paths=raw_paths,
            quality_zoo_mode="all",
            quality_zoo_scenarios=(),
            quality_zoo_reason="invalid diff path; fail-closed full scenario scope",
        )

    if any(_is_common_project_path(path) for path in paths):
        selected = all_names
        project_mode = "full"
        project_reason = "shared CI, manifest, runner, or workflow change"
    else:
        selected = tuple(
            name
            for name in all_names
            if any(path == name or path.startswith(name + "/") for path in paths)
        )
        project_mode = "affected"
        project_reason = (
            "affected project paths" if selected else "no project paths changed"
        )

    skipped = tuple(name for name in all_names if name not in selected)
    scenario_mapping = quality_zoo_scenarios or {}
    qz_paths = [path for path in paths if path.startswith(QUALITY_ZOO_ROOT)]
    if any(
        path in QUALITY_ZOO_MANIFESTS
        or any(path.startswith(prefix) for prefix in QUALITY_ZOO_FULL_PREFIXES)
        for path in qz_paths
    ):
        qz_mode = "all"
        qz_selected: tuple[str, ...] = ()
        qz_reason = "shared Quality Zoo runner, tests, or manifest change"
    elif qz_paths:
        scenario_ids: set[str] = set()
        unknown = False
        for path in qz_paths:
            if path.startswith(QUALITY_ZOO_SCENARIO_PREFIX):
                scenario_id = _scenario_for_path(path, scenario_mapping)
                if scenario_id is None:
                    unknown = True
                else:
                    scenario_ids.add(scenario_id)
            elif path.endswith(".md"):
                # Documentation does not alter a known answer. It is still
                # visible in the discovery summary as a non-selected scope.
                continue
            else:
                unknown = True
        if unknown or (
            qz_paths
            and not scenario_ids
            and not all(path.endswith(".md") for path in qz_paths)
        ):
            qz_mode = "all"
            qz_selected = ()
            qz_reason = "unknown Quality Zoo path; fail-closed full scenario scope"
        elif scenario_ids:
            qz_mode = "selected"
            qz_selected = tuple(sorted(scenario_ids))
            qz_reason = "affected Quality Zoo scenario paths"
        else:
            qz_mode = "skipped"
            qz_selected = ()
            qz_reason = "Quality Zoo documentation only"
    else:
        qz_mode = "skipped"
        qz_selected = ()
        qz_reason = "no Quality Zoo paths changed"

    return ScopeSelection(
        mode=project_mode,
        all_names=all_names,
        selected_names=selected,
        skipped_names=skipped,
        reason=project_reason,
        changed_paths=paths,
        quality_zoo_mode=qz_mode,
        quality_zoo_scenarios=qz_selected,
        quality_zoo_reason=qz_reason,
    )


def _git_diff_paths(root: Path, base_sha: str, head_sha: str) -> tuple[str, ...]:
    """Read changed paths for a PR without invoking a shell."""

    if not SAFE_SHA.fullmatch(base_sha or "") or not SAFE_SHA.fullmatch(head_sha or ""):
        raise ValueError("pull_request base/head SHA is missing or malformed")
    completed = subprocess.run(
        [
            "git",
            "diff",
            "--name-only",
            "--diff-filter=ACDMRTUXB",
            base_sha,
            head_sha,
        ],
        cwd=root,
        capture_output=True,
        check=False,
        text=True,
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or "git diff failed"
        raise RuntimeError(detail)
    return tuple(line for line in completed.stdout.splitlines() if line)


def discover_with_scope(
    root: Path = Path("."),
    *,
    event_name: str | None = None,
    base_sha: str | None = None,
    head_sha: str | None = None,
    changed_paths: Iterable[str] | None = None,
    force_full: bool = False,
) -> tuple[
    list[dict[str, str]],
    list[dict[str, Any]],
    list[str],
    ScopeSelection,
]:
    """Validate the manifest and return matrices narrowed to this CI scope."""

    root = Path(root)
    verify_projects, gui_projects, names = discover(root)
    effective_event = event_name or os.environ.get("GITHUB_EVENT_NAME", "push")
    diff_error: str | None = None
    if changed_paths is None and effective_event == "pull_request" and not force_full:
        try:
            changed_paths = _git_diff_paths(
                root,
                base_sha or os.environ.get("GITHUB_BASE_SHA", ""),
                head_sha or os.environ.get("GITHUB_HEAD_SHA", ""),
            )
        except (OSError, RuntimeError, ValueError) as error:
            diff_error = str(error)
            changed_paths = ()
    elif changed_paths is None:
        changed_paths = ()

    if diff_error is not None:
        selection = ScopeSelection(
            mode="full",
            all_names=tuple(names),
            selected_names=tuple(names),
            skipped_names=(),
            reason=f"pull_request diff unavailable ({diff_error}); fail-closed full scope",
            changed_paths=(),
            quality_zoo_mode="all",
            quality_zoo_scenarios=(),
            quality_zoo_reason="pull_request diff unavailable; fail-closed full scenario scope",
        )
    else:
        scenario_mapping: Mapping[str, str] = {}
        if effective_event == "pull_request":
            try:
                scenario_mapping = _quality_zoo_scenarios(root)
            except (OSError, TypeError, ValueError, json.JSONDecodeError):
                # A changed/invalid registry is already a common full-scope
                # path. For an otherwise unrelated PR, an unavailable mapping
                # is safe to treat as an unknown Quality Zoo path later.
                scenario_mapping = {}
        selection = select_scope(
            names,
            changed_paths,
            event_name=effective_event,
            quality_zoo_scenarios=scenario_mapping,
            force_full=force_full,
        )
    selected_names = set(selection.selected_names)
    selected_verify = [
        project for project in verify_projects if project["name"] in selected_names
    ]
    selected_gui = [
        project for project in gui_projects if project["name"] in selected_names
    ]
    return selected_verify, selected_gui, names, selection


def write_github_outputs(
    verify_projects: list[dict[str, str]],
    gui_projects: list[dict[str, Any]],
    names: list[str],
    selection: ScopeSelection | None = None,
    native_projects: list[dict[str, str]] | None = None,
) -> None:
    output_name = os.environ.get("GITHUB_OUTPUT")
    if not output_name:
        return
    if selection is None:
        selection = ScopeSelection(
            mode="full",
            all_names=tuple(names),
            selected_names=tuple(names),
            skipped_names=(),
            reason="manifest-only discovery",
            changed_paths=(),
            quality_zoo_mode="all",
            quality_zoo_scenarios=(),
            quality_zoo_reason="manifest-only discovery",
        )
    with Path(output_name).open("a", encoding="utf-8") as output:
        output.write(
            "projects=" + json.dumps(verify_projects, separators=(",", ":")) + "\n"
        )
        output.write(
            "gui_projects=" + json.dumps(gui_projects, separators=(",", ":")) + "\n"
        )
        output.write(
            "native_projects="
            + json.dumps(native_projects or [], separators=(",", ":"))
            + "\n"
        )
        output.write("names=" + ",".join(selection.selected_names) + "\n")
        output.write("count=" + str(selection.selected_count) + "\n")
        output.write("gui_count=" + str(len(gui_projects)) + "\n")
        output.write("native_count=" + str(len(native_projects or [])) + "\n")
        output.write("all_names=" + ",".join(selection.all_names) + "\n")
        output.write("all_count=" + str(len(selection.all_names)) + "\n")
        output.write("skipped_names=" + ",".join(selection.skipped_names) + "\n")
        output.write("skipped_count=" + str(selection.skipped_count) + "\n")
        output.write("scope_mode=" + selection.mode + "\n")
        output.write("scope_reason=" + selection.reason + "\n")
        output.write(
            "changed_paths="
            + json.dumps(list(selection.changed_paths), separators=(",", ":"))
            + "\n"
        )
        output.write("quality_zoo_mode=" + selection.quality_zoo_mode + "\n")
        output.write(
            "quality_zoo_scenarios="
            + json.dumps(list(selection.quality_zoo_scenarios), separators=(",", ":"))
            + "\n"
        )
        output.write(
            "quality_zoo_count=" + str(len(selection.quality_zoo_scenarios)) + "\n"
        )
        output.write("quality_zoo_reason=" + selection.quality_zoo_reason + "\n")


def write_github_summary(selection: ScopeSelection) -> None:
    """Write a human-readable scope ledger without inventing PASS results."""

    summary_name = os.environ.get("GITHUB_STEP_SUMMARY")
    if not summary_name:
        return
    selected = ", ".join(f"`{name}`" for name in selection.selected_names) or "없음"
    skipped = ", ".join(f"`{name}`" for name in selection.skipped_names) or "없음"
    if selection.quality_zoo_mode == "all":
        quality = "전체 시나리오"
    elif selection.quality_zoo_mode == "selected":
        quality = ", ".join(f"`{name}`" for name in selection.quality_zoo_scenarios)
    else:
        quality = "실행하지 않음 (PASS 아님)"
    lines = [
        "## CI scope",
        "",
        f"- project mode: `{selection.mode}`",
        f"- selected projects ({selection.selected_count}): {selected}",
        f"- skipped projects ({selection.skipped_count}): {skipped} — not run (not PASS)",
        f"- reason: {selection.reason}",
        f"- Quality Zoo mode: `{selection.quality_zoo_mode}` ({quality})",
        f"- Quality Zoo reason: {selection.quality_zoo_reason}",
    ]
    with Path(summary_name).open("a", encoding="utf-8") as summary:
        summary.write("\n".join(lines) + "\n")


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--event-name",
        default=os.environ.get("GITHUB_EVENT_NAME", "push"),
        help="GitHub event name; pull_request enables path-aware selection",
    )
    parser.add_argument(
        "--base-sha",
        default=os.environ.get("GITHUB_BASE_SHA", ""),
        help="pull request base commit (defaults to GITHUB_BASE_SHA)",
    )
    parser.add_argument(
        "--head-sha",
        default=os.environ.get("GITHUB_HEAD_SHA", ""),
        help="pull request head commit (defaults to GITHUB_HEAD_SHA)",
    )
    parser.add_argument(
        "--changed-path",
        action="append",
        dest="changed_paths",
        help="inject one changed path (primarily for local contract tests)",
    )
    parser.add_argument(
        "--full",
        action="store_true",
        help="force the complete project and Quality Zoo scope",
    )
    args = parser.parse_args()
    try:
        verify_projects, gui_projects, names, selection = discover_with_scope(
            event_name=args.event_name,
            base_sha=args.base_sha,
            head_sha=args.head_sha,
            changed_paths=args.changed_paths,
            force_full=args.full,
        )
        native_projects = discover_native()
    except (OSError, TypeError, ValueError, json.JSONDecodeError) as error:
        raise SystemExit(str(error)) from error
    selected_names = set(selection.selected_names)
    selected_native = [
        project for project in native_projects if project["name"] in selected_names
    ]
    write_github_outputs(
        verify_projects,
        gui_projects,
        names,
        selection,
        selected_native,
    )
    write_github_summary(selection)
    print(
        "validated "
        f"{len(names)} projects; selected={selection.selected_count}; "
        f"skipped={selection.skipped_count}; gui={len(gui_projects)}; "
        f"native={len(selected_native)}; "
        f"mode={selection.mode}; quality-zoo={selection.quality_zoo_mode}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
