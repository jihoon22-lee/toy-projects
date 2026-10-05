# `lens-sys`

Systemd unit parser, drop-in override synthesizer, dependency ordering DAG analyzer, and configuration diff engine.

## Overview

`lens-sys` is the modern Rust replacement for `servicelens`. It provides an offline, static-analysis engine for systemd configurations without requiring a running systemd daemon, DBus connection, or elevated root privileges.

It processes unit definitions across `/lib/systemd/system`, `/etc/systemd/system`, and runtime drop-in directories (`<unit>.d/*.conf`), merges overrides, resolves specifiers (`%i`, `%u`, `%n`, `%p`), models inter-unit dependency graphs (`Wants`, `Requires`, `Before`, `After`), detects ordering cycles via Tarjan's Strongly Connected Components algorithm, and generates differential configuration reports across software deployments or system states.

## Key Features

- **Static Offline Analysis**:
  Evaluates unit files directly from disk images, Git repositories, or chroot environments without needing `systemctl` or DBus.
- **Drop-in Override Synthesizer**:
  Emulates systemd's exact multi-tier drop-in precedence (`/etc` overriding `/run` overriding `/lib`), respecting key-clearing assignments (`ExecStart=`, `Environment=`).
- **Specifier Expansion**:
  Supports `%n`, `%N`, `%p`, `%i`, `%u`, `%h`, and `%%` specifiers for template and instantiated units (e.g. `user@1000.service`).
- **Ordering DAG & Cycle Detection**:
  Constructs a directed graph representing `Before` and `After` ordering constraints. Detects cyclic dependencies using Tarjan's SCC algorithm, catching potential boot deadlocks before deployment.
- **Differential Diagnostics (`servicelens.diff/v1`)**:
  Identifies added, removed, and modified units, changed execution paths (`ExecStart`), altered dependency relationships, and newly introduced or broken ordering cycles.

## Architecture

```
  Base Unit Files (.service, .target)       Drop-in Overrides (.conf)
              │                                      │
              └───────────────────┬──────────────────┘
                                  ▼
                        ┌───────────────────┐
                        │   Unit Parser     │
                        │ & Specifier Engine│
                        └─────────┬─────────┘
                                  ▼
                        ┌───────────────────┐
                        │  Drop-in Merger   │
                        └─────────┬─────────┘
                                  ▼
                        ┌───────────────────┐
                        │   OrderingGraph   │
                        │ (Tarjan's SCC DAG)│
                        └─────────┬─────────┘
                                  ▼
                        ┌───────────────────┐
                        │  SystemdSnapshot  │
                        └─────────┬─────────┘
                                  ▼ (diff_systemd)
                        ┌───────────────────┐
                        │    SystemdDiff    │
                        └───────────────────┘
```

## Schemas Supported

- **Snapshot**: `servicelens.snapshot/v1`
- **Diff**: `servicelens.diff/v1`

## API Usage

```rust
use lens_sys::{parse_unit_content, apply_drop_in, OrderingGraph, diff_systemd};

let base = r#"
[Unit]
Description=Primary Web Service
After=network.target

[Service]
ExecStart=/usr/bin/web-server --port=8080
"#;

let mut unit = parse_unit_content(base, "web.service", Some("/lib/systemd/system/web.service"));

let override_conf = r#"
[Service]
ExecStart=
ExecStart=/usr/bin/web-server --port=8443 --ssl
"#;

apply_drop_in(&mut unit, override_conf, "/etc/systemd/system/web.service.d/ssl.conf");

assert_eq!(unit.exec_start, Some("/usr/bin/web-server --port=8443 --ssl".to_string()));
```

## CLI Subcommand

```bash
# Analyze all systemd units in a root directory
lens sys inspect /etc/systemd/system --output systemd-snapshot.json

# Check for ordering cycles across services
lens sys cycles /etc/systemd/system /lib/systemd/system

# Diff two systemd states (e.g. before/after upgrade)
lens sys diff before.json after.json --format markdown
```
