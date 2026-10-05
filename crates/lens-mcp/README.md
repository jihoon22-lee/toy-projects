# `lens-mcp`

Model Context Protocol (MCP) server for the Lens Forensic Platform.

## Overview

`lens-mcp` exposes the diagnostic tools of the Lens Platform to AI coding assistants (Claude Desktop, Google Antigravity, Cursor, etc.) via the standard Model Context Protocol (JSON-RPC 2.0 over standard I/O).

This enables AI assistants to autonomously:
- Inspect disk usage and pinpoint runaway log/cache directories.
- Analyze ELF binary symbols, version tags, and ABI breaks.
- Filter high-volume logs with zero-allocation speed.
- Reconstruct multithreaded strace logs and isolate errno regressions.
- Detect systemd service ordering cycles before deployment.
- Predict recompilation scope from C/C++ header modifications.
- Audit Python virtual environments for missing dependencies.

## Supported MCP Tools

| Tool Name | Parameters | Purpose |
|---|---|---|
| `lens_disk_scan` | `path: string`, `json?: bool` | Scan directory and return logical/allocated storage breakdown |
| `lens_disk_duplicates` | `path: string`, `min_size?: int` | Find duplicate files based on SHA-256 (inode-aware) |
| `lens_abi_inspect` | `binary: string` | Inspect ELF dynamic symbols, versions, and DWARF debug info |
| `lens_log_filter` | `path: string`, `query?: string`, `min_level?: string` | Filter high-volume logs with zero heap allocations |
| `lens_trace_analyze` | `trace_file: string` | Parse strace logs into syscall latency and error distributions |
| `lens_sys_cycles` | `dir: string` | Detect dependency ordering cycles in systemd service definitions |
| `lens_build_impact` | `compile_commands: string`, `header: string` | Calculate reverse compilation impact for a modified C/C++ header |
| `lens_env_check` | `venv_path: string` | Check Python virtualenv for missing or unsatisfied packages |

## Claude Desktop Configuration

Add the following to your `claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "lens": {
      "command": "/path/to/toy-projects/target/release/lens-mcp"
    }
  }
}
```

## Running Standalone

```bash
# Start MCP server over stdio
lens-mcp
```
