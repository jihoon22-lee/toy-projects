"""Bounded activation of dependency extras and shortest explanation paths."""

from __future__ import annotations

from collections import deque
from collections.abc import Mapping
from typing import Any

from packaging.requirements import InvalidRequirement, Requirement
from packaging.utils import canonicalize_name

from envlens.diff_compat import _bounded_packaging_text, _marker_matches, _project_values

MAX_GRAPH_REQUIREMENTS = 100_000


def applicable_marker(
    marker: str | None, identity: Mapping[str, Any], extras: set[str]
) -> bool | None:
    values = [
        _marker_matches(marker, {**identity, "extra": extra}) for extra in ["", *sorted(extras)]
    ]
    return True if True in values else None if None in values else False


def dependency_context(
    project: Mapping[str, Any] | None,
    grouped: Mapping[str, list[Mapping[str, Any]]],
    identity: Mapping[str, Any],
) -> tuple[dict[str, set[str]], dict[str, list[str]], bool]:
    active: dict[str, set[str]] = {name: set() for name in grouped}
    graph: dict[str, set[str]] = {}
    queue = deque(["project", *sorted(grouped)])
    visited: set[tuple[str, frozenset[str]]] = set()
    processed = 0
    limited = False
    while queue:
        source = queue.popleft()
        extras = active.get(source, set())
        key = (source, frozenset(extras))
        if key in visited:
            continue
        visited.add(key)
        requirements = (
            _project_values(project)[1]
            if source == "project"
            else [
                requirement
                for distribution in grouped.get(source, [])
                for requirement in distribution.get("metadata", {}).get("requires_dist", [])
            ]
        )
        for raw in requirements:
            processed += 1
            if processed > MAX_GRAPH_REQUIREMENTS:
                limited = True
                break
            if not isinstance(raw, str) or not _bounded_packaging_text(raw):
                continue
            try:
                requirement = Requirement(raw)
            except (InvalidRequirement, RecursionError):
                continue
            marker = str(requirement.marker) if requirement.marker else None
            if applicable_marker(marker, identity, extras) is not True:
                continue
            target = str(canonicalize_name(requirement.name))
            graph.setdefault(source, set()).add(target)
            previous = active.setdefault(target, set())
            requested = {str(canonicalize_name(extra)) for extra in requirement.extras}
            if not requested <= previous:
                previous.update(requested)
                if len(previous) > 64:
                    active[target] = set(sorted(previous)[:64])
                    limited = True
                queue.append(target)
        if limited:
            break
    paths: dict[str, list[str]] = {"project": ["project"]}
    queue = deque(["project"])
    while queue:
        source = queue.popleft()
        if len(paths[source]) >= 64:
            limited = True
            continue
        for target in sorted(graph.get(source, set())):
            if target not in paths:
                paths[target] = [*paths[source], target]
                queue.append(target)
    return active, paths, limited
