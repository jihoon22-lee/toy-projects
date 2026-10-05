# lens-net

Linux socket, listening port, and network connection forensics engine.

## Overview
`lens-net` parses Linux kernel pseudo-files (`/proc/net/tcp`, `/proc/net/tcp6`, `/proc/net/udp`, `/proc/net/udp6`, `/proc/net/unix`) and process file descriptors (`/proc/[pid]/fd`) with zero-execution to identify:
- All open listening IPv4 and IPv6 ports and their owner processes.
- Established TCP / TCP6 connections and socket queue backlogs.
- Orphan sockets (no associated process).
- TIME_WAIT socket accumulation trends.
- UNIX domain socket paths and IPC bindings.

