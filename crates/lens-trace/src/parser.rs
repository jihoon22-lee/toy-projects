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

            // Handle process exit markers
            if body.starts_with("+++ exited with ") {
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

                // Track process stats
                let proc_key = ev.tid.to_string();
                let proc = processes.entry(proc_key).or_insert_with(|| ProcessInfo {
                    tid: ev.tid,
                    generation: 0,
                    calls: 0,
                    parent: String::new(),
                    relation: "root".to_string(),
                    exited: false,
                });
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
                        "close" => {
                            if let Ok(fd) = ev.arguments.trim().parse::<u64>() {
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
            leaked_fds.entry(tid).or_default().extend(fds);
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
            version: "0.3.0".to_string(),
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

/// Extract an fd array from strace arguments like `[3,4]` in `pipe2([3,4], 0)`.
fn parse_fd_array(arguments: &str) -> Option<Vec<u64>> {
    let open = arguments.find('[')?;
    let close = arguments[open..].find(']')? + open;
    let inner = &arguments[open + 1..close];
    let fds: Option<Vec<u64>> = inner
        .split(',')
        .map(|s| s.trim().parse::<u64>().ok())
        .collect();
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
        if first_word.contains(':')
            || (first_word.contains('.')
                && first_word.chars().all(|c| c.is_ascii_digit() || c == '.'))
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

        result = res_part.to_string();

        if res_part.starts_with("-1") {
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
}
