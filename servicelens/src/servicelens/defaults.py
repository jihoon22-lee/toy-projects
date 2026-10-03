"""Opt-in, documented system-service default edges for the systemd 255 subset."""

from __future__ import annotations

from typing import Any

from .model import diagnostic


def add_service_defaults(name: str, unit: dict[str, Any], maximum: int) -> None:
    unit["dependency_scope"] = (
        "explicit plus systemd-255 service default/Type=dbus edges; "
        "other implicit/runtime edges not collected"
    )
    resolution = unit["resolution"]
    if not name.endswith(".service") or not resolution["selected"] or resolution["masked"]:
        return
    settings = unit["settings"]
    configured = settings.get("Unit.DefaultDependencies", {})
    value = str(configured.get("value", "yes")).lower()
    known = configured.get("status", "known") == "known"
    enabled = known and value in {"yes", "true", "on", "1", ""}
    disabled = known and value in {"no", "false", "off", "0"}
    edges: list[tuple[str, str, str]] = []
    if enabled:
        edges.extend(
            (relation, target, "service-default-dependencies")
            for relation, target in (
                ("Requires", "sysinit.target"),
                ("After", "sysinit.target"),
                ("After", "basic.target"),
                ("Conflicts", "shutdown.target"),
                ("Before", "shutdown.target"),
            )
        )
    elif not disabled:
        unit["diagnostics"].append(
            diagnostic("default-dependencies-unknown", "DefaultDependencies could not be evaluated")
        )
    service_type = settings.get("Service.Type", {})
    value = service_type.get("value", "")
    if not value and settings.get("Service.BusName", {}).get("value"):
        value = "dbus"
    if service_type.get("status", "known") == "known" and value == "dbus":
        edges.extend(
            (relation, "dbus.socket", "service-type-dbus") for relation in ("Requires", "After")
        )
    existing = {(edge["relation"], edge["target"]) for edge in unit["dependencies"]}
    for relation, target, rule in edges:
        if (relation, target) in existing:
            continue
        if len(unit["dependencies"]) >= maximum:
            unit["diagnostics"].append(diagnostic("edge-budget", "Default edge limit exceeded"))
            break
        unit["dependencies"].append(
            {
                "source": name,
                "target": target,
                "relation": relation,
                "status": "known",
                "origin": {"path": "systemd-255:" + rule, "line": None},
            }
        )
        existing.add((relation, target))
