use crate::cli::*;
use crate::doctor::{self, HealthStatus};
use crate::output::outln;
use lens_abi::{diff_reports, inspect_elf};
use lens_build::{diff_compilations, parse_command_entry, CompileCommandEntry, ImpactGraph};
use lens_core::{
    create_bundle_archive, inspect_bundle, to_deterministic_pretty, verify_bundle_archive,
    BundleArtifact, Result,
};
use lens_disk::{DiskScanner, DuplicateFinder, ScanOptions, SnapshotV2, TrashManager};
use lens_env::{detect_shadowing, diff_environments, inspect_venv, EnvSnapshot};
use lens_log::{parse_line, LogFilter, LogIndexer, LogLevel};
use lens_net::{diff_net_reports, inspect_network, NetReport};
use lens_sys::{diff_systemd, OrderingGraph, SystemdSnapshot};
use lens_test::{diff_test_runs, parse_junit_xml};
use lens_trace::{diff_snapshots, TraceAnalyzer};
use std::fs;
use std::path::{Path, PathBuf};

/// Resolve `--format` + legacy `--json` into a boolean.
/// `default_json` marks commands whose historical default is JSON.
fn wants_json(format: Option<OutputFormat>, json_flag: bool, default_json: bool) -> bool {
    match format {
        Some(OutputFormat::Json) => true,
        Some(OutputFormat::Text) => false,
        None => json_flag || default_json,
    }
}

/// `sys diff` accepts either a saved snapshot JSON or a live unit
/// directory, which is loaded and graphed into an equivalent snapshot.
fn sys_snapshot_from(path: &Path) -> Result<SystemdSnapshot> {
    if path.is_dir() {
        let units = lens_sys::load_units(path)?;
        let graph = OrderingGraph::build(&units);
        return Ok(SystemdSnapshot {
            schema: lens_sys::SNAPSHOT_SCHEMA_V1.to_string(),
            version: env!("CARGO_PKG_VERSION").to_string(),
            semantics: "systemd-255-subset-v1".to_string(),
            units,
            cycles: graph.find_cycles(),
            diagnostics: vec![],
        });
    }
    let s = fs::read_to_string(path).map_err(|e| lens_core::LensError::Io {
        path: path.to_path_buf(),
        source: e,
    })?;
    Ok(serde_json::from_str(&s)?)
}

/// Human-readable binary size (KiB/MiB/GiB) for text-mode summaries.
fn human_bytes(n: u64) -> String {
    const UNITS: [&str; 5] = ["B", "KiB", "MiB", "GiB", "TiB"];
    let mut v = n as f64;
    let mut i = 0;
    while v >= 1024.0 && i < UNITS.len() - 1 {
        v /= 1024.0;
        i += 1;
    }
    if i == 0 {
        format!("{} B", n)
    } else {
        format!("{:.1} {}", v, UNITS[i])
    }
}

/// `test diff`/`test parse` accept a file, a directory (all `*.xml`
/// inside, sorted), or a `*`/`?` filename glob. CI pipelines commonly
/// produce one JUnit file per module.
fn expand_test_inputs(path: &Path) -> Result<Vec<PathBuf>> {
    if path.is_dir() {
        let mut files: Vec<PathBuf> = fs::read_dir(path)
            .map_err(|e| lens_core::LensError::Io {
                path: path.to_path_buf(),
                source: e,
            })?
            .flatten()
            .map(|e| e.path())
            .filter(|p| p.extension().is_some_and(|x| x == "xml"))
            .collect();
        files.sort();
        return Ok(files);
    }
    let s = path.to_string_lossy();
    if s.contains('*') || s.contains('?') {
        let parent = path.parent().unwrap_or_else(|| Path::new("."));
        let pat = path.file_name().map(|f| f.to_string_lossy().into_owned());
        if let Some(pat) = pat {
            let mut files: Vec<PathBuf> = fs::read_dir(parent)
                .map_err(|e| lens_core::LensError::Io {
                    path: parent.to_path_buf(),
                    source: e,
                })?
                .flatten()
                .map(|e| e.path())
                .filter(|p| {
                    p.file_name()
                        .map(|f| glob_match(&pat, &f.to_string_lossy()))
                        .unwrap_or(false)
                })
                .collect();
            files.sort();
            return Ok(files);
        }
    }
    Ok(vec![path.to_path_buf()])
}

/// Parse one or more JUnit files into a single merged run.
fn load_test_run(path: &Path, project: &str) -> Result<lens_test::TestRun> {
    let files = expand_test_inputs(path)?;
    if files.is_empty() {
        return Err(lens_core::LensError::InvalidInput {
            message: format!("{:?} matched no JUnit XML files", path),
        });
    }
    let mut runs = Vec::new();
    for f in &files {
        let bytes = fs::read(f).map_err(|e| lens_core::LensError::Io {
            path: f.clone(),
            source: e,
        })?;
        runs.push(parse_junit_xml(&bytes, project)?);
    }
    Ok(lens_test::merge_test_runs(runs, project))
}

/// Minimal `*`/`?` filename matcher (no recursion into subdirectories).
fn glob_match(pattern: &str, name: &str) -> bool {
    let p: Vec<char> = pattern.chars().collect();
    let n: Vec<char> = name.chars().collect();
    let (mut pi, mut ni) = (0usize, 0usize);
    let (mut star, mut star_next) = (usize::MAX, 0usize);
    while ni < n.len() {
        if pi < p.len() && (p[pi] == '?' || p[pi] == n[ni]) {
            pi += 1;
            ni += 1;
        } else if pi < p.len() && p[pi] == '*' {
            star = pi;
            star_next = ni;
            pi += 1;
        } else if star != usize::MAX {
            pi = star + 1;
            star_next += 1;
            ni = star_next;
        } else {
            return false;
        }
    }
    while pi < p.len() && p[pi] == '*' {
        pi += 1;
    }
    pi == p.len()
}

/// Command result per the exit-code convention: `Clean` exits 0,
/// `Findings` exits 1, and a returned `Err` exits 2.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Outcome {
    Clean,
    Findings,
}

pub fn dispatch(command: Commands) -> Result<Outcome> {
    let mut outcome = Outcome::Clean;
    match command {
        Commands::Disk { action } => match action {
            DiskCommands::Scan {
                path,
                json,
                format,
                parallel,
                max_depth,
                exclude,
                one_file_system,
                top,
            } => {
                let json = wants_json(format, json, false);
                let mut scan_opts = ScanOptions {
                    parallel,
                    one_file_system,
                    exclude_patterns: exclude,
                    ..Default::default()
                };
                if let Some(d) = max_depth {
                    scan_opts.max_depth = d;
                }
                let scanner = DiskScanner::new(scan_opts);
                let result = scanner.scan(&path)?;
                // Scan errors are surfaced even in --json mode so evidence
                // gaps never go unnoticed.
                for e in &result.errors {
                    eprintln!("scan warning: {e}");
                }
                if json {
                    let snapshot = SnapshotV2::from_tree(
                        &result.tree,
                        result.root_id,
                        result.complete,
                        result.truncated,
                    );
                    outln!("{}", to_deterministic_pretty(&snapshot)?);
                } else {
                    let root_node = &result.tree.nodes[result.root_id as usize];
                    // Unreadable entries or truncation mean the totals
                    // undercount reality — say so instead of claiming success.
                    if result.complete && !result.truncated {
                        outln!("Scan completed successfully for: {:?}", path);
                    } else {
                        outln!(
                            "Scan INCOMPLETE for: {:?} ({} scan error(s), truncated={})",
                            path,
                            result.errors.len(),
                            result.truncated
                        );
                    }
                    outln!("Total entries: {}", result.scanned_entries);
                    outln!(
                        "Total logical size: {} bytes ({})",
                        root_node.size,
                        human_bytes(root_node.size)
                    );
                    outln!(
                        "Total allocated size: {} bytes ({})",
                        root_node.allocated_size,
                        human_bytes(root_node.allocated_size)
                    );
                    if top > 0 {
                        let mut ranked: Vec<_> = result
                            .tree
                            .children_ids(result.root_id)
                            .into_iter()
                            .filter_map(|id| result.tree.get(id))
                            .collect();
                        ranked.sort_by(|a, b| b.size.cmp(&a.size).then(a.name.cmp(&b.name)));
                        if !ranked.is_empty() {
                            outln!("Largest top-level entries:");
                            for node in ranked.iter().take(top) {
                                outln!("    {:>10}  {}", human_bytes(node.size), node.name);
                            }
                        }
                    }
                }
                // Exit-code convention: incomplete/truncated evidence is a
                // finding (exit 1), in both text and JSON modes.
                if !result.complete || result.truncated {
                    outcome = Outcome::Findings;
                }
            }
            DiskCommands::Duplicates {
                path,
                min_size,
                format,
            } => {
                let scanner = DiskScanner::new(ScanOptions::default());
                let result = scanner.scan(&path)?;
                // Scan gaps are evidence gaps — surface them even though
                // the duplicate search itself may still succeed.
                for e in &result.errors {
                    eprintln!("scan warning: {e}");
                }
                let finder = DuplicateFinder::new(min_size);
                let report = finder.find_in_tree(&result.tree, &path)?;
                if wants_json(format, false, false) {
                    outln!("{}", to_deterministic_pretty(&report)?);
                } else {
                    for e in &report.errors {
                        eprintln!("hash warning: {e}");
                    }
                    outln!("Found {} duplicate group(s):", report.groups.len());
                    for (i, group) in report.groups.iter().enumerate() {
                        outln!("\n[{}] Hash: {}", i + 1, group.sha256);
                        outln!(
                            "    File size: {}, Reclaimable: {}",
                            human_bytes(group.size),
                            human_bytes(group.reclaimable_bytes)
                        );
                        for file in &group.files {
                            outln!("    - {}", file.display());
                        }
                    }
                    outln!(
                        "Total reclaimable: {} ({} bytes)",
                        human_bytes(report.total_reclaimable_bytes),
                        report.total_reclaimable_bytes
                    );
                    if !report.errors.is_empty() {
                        outln!("Hash errors: {}", report.errors.len());
                    }
                }
            }
            DiskCommands::Trash { path } => {
                let trash = TrashManager::default();
                let receipt = trash.move_to_trash(&path)?;
                outln!("Successfully trashed: {:?}", receipt.original_path);
                outln!("Trash location: {:?}", receipt.trashed_file_path);
                outln!("Info receipt: {:?}", receipt.info_path);
            }
        },
        Commands::Abi { action } => match action {
            AbiCommands::Inspect { binary, format } => {
                let bytes = fs::read(&binary).map_err(|e| lens_core::LensError::Io {
                    path: binary.clone(),
                    source: e,
                })?;
                let report = inspect_elf(&binary, &bytes);
                if wants_json(format, false, true) {
                    outln!("{}", to_deterministic_pretty(&report)?);
                } else {
                    // Human summary: the JSON report can be thousands of
                    // lines; text mode is the at-a-glance answer.
                    let weak = report
                        .evidence
                        .iter()
                        .filter(|s| s.binding == "weak")
                        .count();
                    outln!("ABI summary: {}", binary.display());
                    outln!(
                        "  SONAME:   {}",
                        report.dependencies.soname.as_deref().unwrap_or("-")
                    );
                    outln!("  NEEDED:   {}", report.dependencies.needed.join(", "));
                    outln!(
                        "  Exports:  {} defined ({} weak) | Imports: {}",
                        report.abi.symbols.len(),
                        weak,
                        report.abi.imports.len()
                    );
                    outln!("  Version requirements: {}", report.abi.versions.len());
                    if !report.diagnostics.is_empty() {
                        outln!("  Diagnostics: {}", report.diagnostics.len());
                    }
                }
            }
            AbiCommands::Diff {
                baseline,
                candidate,
                format,
            } => {
                let bytes_base = fs::read(&baseline).map_err(|e| lens_core::LensError::Io {
                    path: baseline.clone(),
                    source: e,
                })?;
                let bytes_cand = fs::read(&candidate).map_err(|e| lens_core::LensError::Io {
                    path: candidate.clone(),
                    source: e,
                })?;
                let report_base = inspect_elf(&baseline, &bytes_base);
                let report_cand = inspect_elf(&candidate, &bytes_cand);
                let diff = diff_reports(&report_base, &report_cand);
                if wants_json(format, false, true) {
                    outln!("{}", to_deterministic_pretty(&diff)?);
                } else {
                    outln!(
                        "ABI diff: {}",
                        format!("{:?}", diff.compatibility).to_lowercase()
                    );
                    outln!(
                        "  Exports: +{} -{} | Imports: +{} -{} | Deps: +{} -{}",
                        diff.symbols.added.len(),
                        diff.symbols.removed.len(),
                        diff.imports.added.len(),
                        diff.imports.removed.len(),
                        diff.dependencies.needed.added.len(),
                        diff.dependencies.needed.removed.len()
                    );
                    for s in diff.symbols.removed.iter().take(10) {
                        outln!("  - export removed: {}", s);
                    }
                    for d in &diff.diagnostics {
                        outln!("  ! {}", d);
                    }
                }
                // Exit-code convention: incompatible or uncertain ABI
                // counts as findings (uncertain needs human review).
                if !diff.compatible {
                    outcome = Outcome::Findings;
                }
            }
        },
        Commands::Log { action } => match action {
            LogCommands::Inspect { path, format } => {
                let indexer = LogIndexer::open(&path)?;
                if wants_json(format, false, false) {
                    outln!(
                        "{}",
                        to_deterministic_pretty(&serde_json::json!({
                            "path": path.to_string_lossy(),
                            "total_lines": indexer.len(),
                        }))?
                    );
                } else {
                    outln!("Log file: {:?}", path);
                    outln!("Total lines indexed: {}", indexer.len());
                }
            }
            LogCommands::Filter {
                path,
                query,
                min_level,
                include_unknown,
                format,
            } => {
                let indexer = LogIndexer::open(&path)?;
                let mut filter = LogFilter::new();
                if let Some(q) = query {
                    filter = filter.with_query(&q);
                }
                if let Some(lvl_str) = min_level {
                    let lvl = LogLevel::parse(&lvl_str);
                    if lvl == LogLevel::Unknown && !lvl_str.eq_ignore_ascii_case("unknown") {
                        return Err(lens_core::LensError::InvalidInput {
                            message: format!(
                                "invalid --min-level {lvl_str:?}; expected trace|debug|info|warn|error|fatal"
                            ),
                        });
                    }
                    filter = filter.with_min_level(lvl);
                }
                filter = filter.with_include_unknown(include_unknown);

                let json_mode = wants_json(format, false, false);
                let mut matched = 0;
                let mut excluded_unknown = 0usize;
                let mut matched_lines: Vec<serde_json::Value> = Vec::new();
                for idx in 0..indexer.len() {
                    if let Some(line) = indexer.get_line(idx) {
                        if filter.min_level.is_some() {
                            // Level filtering needs the parsed record; it
                            // also lets us count evidence discarded for
                            // having no detectable level.
                            let record = parse_line(line, idx + 1);
                            if filter.rejected_only_by_unknown_level(&record) {
                                excluded_unknown += 1;
                                continue;
                            }
                            if filter.matches(&record) {
                                if json_mode {
                                    matched_lines.push(serde_json::json!({
                                        "line": idx + 1,
                                        "text": line,
                                    }));
                                } else {
                                    outln!("[{}] {}", idx + 1, line);
                                }
                                matched += 1;
                            }
                        } else if filter.matches_line(line, idx + 1) {
                            if json_mode {
                                matched_lines.push(serde_json::json!({
                                    "line": idx + 1,
                                    "text": line,
                                }));
                            } else {
                                outln!("[{}] {}", idx + 1, line);
                            }
                            matched += 1;
                        }
                    }
                }
                if json_mode {
                    outln!(
                        "{}",
                        to_deterministic_pretty(&serde_json::json!({
                            "path": path.to_string_lossy(),
                            "matched": matched,
                            "total": indexer.len(),
                            "excluded_unknown": excluded_unknown,
                            "lines": matched_lines,
                        }))?
                    );
                }
                eprintln!("\nMatched {} of {} line(s).", matched, indexer.len());
                if excluded_unknown > 0 {
                    eprintln!(
                        "Excluded {} line(s) whose log level could not be determined (--include-unknown to keep them).",
                        excluded_unknown
                    );
                }
            }
        },
        Commands::Test { action } => match action {
            TestCommands::Parse {
                file,
                project,
                format,
            } => {
                let run = load_test_run(&file, &project)?;
                if wants_json(format, false, true) {
                    outln!("{}", to_deterministic_pretty(&run)?);
                } else {
                    let s = &run.summary;
                    outln!("Test run: {} (project {})", run.run_id, run.project);
                    outln!(
                        "  {} total | {} passed | {} failed | {} errors | {} skipped | {:.2}s",
                        s.total,
                        s.passed,
                        s.failed,
                        s.errors,
                        s.skipped,
                        s.duration_sec
                    );
                    if !run.complete {
                        outln!("  INCOMPLETE: the report was truncated or malformed");
                    }
                }
            }
            TestCommands::Diff {
                baseline,
                candidate,
                format,
            } => {
                let run_base = load_test_run(&baseline, "baseline")?;
                let run_cand = load_test_run(&candidate, "candidate")?;
                let diff = diff_test_runs(&run_base, &run_cand);
                if wants_json(format, false, true) {
                    outln!("{}", to_deterministic_pretty(&diff)?);
                } else {
                    outln!(
                        "Test diff: {} regressions, {} fixes, {} new failures, {} removed, {} skip changes",
                        diff.regressions.len(),
                        diff.fixes.len(),
                        diff.new_failures.len(),
                        diff.removed_tests.len(),
                        diff.skipped_changes.len()
                    );
                    for r in diff.regressions.iter().chain(&diff.new_failures) {
                        outln!(
                            "  - {}{}",
                            r.id,
                            r.message
                                .as_deref()
                                .map(|m| format!(": {}", m))
                                .unwrap_or_default()
                        );
                    }
                    for d in &diff.diagnostics {
                        outln!("  ! {}", d);
                    }
                }
                // Exit-code convention: regressions and brand-new failures
                // are findings.
                if !diff.regressions.is_empty() || !diff.new_failures.is_empty() {
                    outcome = Outcome::Findings;
                }
            }
        },
        Commands::Trace { action } => match action {
            TraceCommands::Analyze { trace_file, format } => {
                let content =
                    fs::read_to_string(&trace_file).map_err(|e| lens_core::LensError::Io {
                        path: trace_file.clone(),
                        source: e,
                    })?;
                let analyzer = TraceAnalyzer::new();
                let snapshot = analyzer.analyze_lines(content.lines());
                // Fail closed on input that is not strace output: when most
                // candidate lines fail to parse, the report is meaningless.
                let unparsed = snapshot.total_events.saturating_sub(snapshot.total_calls);
                if snapshot.total_events == 0 {
                    return Err(lens_core::LensError::InvalidInput {
                        message: format!("{:?} contains no recognizable strace lines", trace_file),
                    });
                }
                if unparsed > snapshot.total_calls {
                    return Err(lens_core::LensError::InvalidInput {
                        message: format!(
                            "{:?} does not look like strace output: {} of {} lines failed to parse",
                            trace_file, unparsed, snapshot.total_events
                        ),
                    });
                }
                if wants_json(format, false, true) {
                    outln!("{}", to_deterministic_pretty(&snapshot)?);
                } else {
                    outln!("Trace summary: {}", trace_file.display());
                    outln!(
                        "  {} events | {} calls | {} errors | {} processes",
                        snapshot.total_events,
                        snapshot.total_calls,
                        snapshot.total_errors,
                        snapshot.processes.len()
                    );
                    let mut top: Vec<_> = snapshot.syscalls.iter().collect();
                    top.sort_by_key(|e| std::cmp::Reverse(e.1.count));
                    for (name, st) in top.iter().take(5) {
                        outln!("    {}: {} calls, {} errors", name, st.count, st.errors);
                    }
                    if !snapshot.fd_leaks.is_empty() {
                        outln!("  fd leaks: {:?}", snapshot.fd_leaks);
                    }
                }
                // Exit-code convention: leaked fds are findings.
                if !snapshot.fd_leaks.is_empty() {
                    outcome = Outcome::Findings;
                }
            }
            TraceCommands::Diff {
                baseline,
                candidate,
                format,
            } => {
                let c1 = fs::read_to_string(&baseline).map_err(|e| lens_core::LensError::Io {
                    path: baseline.clone(),
                    source: e,
                })?;
                let c2 = fs::read_to_string(&candidate).map_err(|e| lens_core::LensError::Io {
                    path: candidate.clone(),
                    source: e,
                })?;
                let analyzer = TraceAnalyzer::new();
                let s1 = analyzer.analyze_lines(c1.lines());
                let s2 = analyzer.analyze_lines(c2.lines());
                let diff = diff_snapshots(&s1, &s2);
                if wants_json(format, false, true) {
                    outln!("{}", to_deterministic_pretty(&diff)?);
                } else {
                    outln!(
                        "Trace diff: calls {:+} ({} -> {}), errors {:+} ({} -> {})",
                        diff.call_delta,
                        diff.baseline_calls,
                        diff.candidate_calls,
                        diff.error_delta,
                        diff.baseline_errors,
                        diff.candidate_errors
                    );
                    for e in &diff.new_errors {
                        outln!("  + new error: {}", e);
                    }
                    for e in &diff.resolved_errors {
                        outln!("  - resolved: {}", e);
                    }
                    for s in &diff.significant_latency_shifts {
                        outln!("  ~ {}", s);
                    }
                }
            }
        },
        Commands::Sys { action } => match action {
            SysCommands::Inspect { path, format } => {
                let units = lens_sys::load_units(&path)?;
                let graph = OrderingGraph::build(&units);
                let cycles = graph.find_cycles();

                let snapshot = SystemdSnapshot {
                    schema: lens_sys::SNAPSHOT_SCHEMA_V1.to_string(),
                    version: env!("CARGO_PKG_VERSION").to_string(),
                    semantics: "systemd-255-subset-v1".to_string(),
                    units,
                    cycles,
                    diagnostics: vec![],
                };
                if wants_json(format, false, true) {
                    outln!("{}", to_deterministic_pretty(&snapshot)?);
                } else {
                    outln!(
                        "Systemd units: {} | cycles: {}",
                        snapshot.units.len(),
                        snapshot.cycles.len()
                    );
                    for (name, u) in &snapshot.units {
                        outln!("  {} {}", name, u.exec_start.as_deref().unwrap_or("-"));
                    }
                }
            }
            SysCommands::Cycles { dir, format } => {
                let units = lens_sys::load_units(&dir)?;
                let graph = OrderingGraph::build(&units);
                let cycles = graph.find_cycle_paths();
                let found = !cycles.is_empty();
                if wants_json(format, false, false) {
                    outln!("{}", to_deterministic_pretty(&cycles)?);
                } else if cycles.is_empty() {
                    outln!("No dependency cycles detected among {} units.", units.len());
                } else {
                    outln!("Detected {} cycle(s):", cycles.len());
                    for (i, c) in cycles.iter().enumerate() {
                        if c.edges.is_empty() {
                            outln!("  [{}] {}", i + 1, c.members.join(" -> "));
                        } else {
                            // Real directed path: each hop carries the
                            // directive and file:line that produced it.
                            let mut line = String::new();
                            for e in &c.edges {
                                let origin = match (&e.path, e.line) {
                                    (Some(p), Some(l)) => format!(
                                        "{}:{}",
                                        Path::new(p)
                                            .file_name()
                                            .map(|f| f.to_string_lossy().into_owned())
                                            .unwrap_or_else(|| p.clone()),
                                        l
                                    ),
                                    (Some(p), None) => p.clone(),
                                    _ => e.declared_by.clone(),
                                };
                                line.push_str(&format!(
                                    "{} --{}({})--> ",
                                    e.before, e.directive, origin
                                ));
                            }
                            line.push_str(&c.edges.last().unwrap().after);
                            outln!("  [{}] {}", i + 1, line);
                        }
                    }
                }
                // Exit-code convention: a detected cycle is a finding.
                if found {
                    outcome = Outcome::Findings;
                }
            }
            SysCommands::Diff {
                baseline,
                candidate,
                format,
            } => {
                // Inputs may be saved snapshots or live unit directories.
                let s1 = sys_snapshot_from(&baseline)?;
                let s2 = sys_snapshot_from(&candidate)?;
                let diff = diff_systemd(&s1, &s2);
                if wants_json(format, false, true) {
                    outln!("{}", to_deterministic_pretty(&diff)?);
                } else {
                    outln!(
                        "Systemd diff: +{} -{} ~{} unit(s), {} new cycle(s), {} resolved",
                        diff.added_units.len(),
                        diff.removed_units.len(),
                        diff.modified_units.len(),
                        diff.new_cycles.len(),
                        diff.resolved_cycles.len()
                    );
                    for u in &diff.modified_units {
                        outln!("  ~ {}", u.unit);
                    }
                }
            }
        },
        Commands::Build { action } => match action {
            BuildCommands::Inspect { file, format } => {
                let content = fs::read_to_string(&file).map_err(|e| lens_core::LensError::Io {
                    path: file.clone(),
                    source: e,
                })?;
                let entries: Vec<CompileCommandEntry> = serde_json::from_str(&content)?;
                let units: Vec<_> = entries.iter().map(parse_command_entry).collect();
                // Emit the full buildscope.snapshot/v4 schema, including the
                // reverse-impact graph resolved from on-disk headers.
                let mut graph = ImpactGraph::new();
                for unit in &units {
                    graph.add_translation_unit(unit);
                }
                let reverse_impact = graph
                    .header_to_units
                    .iter()
                    .map(|(h, us)| (h.clone(), us.iter().cloned().collect()))
                    .collect();
                // Transitive impact for every known header: keys of both
                // maps plus headers reached only through other headers.
                let all_headers: std::collections::BTreeSet<String> = graph
                    .header_to_units
                    .keys()
                    .chain(graph.header_to_headers.keys())
                    .cloned()
                    .chain(graph.header_to_headers.values().flatten().cloned())
                    .collect();
                let transitive_impact = all_headers
                    .iter()
                    .map(|h| (h.clone(), graph.compute_impact(h).impacted_units))
                    .collect();
                let snapshot = lens_build::BuildSnapshot {
                    schema: lens_build::SNAPSHOT_SCHEMA_V4.to_string(),
                    version: env!("CARGO_PKG_VERSION").to_string(),
                    total_units: units.len(),
                    units,
                    reverse_impact,
                    transitive_impact,
                };
                if wants_json(format, false, true) {
                    outln!("{}", to_deterministic_pretty(&snapshot)?);
                } else {
                    outln!(
                        "Build database: {} translation units, {} headers tracked",
                        snapshot.total_units,
                        snapshot.reverse_impact.len()
                    );
                    for u in &snapshot.units {
                        outln!("  {} [{}]", u.file, u.compiler);
                    }
                }
            }
            BuildCommands::Impact {
                file,
                header,
                format,
            } => {
                let content = fs::read_to_string(&file).map_err(|e| lens_core::LensError::Io {
                    path: file.clone(),
                    source: e,
                })?;
                let entries: Vec<CompileCommandEntry> = serde_json::from_str(&content)?;
                let mut graph = ImpactGraph::new();
                for entry in &entries {
                    let unit = parse_command_entry(entry);
                    graph.add_translation_unit(&unit);
                }
                // Normalize --header the same way unit paths are normalized so
                // relative spellings like include/common.h match graph keys.
                let cwd = std::env::current_dir()
                    .map(|p| p.to_string_lossy().into_owned())
                    .unwrap_or_else(|_| "/".to_string());
                let target = lens_build::normalize_path(&header, &cwd);
                let report = graph.compute_impact(&target);
                if wants_json(format, false, true) {
                    outln!("{}", to_deterministic_pretty(&report)?);
                } else {
                    outln!(
                        "Impact of {}: {} translation unit(s) need rebuild",
                        report.target_header,
                        report.total_impacted
                    );
                    for u in &report.impacted_units {
                        outln!("  - {}", u);
                    }
                }
            }
            BuildCommands::Diff {
                baseline,
                candidate,
                format,
            } => {
                let b_str =
                    fs::read_to_string(&baseline).map_err(|e| lens_core::LensError::Io {
                        path: baseline.clone(),
                        source: e,
                    })?;
                let c_str =
                    fs::read_to_string(&candidate).map_err(|e| lens_core::LensError::Io {
                        path: candidate.clone(),
                        source: e,
                    })?;
                let b_entries: Vec<CompileCommandEntry> = serde_json::from_str(&b_str)?;
                let c_entries: Vec<CompileCommandEntry> = serde_json::from_str(&c_str)?;
                let u1: Vec<_> = b_entries.iter().map(parse_command_entry).collect();
                let u2: Vec<_> = c_entries.iter().map(parse_command_entry).collect();
                let diff = diff_compilations(&u1, &u2);
                if wants_json(format, false, true) {
                    outln!("{}", to_deterministic_pretty(&diff)?);
                } else {
                    outln!(
                        "Build diff: +{} -{} ~{} unit(s)",
                        diff.added_units.len(),
                        diff.removed_units.len(),
                        diff.modified_units.len()
                    );
                    for u in &diff.modified_units {
                        outln!("  ~ {}", u.file);
                    }
                }
            }
        },
        Commands::Env { action } => match action {
            EnvCommands::Inspect {
                venv_path,
                project,
                format,
            } => {
                let mut venv =
                    inspect_venv(&venv_path, &[]).map_err(|e| lens_core::LensError::Io {
                        path: venv_path.clone(),
                        source: e,
                    })?;
                if let Some(ref proj) = project {
                    venv.shadowing_issues = detect_shadowing(proj, &venv.packages);
                }
                let snapshot = EnvSnapshot {
                    schema: lens_env::SNAPSHOT_SCHEMA_V1.to_string(),
                    version: env!("CARGO_PKG_VERSION").to_string(),
                    venv,
                };
                if wants_json(format, false, true) {
                    outln!("{}", to_deterministic_pretty(&snapshot)?);
                } else {
                    let v = &snapshot.venv;
                    outln!("Virtualenv: {}", venv_path.display());
                    outln!(
                        "  Python: {} | packages: {}",
                        if v.python_version.is_empty() {
                            "?"
                        } else {
                            v.python_version.as_str()
                        },
                        v.packages.len()
                    );
                    outln!("  Missing deps: {}", v.missing_dependencies.len());
                    if !v.shadowing_issues.is_empty() {
                        outln!("  Shadowing issues: {}", v.shadowing_issues.len());
                    }
                }
            }
            EnvCommands::Check {
                venv_path,
                extras,
                format,
            } => {
                let venv =
                    inspect_venv(&venv_path, &extras).map_err(|e| lens_core::LensError::Io {
                        path: venv_path.clone(),
                        source: e,
                    })?;
                if wants_json(format, false, false) {
                    outln!(
                        "{}",
                        to_deterministic_pretty(&serde_json::json!({
                            "venv": venv_path.to_string_lossy(),
                            "missing_dependencies": venv.missing_dependencies,
                            "version_conflicts": venv.version_conflicts,
                            "unevaluated_dependencies": venv.unevaluated_dependencies,
                        }))?
                    );
                } else {
                    if venv.missing_dependencies.is_empty() {
                        outln!("All dependencies satisfied in {:?}.", venv_path);
                    } else {
                        outln!(
                            "Found {} missing dependenc(ies):",
                            venv.missing_dependencies.len()
                        );
                        for d in &venv.missing_dependencies {
                            outln!("  - {}", d);
                        }
                    }
                    if !venv.version_conflicts.is_empty() {
                        outln!("Version conflicts:");
                        for d in &venv.version_conflicts {
                            outln!("  - {}", d);
                        }
                    }
                    if !venv.unevaluated_dependencies.is_empty() {
                        outln!("Unevaluated requirements (uncertain markers):");
                        for d in &venv.unevaluated_dependencies {
                            outln!("  - {}", d);
                        }
                    }
                }
                // Exit-code convention: missing or version-conflicting
                // dependencies are findings. Unevaluated markers alone are
                // informational and keep exit 0.
                if !venv.missing_dependencies.is_empty() || !venv.version_conflicts.is_empty() {
                    outcome = Outcome::Findings;
                }
            }
            EnvCommands::Diff {
                baseline,
                candidate,
                format,
            } => {
                let b_str =
                    fs::read_to_string(&baseline).map_err(|e| lens_core::LensError::Io {
                        path: baseline.clone(),
                        source: e,
                    })?;
                let c_str =
                    fs::read_to_string(&candidate).map_err(|e| lens_core::LensError::Io {
                        path: candidate.clone(),
                        source: e,
                    })?;
                let s1: EnvSnapshot = serde_json::from_str(&b_str)?;
                let s2: EnvSnapshot = serde_json::from_str(&c_str)?;
                let diff = diff_environments(&s1.venv, &s2.venv);
                if wants_json(format, false, true) {
                    outln!("{}", to_deterministic_pretty(&diff)?);
                } else {
                    outln!(
                        "Environment diff: +{} -{} ~{} package(s)",
                        diff.added_packages.len(),
                        diff.removed_packages.len(),
                        diff.version_changes.len()
                    );
                    for p in &diff.version_changes {
                        outln!("  ~ {}: {} -> {}", p.package, p.old_version, p.new_version);
                    }
                }
            }
        },
        Commands::Bundle { action } => match action {
            BundleCommands::Create {
                output,
                disk,
                log,
                trace,
                test,
                sys,
                env,
                net,
                force,
                format,
            } => {
                // A bundle with zero artifact sources would produce an
                // empty manifest that still verifies OK — refuse to create it.
                let any_source = disk.is_some()
                    || log.is_some()
                    || trace.is_some()
                    || test.is_some()
                    || sys.is_some()
                    || env.is_some()
                    || net;
                if !any_source {
                    return Err(lens_core::LensError::InvalidInput {
                        message: "at least one artifact source is required \
                                  (--disk/--log/--trace/--test/--sys/--env/--net)"
                            .to_string(),
                    });
                }

                // Evidence is append-only: refuse to destroy an existing
                // bundle unless the caller explicitly passes --force.
                if output.try_exists().map_err(|e| lens_core::LensError::Io {
                    path: output.clone(),
                    source: e,
                })? && !force
                {
                    return Err(lens_core::LensError::InvalidInput {
                        message: format!(
                            "output bundle {:?} already exists; pass --force to overwrite",
                            output
                        ),
                    });
                }

                // Fail-closed collection: every capture error is preserved in the
                // manifest's diagnostics rather than silently dropped.
                let mut artifacts: Vec<BundleArtifact> = Vec::new();
                let mut diagnostics: Vec<String> = Vec::new();

                // Each producer runs inside its own closure so `?` aborts only
                // that source's collection, never the whole dispatch.
                macro_rules! collect {
                    ($name:literal, $entry:literal, $produce:expr) => {{
                        #[allow(unused_mut)]
                        let mut produce = || $produce;
                        match produce().and_then(|v| BundleArtifact::json($entry, &v)) {
                            Ok(artifact) => artifacts.push(artifact),
                            Err(e) => diagnostics.push(format!("{}: {}", $name, e)),
                        }
                    }};
                }

                if let Some(ref d) = disk {
                    collect!("disk", "reports/disk_snapshot.json", {
                        let res = DiskScanner::new(ScanOptions::default()).scan(d)?;
                        // Per-entry scan failures are evidence gaps — record
                        // them in the manifest diagnostics.
                        diagnostics.extend(res.errors.iter().map(|e| format!("disk scan: {e}")));
                        Ok(SnapshotV2::from_tree(
                            &res.tree,
                            res.root_id,
                            res.complete,
                            res.truncated,
                        ))
                    });
                }

                if let Some(ref l) = log {
                    collect!("log", "reports/log_summary.json", {
                        let indexer = LogIndexer::open(l)?;
                        Ok(serde_json::json!({
                            "path": l.to_string_lossy(),
                            "total_lines": indexer.len(),
                        }))
                    });
                }

                if let Some(ref t) = trace {
                    collect!("trace", "reports/trace_snapshot.json", {
                        let content =
                            fs::read_to_string(t).map_err(|e| lens_core::LensError::Io {
                                path: t.clone(),
                                source: e,
                            })?;
                        Ok(TraceAnalyzer::new().analyze_lines(content.lines()))
                    });
                }

                if let Some(ref t) = test {
                    collect!("test", "reports/test_run.json", {
                        let bytes = fs::read(t).map_err(|e| lens_core::LensError::Io {
                            path: t.clone(),
                            source: e,
                        })?;
                        parse_junit_xml(&bytes, "bundle")
                    });
                }

                if let Some(ref s) = sys {
                    collect!("sys", "reports/sys_snapshot.json", {
                        let units = lens_sys::load_units(s)?;
                        let graph = OrderingGraph::build(&units);
                        let cycles = graph.find_cycles();
                        Ok(SystemdSnapshot {
                            schema: lens_sys::SNAPSHOT_SCHEMA_V1.to_string(),
                            version: env!("CARGO_PKG_VERSION").to_string(),
                            semantics: "systemd-255-subset-v1".to_string(),
                            units,
                            cycles,
                            diagnostics: vec![],
                        })
                    });
                }

                if let Some(ref e) = env {
                    collect!("env", "reports/env_snapshot.json", {
                        let venv = inspect_venv(e, &[]).map_err(|err| {
                            lens_core::LensError::InvalidInput {
                                message: format!("venv {}: {}", e.display(), err),
                            }
                        })?;
                        Ok(EnvSnapshot {
                            schema: lens_env::SNAPSHOT_SCHEMA_V1.to_string(),
                            version: env!("CARGO_PKG_VERSION").to_string(),
                            venv,
                        })
                    });
                }

                if net {
                    collect!("net", "reports/net_report.json", { inspect_network(None) });
                }

                if artifacts.is_empty() && !diagnostics.is_empty() {
                    eprintln!("All artifact collections failed:");
                    for d in &diagnostics {
                        eprintln!("  - {}", d);
                    }
                    return Err(lens_core::LensError::InvalidInput {
                        message: "bundle contains no artifacts".to_string(),
                    });
                }

                let manifest = create_bundle_archive(
                    &output,
                    env!("CARGO_PKG_VERSION"),
                    &artifacts,
                    diagnostics.clone(),
                )?;

                if wants_json(format, false, false) {
                    outln!("{}", to_deterministic_pretty(&manifest)?);
                } else {
                    outln!(
                        "Successfully created forensic flight recorder bundle: {:?}",
                        output
                    );
                    outln!("  Artifacts embedded: {}", manifest.sources.len());
                    if !diagnostics.is_empty() {
                        outln!("  Collection warnings recorded in manifest:");
                        for d in &diagnostics {
                            outln!("    - {}", d);
                        }
                    }
                }
            }
            BundleCommands::Inspect { bundle, format } => {
                let report = inspect_bundle(&bundle)?;
                if wants_json(format, false, false) {
                    outln!("{}", to_deterministic_pretty(&report)?);
                    return Ok(outcome);
                }
                outln!("=== Lens Forensic Flight Recorder Bundle ===");
                outln!("Entries: {}", report.entries.len());
                match &report.manifest {
                    Some(m) => {
                        outln!("Schema:        {}", m.schema);
                        outln!("Tool:          {} v{}", m.tool, m.version);
                        outln!("Created At:    {}", m.created_at);
                        outln!("Manifest Files:");
                        for s in &m.sources {
                            outln!("  - {} ({} bytes)", s.path, s.size);
                        }
                        if !m.diagnostics.is_empty() {
                            outln!("Capture Diagnostics:");
                            for d in &m.diagnostics {
                                outln!("  ! {}", d);
                            }
                        }
                    }
                    None => outln!("Manifest: NOT FOUND (untrusted bundle)"),
                }
                outln!("Archive Entries:");
                for e in &report.entries {
                    outln!("  - {} ({} bytes)", e.name, e.size);
                }
            }
            BundleCommands::Verify { bundle, format } => {
                let report = verify_bundle_archive(&bundle)?;
                if wants_json(format, false, false) {
                    outln!("{}", to_deterministic_pretty(&report)?);
                    if report.is_valid {
                        return Ok(outcome);
                    }
                    return Err(lens_core::LensError::InvalidInput {
                        message: "Bundle cryptographic verification failed".to_string(),
                    });
                }
                if report.is_valid {
                    outln!("Bundle verification SUCCESSFUL: {:?}", bundle);
                    outln!("  Total manifest files:    {}", report.total_files);
                    outln!("  Verified SHA-256 files:  {}", report.verified_files);
                    // The manifest has no signature: this reports integrity
                    // against the embedded manifest, not provenance.
                    outln!("  Integrity: all files match the embedded manifest");
                    for u in &report.unexpected_files {
                        outln!("  Unlisted entry (not in manifest): {}", u);
                    }
                } else {
                    eprintln!("Bundle verification FAILED: {:?}", bundle);
                    if let Some(err) = report.error {
                        eprintln!("  Error: {}", err);
                    }
                    for t in &report.tampered_files {
                        eprintln!("  Tampered: {}", t);
                    }
                    for m in &report.missing_files {
                        eprintln!("  Missing:  {}", m);
                    }
                    for u in &report.unexpected_files {
                        eprintln!("  Unlisted: {}", u);
                    }
                    return Err(lens_core::LensError::InvalidInput {
                        message: "Bundle cryptographic verification failed".to_string(),
                    });
                }
            }
        },
        Commands::Net { action } => match action {
            NetCommands::Inspect {
                proc_dir,
                json,
                format,
                no_unix,
            } => {
                let mut report = inspect_network(proc_dir.as_deref())?;
                if no_unix {
                    report.sockets.retain(|s| {
                        !matches!(
                            s.kind,
                            lens_net::SocketKind::UnixStream | lens_net::SocketKind::UnixDgram
                        )
                    });
                }
                if wants_json(format, json, false) {
                    outln!("{}", to_deterministic_pretty(&report)?);
                } else {
                    outln!("Network & Socket Inspection Summary:");
                    outln!("  Total Sockets:          {}", report.summary.total_sockets);
                    outln!(
                        "  Listening Ports:        {}",
                        report.summary.listening_ports
                    );
                    outln!(
                        "  Established Conns:      {}",
                        report.summary.established_connections
                    );
                    outln!(
                        "  TIME_WAIT Sockets:      {}",
                        report.summary.time_wait_sockets
                    );
                    outln!(
                        "  Orphan Sockets:         {}",
                        report.summary.orphan_sockets
                    );
                    outln!(
                        "  Owner-Unknown Sockets:  {}",
                        report.summary.owner_unknown_sockets
                    );
                    if report.summary.uninspectable_processes > 0 {
                        outln!(
                            "  Uninspectable Procs:    {} (re-run with sudo to attribute)",
                            report.summary.uninspectable_processes
                        );
                    }
                    outln!(
                        "  UNIX Domain Sockets:    {}",
                        report.summary.unix_domain_sockets
                    );
                    outln!("\nActive Listening Ports:");
                    outln!(
                        "{:<8} {:<24} {:<10} {:<8} {:<8} {:<16}",
                        "PROTO",
                        "LOCAL ADDRESS",
                        "INODE",
                        "UID",
                        "PID",
                        "PROCESS"
                    );
                    for l in &report.listening {
                        let proc_str = l
                            .process
                            .as_ref()
                            .map(|p| p.name.as_str())
                            .map(str::to_string)
                            .unwrap_or_else(|| match l.owner_state {
                                Some(lens_net::OwnerState::OwnerUnknown) => {
                                    "<owner unknown>".to_string()
                                }
                                _ => "<orphan>".to_string(),
                            });
                        let pid_str = l
                            .process
                            .as_ref()
                            .map(|p| p.pid.to_string())
                            .unwrap_or_else(|| "-".to_string());
                        let addr = format!("{}:{}", l.local_address, l.local_port);
                        outln!(
                            "{:<8} {:<24} {:<10} {:<8} {:<8} {}",
                            format!("{:?}", l.kind),
                            addr,
                            l.inode,
                            l.uid,
                            pid_str,
                            proc_str
                        );
                    }
                }
            }
            NetCommands::Diff {
                baseline,
                candidate,
                format,
            } => {
                let base_content =
                    fs::read_to_string(&baseline).map_err(|e| lens_core::LensError::Io {
                        path: baseline.clone(),
                        source: e,
                    })?;
                let cand_content =
                    fs::read_to_string(&candidate).map_err(|e| lens_core::LensError::Io {
                        path: candidate.clone(),
                        source: e,
                    })?;
                let base_report: NetReport = serde_json::from_str(&base_content)?;
                let cand_report: NetReport = serde_json::from_str(&cand_content)?;
                let diff = diff_net_reports(&base_report, &cand_report);
                if wants_json(format, false, true) {
                    outln!("{}", to_deterministic_pretty(&diff)?);
                } else {
                    outln!(
                        "Network diff: +{} -{} listener(s), +{} -{} connection(s), TIME_WAIT {:+}",
                        diff.new_listeners.len(),
                        diff.closed_listeners.len(),
                        diff.new_connections.len(),
                        diff.closed_connections.len(),
                        diff.time_wait_delta
                    );
                    for l in &diff.new_listeners {
                        outln!("  + {}:{}", l.local_address, l.local_port);
                    }
                    for l in &diff.closed_listeners {
                        outln!("  - {}:{}", l.local_address, l.local_port);
                    }
                }
            }
        },
        Commands::Doctor {
            root,
            procfs,
            systemd_dir,
            json,
            format,
            fail_on,
        } => {
            // An explicitly passed path that does not exist is a usage
            // error, not a healthy-looking "non-systemd environment" report.
            for (flag, dir) in [
                ("--root", &root),
                ("--procfs", &procfs),
                ("--systemd-dir", &systemd_dir),
            ] {
                if let Some(p) = dir {
                    if !p.exists() {
                        return Err(lens_core::LensError::InvalidInput {
                            message: format!("{flag} path {:?} does not exist", p),
                        });
                    }
                }
            }
            let report =
                doctor::run_doctor(root.as_deref(), procfs.as_deref(), systemd_dir.as_deref());
            if wants_json(format, json, false) {
                outln!("{}", to_deterministic_pretty(&report)?);
            } else {
                outln!("=== Lens System Doctor Diagnosis ===");
                outln!("Overall Status: {:?}", report.overall_status);
                outln!(
                    "Summary: {} Total | {} Passed | {} Warnings | {} Failures\n",
                    report.summary.total,
                    report.summary.passed,
                    report.summary.warnings,
                    report.summary.failures
                );
                outln!("{:<12} {:<30} {:<8} MESSAGE", "CATEGORY", "CHECK", "STATUS");
                for c in &report.checks {
                    let status_str = match c.status {
                        HealthStatus::Pass => "[PASS]",
                        HealthStatus::Warn => "[WARN]",
                        HealthStatus::Fail => "[FAIL]",
                    };
                    outln!(
                        "{:<12} {:<30} {:<8} {}",
                        c.category,
                        c.name,
                        status_str,
                        c.message
                    );
                    if let Some(ref rec) = c.recommendation {
                        outln!("             -> Recommendation: {}", rec);
                    }
                }
            }
            // Exit-code convention: findings exit 1. --fail-on picks the
            // severity threshold that counts as a finding.
            if report.summary.failures > 0
                || (fail_on == crate::cli::FailOn::Warn && report.summary.warnings > 0)
            {
                outcome = Outcome::Findings;
            }
        }
        Commands::Tui { path } => {
            lens_tui::run(&path).map_err(|e| lens_core::LensError::Io {
                path: path.clone(),
                source: e,
            })?;
        }
        Commands::Completion { shell } => {
            use clap::CommandFactory;
            let mut cmd = Cli::command();
            clap_complete::generate(shell, &mut cmd, "lens", &mut std::io::stdout());
        }
    }

    Ok(outcome)
}
