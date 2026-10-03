# Changelog

## Unreleased

- Add opt-in system-service DefaultDependencies and Type=dbus dependency rules,
  with synthetic rule origins and unchanged explicit-only defaults.

## 0.1.0

- Independent offline systemd configuration CLI and Python library.
- Source-preserving unit/drop-in resolution, template instances, aliases and masks.
- Typed setting explanations, static environment/command inspection, explicit dependency graphs.
- Redacted snapshots, strict schema loading, offline diff and configurable CI checks.
- Bounded rootfs access, atomic persistence, fixtures and installable wheel/sdist.
