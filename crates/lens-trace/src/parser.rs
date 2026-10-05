use crate::model::*;
use std::collections::{BTreeMap, HashMap};

#[derive(Debug, Clone)]
struct PendingCall {
    syscall: String,
    partial_args: String,
    tid: u64,
    timestamp: Option<String>,
}

pub struct TraceAnalyzer {
    pub max_retained_events: usize,
}

impl Default for TraceAnalyzer {
    fn default() -> Self {
        Self {
            max_retained_events: 10_000,
        }
    }
}

impl TraceAnalyzer {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn analyze_lines<'a, I>(&self, lines: I) -> TraceSnapshot
    where
        I: IntoIterator<Item = &'a str>,
    {
        let mut total_events = 0u64;
        let mut total_calls = 0u64;
        let mut total_errors = 0u64;

        let mut syscalls: BTreeMap<String, SyscallStats> = BTreeMap::new();
        let mut errors: BTreeMap<String, u64> = BTreeMap::new();
        let mut processes: BTreeMap<String, ProcessInfo> = BTreeMap::new();
        let mut events: Vec<TraceEvent> = Vec::new();
        let mut pending: HashMap<u64, PendingCall> = HashMap::new();
        // fd tables are per-process: tid -> open fd set. A global set would
        // let process B's close(3) erase process A's open fd 3.
        let mut open_fds: BTreeMap<u64, std::collections::BTreeSet<u64>> = BTreeMap::new();
        let mut leaked_fds: BTreeMap<u64, std::collections::BTreeSet<u64>> = BTreeMap::new();
        let mut io_read_bytes = 0u64;
        let mut io_write_bytes = 0u64;

        for line in lines {
            let trimmed = line.trim();
            if trimmed.is_empty() {
                continue;
            }

            let (tid, timestamp, body) = split_tid_and_body(trimmed);

            // Handle process terminators: `exited with N`, `exited (status
            // N)` (strace <= 4.6), `killed by SIGX`, `superseded by execve`.
            // All end the process's lifetime on this tid.
            if body.starts_with("+++") {
                let proc_key = tid.to_string();
                processes
                    .entry(proc_key)
                    .or_insert_with(|| ProcessInfo {
                        tid,
                        generation: 0,
                        calls: 0,
                        parent: String::new(),
                        relation: "root".to_string(),
                        exited: true,
                    })
                    .exited = true;
                pending.remove(&tid);
                // Any fd still open at exit is a leak for this process; move
                // it out of the live table so a recycled tid starts fresh.
                if let Some(fds) = open_fds.remove(&tid) {
                    if !fds.is_empty() {
                        leaked_fds.entry(tid).or_default().extend(fds);
                    }
                }
                continue;
            }

            // Handle signal markers
            if body.starts_with("--- ") && body.ends_with(" ---") {
                continue;
            }

            total_events += 1;

            // Handle <unfinished ...>
            if body.ends_with("<unfinished ...>") {
                if let Some(open) = body.find('(') {
                    let syscall = body[..open].trim().to_string();
                    let raw_args = &body[open + 1..body.len() - 16];
                    let partial_args = raw_args.trim_end_matches([',', ' ']).to_string();
                    pending.insert(
                        tid,
                        PendingCall {
                            syscall,
                            partial_args,
                            tid,
                            timestamp: timestamp.map(String::from),
                        },
                    );
                }
                continue;
            }

            // Handle <... resumed>
            let event = if body.starts_with("<... ") {
                if let Some(mark) = body.find(" resumed>") {
                    let resumed_syscall = &body[5..mark];
                    let rest = body[mark + 9..].trim();
                    if let Some(pending_call) = pending.remove(&tid) {
                        if pending_call.syscall == resumed_syscall {
                            parse_resumed_tail(&pending_call, rest)
                        } else {
                            parse_strace_body(tid, timestamp, body)
                        }
                    } else {
                        parse_strace_body(tid, timestamp, body)
                    }
                } else {
                    parse_strace_body(tid, timestamp, body)
                }
            } else {
                parse_strace_body(tid, timestamp, body)
            };

            if let Some(ev) = event {
                total_calls += 1;

                // Track process stats. A tid reused after `+++ exited` starts
                // a new generation rather than conflating both processes.
                let proc_key = ev.tid.to_string();
                let proc = processes.entry(proc_key).or_insert_with(|| ProcessInfo {
                    tid: ev.tid,
                    generation: 0,
                    calls: 0,
                    parent: String::new(),
                    relation: "root".to_string(),
                    exited: false,
                });
                if proc.exited {
                    proc.generation += 1;
                    proc.calls = 0;
                    proc.exited = false;
                }
                proc.calls += 1;

                // Track syscall stats
                let stat = syscalls.entry(ev.syscall.clone()).or_default();
                stat.count += 1;

                if let Some(ns) = ev.duration_ns {
                    stat.known_duration += 1;
                    stat.total_ns += ns;
                    if ns > stat.max_ns {
                        stat.max_ns = ns;
                    }
                }

                if let Some(ref err) = ev.error {
                    total_errors += 1;
                    stat.errors += 1;
                    *errors.entry(err.clone()).or_default() += 1;
                }

                // FD leak & I/O tracking
                if ev.error.is_none() {
                    match ev.syscall.as_str() {
                        // Syscalls whose return value is a new file descriptor.
                        "open" | "openat" | "openat2" | "creat" | "socket" | "accept"
                        | "accept4" | "dup" | "dup2" | "dup3" | "epoll_create"
                        | "epoll_create1" | "eventfd" | "eventfd2" | "signalfd" | "signalfd4"
                        | "timerfd_create" | "inotify_init" | "inotify_init1" | "memfd_create"
                        | "pidfd_open" | "perf_event_open" | "fanotify_init" | "bpf" => {
                            if let Ok(fd) = ev.result.parse::<u64>() {
                                open_fds.entry(ev.tid).or_default().insert(fd);
                            }
                        }
                        // pipe/pipe2/socketpair hand back both ends in the
                        // argument array, e.g. pipe2([3,4], O_CLOEXEC) = 0
                        "pipe" | "pipe2" | "socketpair" => {
                            if let (Ok(0), Some(fds)) =
                                (ev.result.parse::<u64>(), parse_fd_array(&ev.arguments))
                            {
                                let table = open_fds.entry(ev.tid).or_default();
                                for fd in fds {
                                    table.insert(fd);
                                }
                            }
                        }
                        // fork/vfork/clone/clone3 return the new tid; the
                        // child inherits the parent's fd table.
                        "fork" | "vfork" | "clone" | "clone3" => {
                            if let Ok(child) = ev.result.parse::<u64>() {
                                if child > 0 {
                                    let inherited =
                                        open_fds.get(&ev.tid).cloned().unwrap_or_default();
                                    open_fds.insert(child, inherited);
                                    processes.entry(child.to_string()).or_insert_with(|| {
                                        ProcessInfo {
                                            tid: child,
                                            generation: 0,
                                            calls: 0,
                                            parent: ev.tid.to_string(),
                                            relation: ev.syscall.clone(),
                                            exited: false,
                                        }
                                    });
                                }
                            }
                        }
                        "close" => {
                            if let Some(fd) = fd_from_arg(&ev.arguments) {
                                if let Some(table) = open_fds.get_mut(&ev.tid) {
                                    table.remove(&fd);
                                }
                            }
                        }
                        "read" | "pread" | "pread64" | "recv" | "recvfrom" | "recvmsg" => {
                            if let Ok(bytes) = ev.result.parse::<u64>() {
                                io_read_bytes += bytes;
                            }
                        }
                        "write" | "pwrite" | "pwrite64" | "send" | "sendto" | "sendmsg" => {
                            if let Ok(bytes) = ev.result.parse::<u64>() {
                                io_write_bytes += bytes;
                            }
                        }
                        _ => {}
                    }
                }

                if events.len() < self.max_retained_events {
                    events.push(ev);
                }
            }
        }

        // Fds open at end-of-trace are leaks for still-running processes.
        for (tid, fds) in open_fds {
            if !fds.is_empty() {
                leaked_fds.entry(tid).or_default().extend(fds);
            }
        }
        let fd_leaks_by_process: BTreeMap<String, Vec<u64>> = leaked_fds
            .iter()
            .map(|(tid, fds)| (tid.to_string(), fds.iter().copied().collect()))
            .collect();
        let mut fd_leaks: Vec<u64> = leaked_fds
            .values()
            .flat_map(|fds| fds.iter().copied())
            .collect();
        fd_leaks.sort_unstable();
        fd_leaks.dedup();

        TraceSnapshot {
            schema: SNAPSHOT_SCHEMA_V1.to_string(),
            version: env!("CARGO_PKG_VERSION").to_string(),
            total_events,
            total_calls,
            total_errors,
            syscalls,
            errors,
            processes,
            events,
            fd_leaks,
            fd_leaks_by_process,
            io_read_bytes,
            io_write_bytes,
        }
    }
}

/// Parse a leading fd number that may carry a `strace -y` `<...>`
/// annotation, e.g. `3</etc/hosts>` -> 3.
fn fd_from_arg(arg: &str) -> Option<u64> {
    let head = arg.trim().split('<').next()?.trim();
    head.parse::<u64>().ok()
}

/// Extract an fd array from strace arguments like `[3,4]` in `pipe2([3,4],
/// 0)`. `strace -y` annotates each element (`[3<TCP:[::1]:9>,4<...>]`), so
/// the closing `]` is found while skipping `<...>` regions and each element
/// is parsed by its leading digits.
fn parse_fd_array(arguments: &str) -> Option<Vec<u64>> {
    let open = arguments.find('[')?;
    let bytes = arguments.as_bytes();
    let mut in_anno = false;
    let mut close = None;
    for (i, &b) in bytes.iter().enumerate().skip(open + 1) {
        match b {
            b'<' => in_anno = true,
            b'>' => in_anno = false,
            b']' if !in_anno => {
                close = Some(i);
                break;
            }
            _ => {}
        }
    }
    let inner = &arguments[open + 1..close?];
    let fds: Option<Vec<u64>> = inner.split(',').map(fd_from_arg).collect();
    fds.filter(|v| !v.is_empty())
}

fn split_tid_and_body(line: &str) -> (u64, Option<&str>, &str) {
    let mut s = line.trim();
    let mut tid = 0u64;
    let mut timestamp = None;

    // 1. Check [pid 1234] or 1234 prefix
    if s.starts_with("[pid ") {
        if let Some(end) = s.find(']') {
            let pid_str = s[5..end].trim();
            tid = pid_str.parse().unwrap_or(0);
            s = s[end + 1..].trim();
        }
    } else if let Some(first_word) = s.split_whitespace().next() {
        if let Ok(pid) = first_word.parse::<u64>() {
            tid = pid;
            s = s[first_word.len()..].trim();
        }
    }

    // 2. Check timestamp prefix (e.g. 12:00:00.123456 or 1700000000.000001)
    if let Some(first_word) = s.split_whitespace().next() {
        // Only digits/colons/dots — `fd<TCP:[::1]:9>`-style annotations and
        // syscall names containing ':' must not be mistaken for timestamps.
        if first_word
            .chars()
            .all(|c| c.is_ascii_digit() || c == ':' || c == '.')
            && (first_word.contains(':') || first_word.contains('.'))
        {
            timestamp = Some(first_word);
            s = s[first_word.len()..].trim();
        }
    }

    (tid, timestamp, s)
}

fn parse_resumed_tail(pending: &PendingCall, rest: &str) -> Option<TraceEvent> {
    // rest is like: `"abc", 3) = 3 <0.000020>`
    let close_paren = find_matching_paren_suffix(rest)?;
    let tail_args = rest[..close_paren].trim().trim_start_matches([',', ' ']);
    let arguments = if pending.partial_args.is_empty() {
        tail_args.to_string()
    } else if tail_args.is_empty() {
        pending.partial_args.clone()
    } else {
        format!("{}, {}", pending.partial_args, tail_args)
    };

    let after_paren = rest[close_paren + 1..].trim();
    let (result, error, duration_ns) = parse_return_and_duration(after_paren);

    Some(TraceEvent {
        kind: "call".to_string(),
        syscall: pending.syscall.clone(),
        arguments,
        result,
        error,
        duration_ns,
        tid: pending.tid,
        timestamp: pending.timestamp.clone(),
    })
}

fn parse_strace_body(tid: u64, timestamp: Option<&str>, body: &str) -> Option<TraceEvent> {
    let open_paren = body.find('(')?;
    let syscall = body[..open_paren].trim().to_string();

    let close_paren = find_matching_paren(&body[open_paren..])? + open_paren;
    let arguments = body[open_paren + 1..close_paren].to_string();

    let after_paren = body[close_paren + 1..].trim();
    let (result, error, duration_ns) = parse_return_and_duration(after_paren);

    Some(TraceEvent {
        kind: "call".to_string(),
        syscall,
        arguments,
        result,
        error,
        duration_ns,
        tid,
        timestamp: timestamp.map(String::from),
    })
}

pub fn parse_strace_line(line: &str) -> Option<TraceEvent> {
    let (tid, timestamp, body) = split_tid_and_body(line);
    parse_strace_body(tid, timestamp, body)
}

fn parse_return_and_duration(after_paren: &str) -> (String, Option<String>, Option<u64>) {
    let mut result = String::new();
    let mut error = None;
    let mut duration_ns = None;

    if let Some(eq_idx) = after_paren.find('=') {
        let mut res_part = after_paren[eq_idx + 1..].trim();

        // Extract duration <0.000045>
        if let Some(dur_start) = res_part.rfind('<') {
            if let Some(dur_end) = res_part.rfind('>') {
                if dur_end > dur_start {
                    let dur_str = &res_part[dur_start + 1..dur_end];
                    if let Ok(secs) = dur_str.parse::<f64>() {
                        duration_ns = Some((secs * 1_000_000_000.0) as u64);
                    }
                    res_part = res_part[..dur_start].trim();
                }
            }
        }

        // `strace -y` annotates fd results: `= 3</etc/hosts>` -> `3`.
        if res_part.ends_with('>') {
            if let Some(lt) = res_part.rfind('<') {
                res_part = res_part[..lt].trim();
            }
        }

        result = res_part.to_string();

        // `-1 ERRNO` and `? ERESTART*` are both error/restart returns.
        if res_part == "-1" || res_part.starts_with("-1 ") || res_part.starts_with("? ") {
            let tokens: Vec<&str> = res_part.split_whitespace().collect();
            if tokens.len() >= 2 {
                error = Some(tokens[1].to_string());
            } else {
                error = Some("UNKNOWN_ERROR".to_string());
            }
        }
    }

    (result, error, duration_ns)
}

fn find_matching_paren(s: &str) -> Option<usize> {
    let mut depth = 0;
    let mut in_quote = false;
    let mut escape = false;

    for (idx, ch) in s.char_indices() {
        if in_quote {
            if escape {
                escape = false;
            } else if ch == '\\' {
                escape = true;
            } else if ch == '"' {
                in_quote = false;
            }
            continue;
        }

        if ch == '"' {
            in_quote = true;
        } else if ch == '(' {
            depth += 1;
        } else if ch == ')' {
            depth -= 1;
            if depth == 0 {
                return Some(idx);
            }
        }
    }
    None
}

fn find_matching_paren_suffix(s: &str) -> Option<usize> {
    let mut depth = 1;
    let mut in_quote = false;
    let mut escape = false;

    for (idx, ch) in s.char_indices() {
        if in_quote {
            if escape {
                escape = false;
            } else if ch == '\\' {
                escape = true;
            } else if ch == '"' {
                in_quote = false;
            }
            continue;
        }

        if ch == '"' {
            in_quote = true;
        } else if ch == '(' {
            depth += 1;
        } else if ch == ')' {
            depth -= 1;
            if depth == 0 {
                return Some(idx);
            }
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_fd_leaks_and_io_tracking() {
        let lines = vec![
            "1001 openat(AT_FDCWD, \"/etc/hosts\", O_RDONLY) = 3 <0.000100>",
            "1001 read(3, \"127.0.0.1 localhost\\n\", 1024) = 20 <0.000050>",
            "1001 openat(AT_FDCWD, \"/etc/resolv.conf\", O_RDONLY) = 4 <0.000120>",
            "1001 write(1, \"output log\\n\", 11) = 11 <0.000040>",
            "1001 close(3) = 0 <0.000030>",
        ];

        let analyzer = TraceAnalyzer::new();
        let snapshot = analyzer.analyze_lines(lines);

        // FD 3 was closed, but FD 4 was leaked
        assert_eq!(snapshot.fd_leaks, vec![4]);
        assert_eq!(snapshot.fd_leaks_by_process["1001"], vec![4]);
        assert_eq!(snapshot.io_read_bytes, 20);
        assert_eq!(snapshot.io_write_bytes, 11);
    }

    #[test]
    fn test_fd_tracking_is_per_process() {
        let lines = vec![
            // pid A opens fd 3 and never closes it; pid B closes *its* fd 3.
            "2001 openat(AT_FDCWD, \"/etc/hosts\", O_RDONLY) = 3 <0.000100>",
            "2002 openat(AT_FDCWD, \"/tmp/x\", O_RDONLY) = 3 <0.000110>",
            "2002 close(3) = 0 <0.000030>",
            // pid B exits cleanly; pid A still holds fd 3 at end of trace.
            "2002 +++ exited with 0 +++",
        ];

        let snapshot = TraceAnalyzer::new().analyze_lines(lines);
        // B's close(3) must not erase A's fd 3.
        assert_eq!(snapshot.fd_leaks_by_process["2001"], vec![3]);
        assert!(!snapshot.fd_leaks_by_process.contains_key("2002"));
    }

    #[test]
    fn test_fd_creating_syscalls_and_pipe() {
        let lines = vec![
            "3001 socket(AF_INET, SOCK_STREAM, 0) = 3 <0.000010>",
            "3001 accept4(3, NULL, NULL, SOCK_CLOEXEC) = 4 <0.000020>",
            "3001 pipe2([5,6], O_CLOEXEC) = 0 <0.000010>",
            "3001 dup2(4, 7) = 7 <0.000010>",
            "3001 close(4) = 0 <0.000010>",
        ];
        let snapshot = TraceAnalyzer::new().analyze_lines(lines);
        assert_eq!(snapshot.fd_leaks_by_process["3001"], vec![3, 5, 6, 7]);
    }

    #[test]
    fn test_killed_and_superseded_markers_are_not_syscalls() {
        let lines = vec![
            "4001 openat(AT_FDCWD, \"/x\", O_RDONLY) = 3 <0.000010>",
            "4001 +++ killed by SIGSEGV (core dumped) +++",
            "4002 +++ exited (status 0) +++",
            "4003 +++ superseded by execve in pid 4004 +++",
        ];
        let snapshot = TraceAnalyzer::new().analyze_lines(lines);
        assert_eq!(snapshot.total_calls, 1);
        assert!(!snapshot.syscalls.contains_key("+++ killed by SIGSEGV"));
        assert!(snapshot.processes["4001"].exited);
        assert_eq!(snapshot.fd_leaks_by_process["4001"], vec![3]);
    }

    #[test]
    fn test_strace_y_annotations_do_not_fake_leaks() {
        let lines = vec![
            // `strace -y` annotates fds with `<path>` everywhere.
            "5001 openat(AT_FDCWD, \"/etc/hosts\", O_RDONLY) = 3</etc/hosts> <0.000100>",
            "5001 close(3</etc/hosts>) = 0 <0.000030>",
            "5001 pipe2([4<pipe:[1]>,5<pipe:[1]>], O_CLOEXEC) = 0 <0.000010>",
            "5001 close(4<pipe:[1]>) = 0 <0.000010>",
        ];
        let snapshot = TraceAnalyzer::new().analyze_lines(lines);
        assert_eq!(snapshot.fd_leaks_by_process["5001"], vec![5]);
    }

    #[test]
    fn test_fork_child_inherits_fd_table() {
        let lines = vec![
            "6001 openat(AT_FDCWD, \"/x\", O_RDONLY) = 3 <0.000010>",
            "6001 clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID) = 6002 <0.000020>",
            "6002 close(3) = 0 <0.000010>",
        ];
        let snapshot = TraceAnalyzer::new().analyze_lines(lines);
        assert_eq!(snapshot.processes["6002"].parent, "6001");
        assert_eq!(snapshot.processes["6002"].relation, "clone");
        // Parent still holds fd 3 at end of trace; the child closed its
        // inherited copy, so only the parent leaks.
        assert_eq!(snapshot.fd_leaks_by_process["6001"], vec![3]);
        assert!(!snapshot.fd_leaks_by_process.contains_key("6002"));
    }

    #[test]
    fn test_tid_reuse_starts_new_generation() {
        let lines = vec![
            "7001 openat(AT_FDCWD, \"/x\", O_RDONLY) = 3 <0.000010>",
            "7001 +++ exited with 0 +++",
            // kernel reuses tid 7001 for a new process
            "7001 read(0, \"\", 1) = 0 <0.000010>",
            "7001 +++ exited with 0 +++",
        ];
        let snapshot = TraceAnalyzer::new().analyze_lines(lines);
        assert_eq!(snapshot.processes["7001"].generation, 1);
        assert!(snapshot.processes["7001"].exited);
        // fd 3 leaked by generation 0; generation 1 leaked nothing new.
        assert_eq!(snapshot.fd_leaks_by_process["7001"], vec![3]);
    }
}
