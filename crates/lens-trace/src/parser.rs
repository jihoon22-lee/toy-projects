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

                if events.len() < self.max_retained_events {
                    events.push(ev);
                }
            }
        }

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
        }
    }
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
