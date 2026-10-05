use crate::model::*;
use std::collections::BTreeSet;

pub fn diff_snapshots(baseline: &TraceSnapshot, candidate: &TraceSnapshot) -> TraceDiff {
    let call_delta = candidate.total_calls as i64 - baseline.total_calls as i64;
    let error_delta = candidate.total_errors as i64 - baseline.total_errors as i64;

    // Detect new errors
    let mut new_errors = Vec::new();
    for (err, &count) in &candidate.errors {
        if count > 0 && baseline.errors.get(err).copied().unwrap_or(0) == 0 {
            new_errors.push(err.clone());
        }
    }
    new_errors.sort();

    // Detect resolved errors
    let mut resolved_errors = Vec::new();
    for (err, &count) in &baseline.errors {
        if count > 0 && candidate.errors.get(err).copied().unwrap_or(0) == 0 {
            resolved_errors.push(err.clone());
        }
    }
    resolved_errors.sort();

    // Syscall deltas and latency shifts
    let mut syscall_names = BTreeSet::new();
    for key in baseline.syscalls.keys() {
        syscall_names.insert(key.clone());
    }
    for key in candidate.syscalls.keys() {
        syscall_names.insert(key.clone());
    }

    let mut syscall_deltas = Vec::new();
    let mut significant_latency_shifts = Vec::new();

    for name in syscall_names {
        let b = baseline.syscalls.get(&name);
        let c = candidate.syscalls.get(&name);

        let b_count = b.map(|s| s.count).unwrap_or(0);
        let c_count = c.map(|s| s.count).unwrap_or(0);
        let b_errs = b.map(|s| s.errors).unwrap_or(0);
        let c_errs = c.map(|s| s.errors).unwrap_or(0);

        syscall_deltas.push(SyscallDelta {
            syscall: name.clone(),
            baseline_count: b_count,
            candidate_count: c_count,
            count_delta: c_count as i64 - b_count as i64,
            baseline_errors: b_errs,
            candidate_errors: c_errs,
            error_delta: c_errs as i64 - b_errs as i64,
        });

        // Check latency shift
        if let (Some(b_stat), Some(c_stat)) = (b, c) {
            if b_stat.known_duration > 0 && c_stat.known_duration > 0 {
                let b_avg = b_stat.total_ns / b_stat.known_duration;
                let c_avg = c_stat.total_ns / c_stat.known_duration;

                // If avg latency increased by > 50% and base avg > 1 microsecond (1000ns)
                if b_avg >= 1000 && c_avg > b_avg {
                    let pct_increase = ((c_avg - b_avg) * 100) / b_avg;
                    if pct_increase >= 50 {
                        significant_latency_shifts.push(format!(
                            "{}: avg latency shifted from {}ns to {}ns (+{}%)",
                            name, b_avg, c_avg, pct_increase
                        ));
                    }
                }
            }
        }
    }

    TraceDiff {
        schema: DIFF_SCHEMA_V1.to_string(),
        baseline_calls: baseline.total_calls,
        candidate_calls: candidate.total_calls,
        call_delta,
        baseline_errors: baseline.total_errors,
        candidate_errors: candidate.total_errors,
        error_delta,
        new_errors,
        resolved_errors,
        significant_latency_shifts,
        syscall_deltas,
    }
}
