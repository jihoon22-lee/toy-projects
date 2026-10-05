use serde_json::Value;
use std::collections::BTreeMap;
use std::fs;
use std::path::Path;

use lens_abi::inspect_elf;
use lens_build::{parse_command_entry, CompileCommandEntry, ImpactGraph};
use lens_core::to_deterministic_pretty;
use lens_disk::{DiskScanner, DuplicateFinder, ScanOptions, SnapshotV2};
use lens_env::inspect_venv;
use lens_log::{parse_line, LogFilter, LogIndexer, LogLevel};
use lens_sys::{OrderingGraph, SystemdUnit};
use lens_trace::TraceAnalyzer;

pub fn execute_tool(name: &str, args: &Value) -> Result<String, String> {
    match name {
        "lens_disk_scan" => {
            let path_str = args
                .get("path")
                .and_then(|v| v.as_str())
                .ok_or("Missing 'path' argument")?;
            let path = Path::new(path_str);
            let scanner = DiskScanner::new(ScanOptions::default());
            let res = scanner.scan(path).map_err(|e| e.to_string())?;

            if args.get("json").and_then(|v| v.as_bool()).unwrap_or(false) {
                let snap =
                    SnapshotV2::from_tree(&res.tree, res.root_id, res.complete, res.truncated);
                to_deterministic_pretty(&snap).map_err(|e| e.to_string())
            } else {
                let children_count = res.tree.children_ids(res.root_id).len();
                let root = &res.tree.nodes[res.root_id as usize];
                serde_json::to_string_pretty(&serde_json::json!({
                    "path": path_str,
                    "scanned_entries": res.scanned_entries,
                    "logical_bytes": root.size,
                    "allocated_bytes": root.allocated_size,
                    "children_count": children_count,
                    "complete": res.complete,
                    "errors": res.errors,
                }))
                .map_err(|e| e.to_string())
            }
        }
        "lens_disk_duplicates" => {
            let path_str = args
                .get("path")
                .and_then(|v| v.as_str())
                .ok_or("Missing 'path' argument")?;
            let min_size = args
                .get("min_size")
                .and_then(|v| v.as_u64())
                .unwrap_or(1024);
            let path = Path::new(path_str);
            let scanner = DiskScanner::new(ScanOptions::default());
            let res = scanner.scan(path).map_err(|e| e.to_string())?;
            let finder = DuplicateFinder::new(min_size);
            let groups = finder
                .find_in_tree(&res.tree, path)
                .map_err(|e| e.to_string())?;
            to_deterministic_pretty(&groups).map_err(|e| e.to_string())
        }
        "lens_abi_inspect" => {
            let bin_str = args
                .get("binary")
                .and_then(|v| v.as_str())
                .ok_or("Missing 'binary' argument")?;
            let p = Path::new(bin_str);
            let bytes = fs::read(p).map_err(|e| e.to_string())?;
            let report = inspect_elf(p, &bytes);
            to_deterministic_pretty(&report).map_err(|e| e.to_string())
        }
        "lens_log_filter" => {
            let path_str = args
                .get("path")
                .and_then(|v| v.as_str())
                .ok_or("Missing 'path' argument")?;
            let indexer = LogIndexer::open(Path::new(path_str)).map_err(|e| e.to_string())?;
            let mut filter = LogFilter::new();
            if let Some(q) = args.get("query").and_then(|v| v.as_str()) {
                filter = filter.with_query(q);
            }
            if let Some(lvl) = args.get("min_level").and_then(|v| v.as_str()) {
                let parsed = LogLevel::parse(lvl);
                if parsed == LogLevel::Unknown && !lvl.eq_ignore_ascii_case("unknown") {
                    return Err(format!(
                        "Invalid 'min_level' {lvl:?}; expected trace|debug|info|warn|error|fatal"
                    ));
                }
                filter = filter.with_min_level(parsed);
            }

            // The scan is capped at 1000 lines — surface the bound so callers
            // don't read a partial result as complete.
            const MAX_SCANNED: usize = 1000;
            let mut matches = Vec::new();
            for idx in 0..indexer.len().min(MAX_SCANNED) {
                if let Some(line) = indexer.get_line(idx) {
                    let record = parse_line(line, idx + 1);
                    if filter.matches(&record) {
                        matches.push(serde_json::json!({
                            "line": record.line_number,
                            "level": record.level.as_str(),
                            "raw": record.raw,
                        }));
                    }
                }
            }
            serde_json::to_string_pretty(&serde_json::json!({
                "matches": matches,
                "scanned_lines": indexer.len().min(MAX_SCANNED),
                "total_lines": indexer.len(),
                "truncated": indexer.len() > MAX_SCANNED,
            }))
            .map_err(|e| e.to_string())
        }
        "lens_trace_analyze" => {
            let path_str = args
                .get("trace_file")
                .and_then(|v| v.as_str())
                .ok_or("Missing 'trace_file' argument")?;
            let content = fs::read_to_string(path_str).map_err(|e| e.to_string())?;
            let analyzer = TraceAnalyzer::new();
            let snap = analyzer.analyze_lines(content.lines());
            to_deterministic_pretty(&snap).map_err(|e| e.to_string())
        }
        "lens_sys_cycles" => {
            let dir_str = args
                .get("dir")
                .and_then(|v| v.as_str())
                .ok_or("Missing 'dir' argument")?;
            let units = load_sys_units(Path::new(dir_str)).map_err(|e| e.to_string())?;
            let graph = OrderingGraph::build(&units);
            let cycles = graph.find_cycles();
            serde_json::to_string_pretty(&serde_json::json!({
                "scanned_units": units.len(),
                "cycle_count": cycles.len(),
                "cycles": cycles,
            }))
            .map_err(|e| e.to_string())
        }
        "lens_build_impact" => {
            let file_str = args
                .get("compile_commands")
                .and_then(|v| v.as_str())
                .ok_or("Missing 'compile_commands' argument")?;
            let header_str = args
                .get("header")
                .and_then(|v| v.as_str())
                .ok_or("Missing 'header' argument")?;
            let content = fs::read_to_string(file_str).map_err(|e| e.to_string())?;
            let entries: Vec<CompileCommandEntry> =
                serde_json::from_str(&content).map_err(|e| e.to_string())?;
            let mut graph = ImpactGraph::new();
            for entry in &entries {
                let unit = parse_command_entry(entry);
                graph.add_translation_unit(&unit);
            }
            let cwd = std::env::current_dir()
                .map(|p| p.to_string_lossy().into_owned())
                .unwrap_or_else(|_| "/".to_string());
            let target = lens_build::normalize_path(header_str, &cwd);
            let report = graph.compute_impact(&target);
            to_deterministic_pretty(&report).map_err(|e| e.to_string())
        }
        "lens_env_check" => {
            let path_str = args
                .get("venv_path")
                .and_then(|v| v.as_str())
                .ok_or("Missing 'venv_path' argument")?;
            let venv = inspect_venv(Path::new(path_str)).map_err(|e| e.to_string())?;
            serde_json::to_string_pretty(&serde_json::json!({
                "python_version": venv.python_version,
                "packages_count": venv.packages.len(),
                "missing_dependencies": venv.missing_dependencies,
            }))
            .map_err(|e| e.to_string())
        }
        "lens_net_inspect" => {
            let proc_dir = args.get("proc_dir").and_then(|v| v.as_str()).map(Path::new);
            let report = lens_net::inspect_network(proc_dir).map_err(|e| e.to_string())?;
            to_deterministic_pretty(&report).map_err(|e| e.to_string())
        }
        "lens_bundle_verify" => {
            let bundle_str = args
                .get("bundle_path")
                .and_then(|v| v.as_str())
                .ok_or("Missing 'bundle_path' argument")?;
            let report = lens_core::verify_bundle_archive(Path::new(bundle_str))
                .map_err(|e| e.to_string())?;
            to_deterministic_pretty(&report).map_err(|e| e.to_string())
        }
        "lens_doctor" => {
            let root_path = args
                .get("root_path")
                .and_then(|v| v.as_str())
                .map(Path::new);
            let proc_dir = args.get("proc_dir").and_then(|v| v.as_str()).map(Path::new);
            let systemd_dir = args
                .get("systemd_dir")
                .and_then(|v| v.as_str())
                .map(Path::new);
            let report = lens_cli::doctor::run_doctor(root_path, proc_dir, systemd_dir);
            to_deterministic_pretty(&report).map_err(|e| e.to_string())
        }
        other => Err(format!("Unknown tool: {}", other)),
    }
}

fn load_sys_units(dir: &Path) -> std::io::Result<BTreeMap<String, SystemdUnit>> {
    lens_sys::load_units(dir).map_err(|e| std::io::Error::other(e.to_string()))
}
