# Changelog

## Unreleased

- Validate supported scalar assignments with directive-specific systemd-255 grammars,
  normalization and empty/reset behavior. Invalid assignments remain unknown and fail
  error checks; earlier valid candidates retain their provenance.
- Correct boolean aliases, duration and octal handling, Delegate controller unions,
  and specifier expansion boundaries. Keep the existing v1 snapshot schema.

## 0.1.0 (2026-10-03)

- Add opt-in system-service DefaultDependencies and Type=dbus dependency rules,
  with synthetic rule origins and unchanged explicit-only defaults.

### Features

* expand eight independent diagnostic tools and verified releases ([#120](https://github.com/jihoon22-lee/toy-projects/issues/120)) ([3a01bb9](https://github.com/jihoon22-lee/toy-projects/commit/3a01bb93c3fee936a335bb9445400b5f469d6a87))

- Independent offline systemd configuration CLI and Python library.
- Source-preserving unit/drop-in resolution, template instances, aliases and masks.
- Typed setting explanations, static environment/command inspection, explicit dependency graphs.
- Redacted snapshots, strict schema loading, offline diff and configurable CI checks.
- Bounded rootfs access, atomic persistence, fixtures and installable wheel/sdist.
