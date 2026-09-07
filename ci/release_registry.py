#!/usr/bin/env python3
"""Derive each product's release asset list from ``ci/projects.json``.

A release asset list is not free-form text: it is a consequence of what the
product actually builds. AbiLens has no Python packaging, so it cannot ship a
wheel; EnvLens has no linker output, so it cannot ship a native bundle. Putting
that in one registry means adding a product is a data change, and it means the
publish audit and the workflow that produced the files read the same list
rather than two hand-written lists that agree until one is edited.

Dependency-free standard library only: the release workflow's first job runs
before any toolchain setup.
"""

from __future__ import annotations

import json
from collections.abc import Mapping
from pathlib import Path
from typing import Any

MANIFEST_PATH = Path("ci/projects.json")

# The order is the published order of BuildScope 0.5.0's nine assets, and
# ``test_generic_names_reproduce_the_published_buildscope_nine`` holds it
# there. A product that ships a subset keeps this relative order, so two
# releases never disagree about where a file belongs in ``SHA256SUMS``.
_ARTIFACT_KINDS = ("pyz", "wheel", "sdist", "native_bundle")

_KNOWN_KEYS = frozenset({"enabled", "display_name", "artifacts"})


class ReleaseRegistryError(ValueError):
    """The manifest's release metadata is missing or malformed."""


class ReleaseSpec:
    """One product's release contract, derived from the manifest."""

    def __init__(self, name: str, display_name: str, artifacts: Mapping[str, bool]):
        self.name = name
        self.display_name = display_name
        self.artifacts = dict(artifacts)

    @property
    def tag_prefix(self) -> str:
        """Return the tag prefix, derived so it cannot disagree with the name."""

        return f"{self.name}-v"

    def ships(self, kind: str) -> bool:
        return self.artifacts.get(kind, False)

    def expected_asset_names(self, version: str) -> tuple[str, ...]:
        """Return the exact asset names this product publishes for ``version``."""

        name = self.name
        names: list[str] = []
        if self.ships("pyz"):
            names += [f"{name}.pyz", f"{name}.pyz.sha256"]
        if self.ships("wheel"):
            names.append(f"{name}-{version}-py3-none-any.whl")
        if self.ships("sdist"):
            names.append(f"{name}-{version}.tar.gz")
        names += [f"{name}-ici-deep.json", f"{name}-ici-deep.html"]
        names.append(f"{name}-provenance.json")
        if self.ships("native_bundle"):
            names.append(f"{name}-{version}-linux-x86_64.tar.gz")
        names.append("SHA256SUMS")
        return tuple(names)


def _validate_release(name: str, entry: Mapping[str, Any]) -> ReleaseSpec | None:
    """Validate one project's optional ``release`` block."""

    release = entry.get("release")
    if release is None:
        return None
    if not isinstance(release, dict):
        raise ReleaseRegistryError(f"{name}.release must be an object")
    unknown = sorted(set(release) - _KNOWN_KEYS)
    if unknown:
        raise ReleaseRegistryError(f"{name}.release has unknown keys: {unknown}")
    if release.get("enabled") is not True:
        raise ReleaseRegistryError(
            f"{name}.release must declare enabled=true when present"
        )
    display_name = release.get("display_name")
    if not isinstance(display_name, str) or not display_name.strip():
        raise ReleaseRegistryError(f"invalid {name}.release.display_name")

    artifacts = release.get("artifacts")
    if not isinstance(artifacts, dict):
        raise ReleaseRegistryError(f"{name}.release.artifacts must be an object")
    unknown_kinds = sorted(set(artifacts) - set(_ARTIFACT_KINDS))
    if unknown_kinds:
        raise ReleaseRegistryError(
            f"{name}.release.artifacts has unknown kinds: {unknown_kinds}"
        )
    missing = sorted(set(_ARTIFACT_KINDS) - set(artifacts))
    if missing:
        # Every kind is stated, including the false ones. A product that gains
        # Python packaging must then edit this line, rather than inheriting a
        # default that quietly keeps the wheel out of the published list.
        raise ReleaseRegistryError(
            f"{name}.release.artifacts must state every kind; missing: {missing}"
        )
    for kind, value in artifacts.items():
        if not isinstance(value, bool):
            raise ReleaseRegistryError(
                f"{name}.release.artifacts.{kind} must be a boolean, got {value!r}"
            )
    if not any(artifacts[kind] for kind in ("pyz", "wheel", "sdist", "native_bundle")):
        raise ReleaseRegistryError(
            f"{name}.release ships no build output; it would publish only reports"
        )
    return ReleaseSpec(name, display_name, artifacts)


def load_release_specs(root: Path = Path(".")) -> dict[str, ReleaseSpec]:
    """Return every releasable product keyed by name."""

    payload = json.loads((Path(root) / MANIFEST_PATH).read_text(encoding="utf-8"))
    projects = payload.get("projects")
    if not isinstance(projects, list):
        raise ReleaseRegistryError("manifest projects must be a list")
    specs: dict[str, ReleaseSpec] = {}
    for entry in projects:
        if not isinstance(entry, dict):
            raise ReleaseRegistryError("each manifest project must be an object")
        name = entry.get("name")
        if not isinstance(name, str):
            raise ReleaseRegistryError("each manifest project needs a string name")
        spec = _validate_release(name, entry)
        if spec is not None:
            specs[name] = spec
    return specs


def spec_for_tag(tag: str, root: Path = Path(".")) -> tuple[ReleaseSpec, str]:
    """Resolve ``<product>-v<version>`` to its spec and version.

    The tag is the only untrusted input the release workflow starts from, so
    it is resolved against the registry before anything else runs. An unknown
    product, or a product with no release block, stops here.
    """

    specs = load_release_specs(root)
    for name, spec in sorted(specs.items()):
        if tag.startswith(spec.tag_prefix):
            return spec, tag[len(spec.tag_prefix) :]
    known = ", ".join(sorted(specs)) or "(none)"
    raise ReleaseRegistryError(
        f"tag {tag!r} does not name a releasable product; known products: {known}"
    )
