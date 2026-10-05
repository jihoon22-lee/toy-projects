use clap::{Parser, Subcommand};
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};

use lens_abi::{diff_reports, inspect_elf};
use lens_build::{diff_compilations, parse_command_entry, CompileCommandEntry, ImpactGraph};
use lens_core::{to_deterministic_pretty, Result};
use lens_disk::{DiskScanner, DuplicateFinder, ScanOptions, SnapshotV2, TrashManager};
use lens_env::{detect_shadowing, diff_environments, inspect_venv, EnvSnapshot};
use lens_log::{parse_line, LogFilter, LogIndexer, LogLevel};
use lens_sys::{diff_systemd, parse_unit_content, OrderingGraph, SystemdSnapshot, SystemdUnit};
use lens_test::{diff_test_runs, parse_junit_xml};
use lens_trace::{diff_snapshots, TraceAnalyzer};

#[derive(Parser)]
#[command(name = "lens")]
#[command(version = "0.3.0")]
#[command(about = "Unified Systems Diagnostics and Forensic Platform", long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Filesystem footprint analysis, duplicate detection, and safe cleanup
    Disk {
        #[command(subcommand)]
        action: DiskCommands,
    },
    /// ELF binary inspection, dynamic symbol surface, and ABI diffing
    Abi {
        #[command(subcommand)]
        action: AbiCommands,
    },
    /// High-performance memory-mapped log viewer and filter
    Log {
        #[command(subcommand)]
        action: LogCommands,
    },
    /// High-speed test report parsing, flaky analysis, and regression diffing
    Test {
        #[command(subcommand)]
        action: TestCommands,
    },
    /// Linux syscall trace (strace) analysis and latency/error diffing
    Trace {
        #[command(subcommand)]
        action: TraceCommands,
    },
    /// Systemd service unit, drop-in override, and dependency DAG analysis
    Sys {
        #[command(subcommand)]
        action: SysCommands,
    },
    /// Compilation database analysis, compiler flags, and header impact DAG
    Build {
        #[command(subcommand)]
        action: BuildCommands,
    },
    /// Python virtual environment audit, dependency check, and shadowing detection
    Env {
        #[command(subcommand)]
        action: EnvCommands,
    },
    /// Forensic Flight Recorder: bundle multiple diagnostic artifacts into a .lens container
    Bundle {
        #[command(subcommand)]
        action: BundleCommands,
    },
}

#[derive(Subcommand)]
enum DiskCommands {
    /// Scan directory and print space usage summary or snapshot JSON
    Scan {
        path: PathBuf,
        #[arg(long)]
        json: bool,
    },
    /// Find duplicate files with hard link deduplication
    Duplicates {
        path: PathBuf,
        #[arg(long, default_value = "1024")]
        min_size: u64,
    },
    /// Safely move a file to trash following the FreeDesktop Trash spec
    Trash { path: PathBuf },
}

#[derive(Subcommand)]
enum AbiCommands {
    /// Inspect an ELF binary and output JSON report
    Inspect { binary: PathBuf },
    /// Compare two ELF binaries and report 3-state ABI compatibility
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
    },
}

#[derive(Subcommand)]
enum LogCommands {
    /// Inspect log file metrics and line counts
    Inspect { path: PathBuf },
    /// Search and filter log lines with zero-allocation speed
    Filter {
        path: PathBuf,
        #[arg(long)]
        query: Option<String>,
        #[arg(long)]
        min_level: Option<String>,
    },
}

#[derive(Subcommand)]
enum TestCommands {
    /// Parse JUnit XML test report
    Parse {
        file: PathBuf,
        #[arg(long, default_value = "default")]
        project: String,
    },
    /// Compare two JUnit XML test runs and detect regressions
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
    },
}

#[derive(Subcommand)]
enum TraceCommands {
    /// Analyze strace output log and output JSON snapshot
    Analyze { trace_file: PathBuf },
    /// Compare two strace log runs and detect latency shifts/new errors
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
    },
}

#[derive(Subcommand)]
enum SysCommands {
    /// Inspect systemd unit file or unit directory
    Inspect { path: PathBuf },
    /// Detect ordering cycles in a directory of systemd unit files
    Cycles { dir: PathBuf },
    /// Compare two systemd snapshots
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
    },
}

#[derive(Subcommand)]
enum BuildCommands {
    /// Inspect compile_commands.json database
    Inspect { file: PathBuf },
    /// Calculate reverse compilation impact for a header
    Impact {
        file: PathBuf,
        #[arg(long)]
        header: String,
    },
    /// Diff two compile_commands.json databases
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
    },
}

#[derive(Subcommand)]
enum EnvCommands {
    /// Inspect Python virtualenv and optionally check local project shadowing
    Inspect {
        venv_path: PathBuf,
        #[arg(long)]
        project: Option<PathBuf>,
    },
    /// Check for missing dependencies in a virtualenv
    Check { venv_path: PathBuf },
    /// Diff two virtual environment snapshots
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
    },
}

#[derive(Subcommand)]
enum BundleCommands {
    /// Create a consolidated .lens forensic archive from multiple system sources
    Create {
        /// Output path for the .lens bundle
        output: PathBuf,
        #[arg(long)]
        disk: Option<PathBuf>,
        #[arg(long)]
        log: Option<PathBuf>,
        #[arg(long)]
        trace: Option<PathBuf>,
        #[arg(long)]
        test: Option<PathBuf>,
        #[arg(long)]
        sys: Option<PathBuf>,
        #[arg(long)]
        env: Option<PathBuf>,
    },
    /// Inspect contents and diagnostics of a .lens bundle
    Inspect { bundle: PathBuf },
}

#[derive(Debug, Serialize, Deserialize)]
struct LensBundle {
    schema: String,
    version: String,
    created_at: String,
    disk_snapshot: Option<serde_json::Value>,
    log_summary: Option<serde_json::Value>,
    trace_snapshot: Option<serde_json::Value>,
    test_run: Option<serde_json::Value>,
    sys_snapshot: Option<serde_json::Value>,
    env_snapshot: Option<serde_json::Value>,
}

fn main() -> Result<()> {
    let cli = Cli::parse();

    match cli.command {
        Commands::Disk { action } => match action {
            DiskCommands::Scan { path, json } => {
                let scanner = DiskScanner::new(ScanOptions::default());
                let result = scanner.scan(&path)?;
                if json {
                    let snapshot = SnapshotV2::from_tree(
                        &result.tree,
                        result.root_id,
                        result.complete,
                        result.truncated,
                    );
                    println!("{}", to_deterministic_pretty(&snapshot)?);
                } else {
                    let root_node = &result.tree.nodes[result.root_id as usize];
                    println!("Scan completed successfully for: {:?}", path);
                    println!("Total entries: {}", result.scanned_entries);
                    println!(
                        "Total logical size: {} bytes ({:.2} MB)",
                        root_node.size,
                        root_node.size as f64 / 1_048_576.0
                    );
                    println!(
                        "Total allocated size: {} bytes ({:.2} MB)",
                        root_node.allocated_size,
                        root_node.allocated_size as f64 / 1_048_576.0
                    );
                }
            }
            DiskCommands::Duplicates { path, min_size } => {
                let scanner = DiskScanner::new(ScanOptions::default());
                let result = scanner.scan(&path)?;
                let finder = DuplicateFinder::new(min_size);
                let groups = finder.find_in_tree(&result.tree, &path)?;
                println!("Found {} duplicate group(s):", groups.len());
                for (i, group) in groups.iter().enumerate() {
                    println!("\n[{}] Hash: {}", i + 1, group.sha256);
                    println!(
                        "    File size: {} bytes, Reclaimable: {} bytes",
                        group.size, group.reclaimable_bytes
                    );
                    for file in &group.files {
                        println!("    - {:?}", file);
                    }
                }
            }
            DiskCommands::Trash { path } => {
                let trash = TrashManager::default();
                let receipt = trash.move_to_trash(&path)?;
                println!("Successfully trashed: {:?}", receipt.original_path);
                println!("Trash location: {:?}", receipt.trashed_file_path);
                println!("Info receipt: {:?}", receipt.info_path);
            }
        },
        Commands::Abi { action } => match action {
            AbiCommands::Inspect { binary } => {
                let bytes = fs::read(&binary).map_err(|e| lens_core::LensError::Io {
                    path: binary.clone(),
                    source: e,
                })?;
                let report = inspect_elf(&binary, &bytes);
                println!("{}", to_deterministic_pretty(&report)?);
            }
            AbiCommands::Diff {
                baseline,
                candidate,
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
                println!("{}", to_deterministic_pretty(&diff)?);
            }
        },
        Commands::Log { action } => match action {
            LogCommands::Inspect { path } => {
                let indexer = LogIndexer::open(&path)?;
                println!("Log file: {:?}", path);
                println!("Total lines indexed: {}", indexer.len());
            }
            LogCommands::Filter {
                path,
                query,
                min_level,
            } => {
                let indexer = LogIndexer::open(&path)?;
                let mut filter = LogFilter::new();
                if let Some(q) = query {
                    filter = filter.with_query(&q);
                }
                if let Some(lvl_str) = min_level {
                    filter = filter.with_min_level(LogLevel::parse(&lvl_str));
                }

                let mut matched = 0;
                for idx in 0..indexer.len() {
                    if let Some(line) = indexer.get_line(idx) {
                        let record = parse_line(line, idx + 1);
                        if filter.matches(&record) {
                            println!("[{}] {}", record.line_number, record.raw);
                            matched += 1;
                        }
                    }
                }
                eprintln!("\nMatched {} of {} line(s).", matched, indexer.len());
            }
        },
        Commands::Test { action } => match action {
            TestCommands::Parse { file, project } => {
                let bytes = fs::read(&file).map_err(|e| lens_core::LensError::Io {
                    path: file.clone(),
                    source: e,
                })?;
                let run = parse_junit_xml(&bytes, &project)?;
                println!("{}", to_deterministic_pretty(&run)?);
            }
            TestCommands::Diff {
                baseline,
                candidate,
            } => {
                let bytes_base = fs::read(&baseline).map_err(|e| lens_core::LensError::Io {
                    path: baseline.clone(),
                    source: e,
                })?;
                let bytes_cand = fs::read(&candidate).map_err(|e| lens_core::LensError::Io {
                    path: candidate.clone(),
                    source: e,
                })?;
                let run_base = parse_junit_xml(&bytes_base, "baseline")?;
                let run_cand = parse_junit_xml(&bytes_cand, "candidate")?;
                let diff = diff_test_runs(&run_base, &run_cand);
                println!("{}", to_deterministic_pretty(&diff)?);
            }
        },
        Commands::Trace { action } => match action {
            TraceCommands::Analyze { trace_file } => {
                let content =
                    fs::read_to_string(&trace_file).map_err(|e| lens_core::LensError::Io {
                        path: trace_file.clone(),
                        source: e,
                    })?;
                let analyzer = TraceAnalyzer::new();
                let snapshot = analyzer.analyze_lines(content.lines());
                println!("{}", to_deterministic_pretty(&snapshot)?);
            }
            TraceCommands::Diff {
                baseline,
                candidate,
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
                println!("{}", to_deterministic_pretty(&diff)?);
            }
        },
        Commands::Sys { action } => match action {
            SysCommands::Inspect { path } => {
                let units = load_units(&path)?;
                let graph = OrderingGraph::build(&units);
                let cycles = graph.find_cycles();

                let snapshot = SystemdSnapshot {
                    schema: lens_sys::SNAPSHOT_SCHEMA_V1.to_string(),
                    version: "0.3.0".to_string(),
                    semantics: "systemd-255-subset-v1".to_string(),
                    units,
                    cycles,
                    diagnostics: vec![],
                };
                println!("{}", to_deterministic_pretty(&snapshot)?);
            }
            SysCommands::Cycles { dir } => {
                let units = load_units(&dir)?;
                let graph = OrderingGraph::build(&units);
                let cycles = graph.find_cycles();
                if cycles.is_empty() {
                    println!("No dependency cycles detected among {} units.", units.len());
                } else {
                    println!("Detected {} cycle(s):", cycles.len());
                    for (i, c) in cycles.iter().enumerate() {
                        println!("  [{}] {}", i + 1, c.join(" -> "));
                    }
                }
            }
            SysCommands::Diff {
                baseline,
                candidate,
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
                let s1: SystemdSnapshot = serde_json::from_str(&b_str)?;
                let s2: SystemdSnapshot = serde_json::from_str(&c_str)?;
                let diff = diff_systemd(&s1, &s2);
                println!("{}", to_deterministic_pretty(&diff)?);
            }
        },
        Commands::Build { action } => match action {
            BuildCommands::Inspect { file } => {
                let content = fs::read_to_string(&file).map_err(|e| lens_core::LensError::Io {
                    path: file.clone(),
                    source: e,
                })?;
                let entries: Vec<CompileCommandEntry> = serde_json::from_str(&content)?;
                let units: Vec<_> = entries.iter().map(parse_command_entry).collect();
                println!("{}", to_deterministic_pretty(&units)?);
            }
            BuildCommands::Impact { file, header } => {
                let content = fs::read_to_string(&file).map_err(|e| lens_core::LensError::Io {
                    path: file.clone(),
                    source: e,
                })?;
                let entries: Vec<CompileCommandEntry> = serde_json::from_str(&content)?;
                let mut graph = ImpactGraph::new();
                for entry in &entries {
                    let unit = parse_command_entry(entry);
                    if let Ok(src) = fs::read_to_string(&unit.file) {
                        for inc in lens_build::extract_includes(&src) {
                            graph.add_unit_include(&unit.file, &inc);
                        }
                    }
                }
                let report = graph.compute_impact(&header);
                println!("{}", to_deterministic_pretty(&report)?);
            }
            BuildCommands::Diff {
                baseline,
                candidate,
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
                println!("{}", to_deterministic_pretty(&diff)?);
            }
        },
        Commands::Env { action } => match action {
            EnvCommands::Inspect { venv_path, project } => {
                let mut venv = inspect_venv(&venv_path).map_err(|e| lens_core::LensError::Io {
                    path: venv_path.clone(),
                    source: e,
                })?;
                if let Some(ref proj) = project {
                    venv.shadowing_issues = detect_shadowing(proj, &venv.packages);
                }
                let snapshot = EnvSnapshot {
                    schema: lens_env::SNAPSHOT_SCHEMA_V1.to_string(),
                    version: "0.3.0".to_string(),
                    venv,
                };
                println!("{}", to_deterministic_pretty(&snapshot)?);
            }
            EnvCommands::Check { venv_path } => {
                let venv = inspect_venv(&venv_path).map_err(|e| lens_core::LensError::Io {
                    path: venv_path.clone(),
                    source: e,
                })?;
                if venv.missing_dependencies.is_empty() {
                    println!("All dependencies satisfied in {:?}.", venv_path);
                } else {
                    println!(
                        "Found {} missing dependenc(ies):",
                        venv.missing_dependencies.len()
                    );
                    for d in &venv.missing_dependencies {
                        println!("  - {}", d);
                    }
                }
            }
            EnvCommands::Diff {
                baseline,
                candidate,
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
                println!("{}", to_deterministic_pretty(&diff)?);
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
            } => {
                let mut disk_snapshot = None;
                if let Some(ref d) = disk {
                    let scanner = DiskScanner::new(ScanOptions::default());
                    if let Ok(res) = scanner.scan(d) {
                        let snap = SnapshotV2::from_tree(
                            &res.tree,
                            res.root_id,
                            res.complete,
                            res.truncated,
                        );
                        disk_snapshot = serde_json::to_value(snap).ok();
                    }
                }

                let mut log_summary = None;
                if let Some(ref l) = log {
                    if let Ok(indexer) = LogIndexer::open(l) {
                        log_summary = Some(serde_json::json!({
                            "path": l.to_string_lossy(),
                            "total_lines": indexer.len(),
                        }));
                    }
                }

                let mut trace_snapshot = None;
                if let Some(ref t) = trace {
                    if let Ok(c) = fs::read_to_string(t) {
                        let analyzer = TraceAnalyzer::new();
                        let snap = analyzer.analyze_lines(c.lines());
                        trace_snapshot = serde_json::to_value(snap).ok();
                    }
                }

                let mut test_run = None;
                if let Some(ref t) = test {
                    if let Ok(bytes) = fs::read(t) {
                        if let Ok(run) = parse_junit_xml(&bytes, "bundle") {
                            test_run = serde_json::to_value(run).ok();
                        }
                    }
                }

                let mut sys_snapshot = None;
                if let Some(ref s) = sys {
                    if let Ok(units) = load_units(s) {
                        let graph = OrderingGraph::build(&units);
                        let cycles = graph.find_cycles();
                        let snap = SystemdSnapshot {
                            schema: lens_sys::SNAPSHOT_SCHEMA_V1.to_string(),
                            version: "0.3.0".to_string(),
                            semantics: "systemd-255-subset-v1".to_string(),
                            units,
                            cycles,
                            diagnostics: vec![],
                        };
                        sys_snapshot = serde_json::to_value(snap).ok();
                    }
                }

                let mut env_snapshot = None;
                if let Some(ref e) = env {
                    if let Ok(venv) = inspect_venv(e) {
                        let snap = EnvSnapshot {
                            schema: lens_env::SNAPSHOT_SCHEMA_V1.to_string(),
                            version: "0.3.0".to_string(),
                            venv,
                        };
                        env_snapshot = serde_json::to_value(snap).ok();
                    }
                }

                let bundle = LensBundle {
                    schema: "lens.bundle/v1".to_string(),
                    version: "0.3.0".to_string(),
                    created_at: "2026-10-05T00:00:00Z".to_string(),
                    disk_snapshot,
                    log_summary,
                    trace_snapshot,
                    test_run,
                    sys_snapshot,
                    env_snapshot,
                };

                let json_data = to_deterministic_pretty(&bundle)?;
                fs::write(&output, json_data).map_err(|e| lens_core::LensError::Io {
                    path: output.clone(),
                    source: e,
                })?;
                println!(
                    "Successfully created forensic flight recorder bundle: {:?}",
                    output
                );
            }
            BundleCommands::Inspect { bundle } => {
                let content =
                    fs::read_to_string(&bundle).map_err(|e| lens_core::LensError::Io {
                        path: bundle.clone(),
                        source: e,
                    })?;
                let b: LensBundle = serde_json::from_str(&content)?;
                println!("=== Lens Forensic Flight Recorder Bundle ===");
                println!("Schema: {}", b.schema);
                println!("Bundle Version: {}", b.version);
                println!("Created At: {}", b.created_at);
                println!("Captured Components:");
                println!(
                    "  - Disk Snapshot: {}",
                    if b.disk_snapshot.is_some() {
                        "Present"
                    } else {
                        "None"
                    }
                );
                println!(
                    "  - Log Summary:   {}",
                    if b.log_summary.is_some() {
                        "Present"
                    } else {
                        "None"
                    }
                );
                println!(
                    "  - Trace Data:    {}",
                    if b.trace_snapshot.is_some() {
                        "Present"
                    } else {
                        "None"
                    }
                );
                println!(
                    "  - Test Run:      {}",
                    if b.test_run.is_some() {
                        "Present"
                    } else {
                        "None"
                    }
                );
                println!(
                    "  - Systemd Units: {}",
                    if b.sys_snapshot.is_some() {
                        "Present"
                    } else {
                        "None"
                    }
                );
                println!(
                    "  - Python Env:    {}",
                    if b.env_snapshot.is_some() {
                        "Present"
                    } else {
                        "None"
                    }
                );
            }
        },
    }

    Ok(())
}

fn load_units(path: &Path) -> Result<BTreeMap<String, SystemdUnit>> {
    let mut units = BTreeMap::new();
    if path.is_file() {
        let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("unit");
        let content = fs::read_to_string(path).map_err(|e| lens_core::LensError::Io {
            path: path.to_path_buf(),
            source: e,
        })?;
        let unit = parse_unit_content(&content, name, Some(&path.to_string_lossy()));
        units.insert(name.to_string(), unit);
    } else if path.is_dir() {
        if let Ok(entries) = fs::read_dir(path) {
            for entry in entries.flatten() {
                let p = entry.path();
                if p.is_file() {
                    if let Some(name) = p.file_name().and_then(|n| n.to_str()) {
                        if name.ends_with(".service")
                            || name.ends_with(".target")
                            || name.ends_with(".socket")
                        {
                            if let Ok(content) = fs::read_to_string(&p) {
                                let unit =
                                    parse_unit_content(&content, name, Some(&p.to_string_lossy()));
                                units.insert(name.to_string(), unit);
                            }
                        }
                    }
                }
            }
        }
    }
    Ok(units)
}
