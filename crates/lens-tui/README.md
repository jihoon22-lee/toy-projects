# `lens-tui`

Interactive Terminal User Interface (TUI) dashboard for the Lens Forensic Platform.

## Overview

`lens-tui` provides a real-time, terminal-native interactive dashboard for browsing storage consumption, inspecting diagnostic artifacts, and navigating directory footprints with VIM-style keybindings (`h`/`j`/`k`/`l`).

Built on `ratatui` and `crossterm`, it runs effortlessly across SSH sessions, container terminals, and headless servers without requiring X11, Wayland, or Qt6 runtimes.

## Key Features

- **Zero-Dependency Terminal Interface**:
  Runs directly in standard terminal emulators via ANSI escape sequences.
- **VIM-Style Navigation**:
  - `j` / `Down`: Move selection down
  - `k` / `Up`: Move selection up
  - `Enter` / `l`: Enter selected directory
  - `Backspace` / `h`: Go to parent directory
  - `r`: Refresh current scan
  - `q` / `Esc`: Exit dashboard
- **Real-Time Size Gauges & Ratios**:
  Displays relative disk footprint percentages, human-readable size formats, and allocation metrics in split visual panels.

## Usage

```bash
# Launch interactive TUI for current directory
lens-tui

# Inspect a specific path
lens-tui /var/log
```
