use crate::cli::*;
use crate::doctor::{self, HealthStatus};
use lens_abi::{diff_reports, inspect_elf};
use lens_build::{diff_compilations, parse_command_entry, CompileCommandEntry, ImpactGraph};
use lens_core::{
    create_bundle_archive, inspect_bundle, to_deterministic_pretty, verify_bundle_archive,
    BundleArtifact, Result,
};
use lens_disk::{DiskScanner, DuplicateFinder, ScanOptions, SnapshotV2, TrashManager};
use lens_env::{detect_shadowing, diff_environments, inspect_venv, EnvSnapshot};
use lens_log::{LogFilter, LogIndexer, LogLevel};
use lens_net::{diff_net_reports, inspect_network, NetReport};
use lens_sys::{diff_systemd, OrderingGraph, SystemdSnapshot};
use lens_test::{diff_test_runs, parse_junit_xml};
use lens_trace::{diff_snapshots, TraceAnalyzer};
use std::fs;

pub fn dispatch(command: Commands) -> Result<()> {
    match command {
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
                        if filter.matches_line(line, idx + 1) {
                            println!("[{}] {}", idx + 1, line);
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
                println!("{}", to_deterministic_pretty(&snapshot)?);
            }
            SysCommands::Cycles { dir } => {
                let units = lens_sys::load_units(&dir)?;
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
                    graph.add_translation_unit(&unit);
                }
                // Normalize --header the same way unit paths are normalized so
                // relative spellings like include/common.h match graph keys.
                let cwd = std::env::current_dir()
                    .map(|p| p.to_string_lossy().into_owned())
                    .unwrap_or_else(|_| "/".to_string());
                let target = lens_build::normalize_path(&header, &cwd);
                let report = graph.compute_impact(&target);
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
                    version: env!("CARGO_PKG_VERSION").to_string(),
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
                net,
            } => {
                // Fail-closed collection: every capture error is preserved in the
                // manifest's diagnostics rather than silently dropped.
                let mut artifacts: Vec<BundleArtifact> = Vec::new();
                let mut diagnostics: Vec<String> = Vec::new();

                macro_rules! collect {
                    ($name:literal, $entry:literal, $produce:expr) => {
                        match $produce.and_then(|v| BundleArtifact::json($entry, &v)) {
                            Ok(artifact) => artifacts.push(artifact),
                            Err(e) => diagnostics.push(format!("{}: {}", $name, e)),
                        }
                    };
                }

                if let Some(ref d) = disk {
                    collect!("disk", "reports/disk_snapshot.json", {
                        let res = DiskScanner::new(ScanOptions::default()).scan(d)?;
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
                        let venv =
                            inspect_venv(e).map_err(|err| lens_core::LensError::InvalidInput {
                                message: format!("venv {}: {}", e.display(), err),
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

                println!(
                    "Successfully created forensic flight recorder bundle: {:?}",
                    output
                );
                println!("  Artifacts embedded: {}", manifest.sources.len());
                if !diagnostics.is_empty() {
                    println!("  Collection warnings recorded in manifest:");
                    for d in &diagnostics {
                        println!("    - {}", d);
                    }
                }
            }
            BundleCommands::Inspect { bundle } => {
                let report = inspect_bundle(&bundle)?;
                println!("=== Lens Forensic Flight Recorder Bundle ===");
                println!("Entries: {}", report.entries.len());
                match &report.manifest {
                    Some(m) => {
                        println!("Schema:        {}", m.schema);
                        println!("Tool:          {} v{}", m.tool, m.version);
                        println!("Created At:    {}", m.created_at);
                        println!("Manifest Files:");
                        for s in &m.sources {
                            println!("  - {} ({} bytes)", s.path, s.size);
                        }
                        if !m.diagnostics.is_empty() {
                            println!("Capture Diagnostics:");
                            for d in &m.diagnostics {
                                println!("  ! {}", d);
                            }
                        }
                    }
                    None => println!("Manifest: NOT FOUND (untrusted bundle)"),
                }
                println!("Archive Entries:");
                for e in &report.entries {
                    println!("  - {} ({} bytes)", e.name, e.size);
                }
            }
            BundleCommands::Verify { bundle } => {
                let report = verify_bundle_archive(&bundle)?;
                if report.is_valid {
                    println!("Bundle verification SUCCESSFUL: {:?}", bundle);
                    println!("  Total manifest files:    {}", report.total_files);
                    println!("  Verified SHA-256 files:  {}", report.verified_files);
                    println!("  Integrity: 100% Authentic & Tamper-Free");
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
                    return Err(lens_core::LensError::InvalidInput {
                        message: "Bundle cryptographic verification failed".to_string(),
                    });
                }
            }
        },
        Commands::Net { action } => match action {
            NetCommands::Inspect { proc_dir, json } => {
                let report = inspect_network(proc_dir.as_deref())?;
                if json {
                    println!("{}", to_deterministic_pretty(&report)?);
                } else {
                    println!("Network & Socket Inspection Summary:");
                    println!("  Total Sockets:          {}", report.summary.total_sockets);
                    println!(
                        "  Listening Ports:        {}",
                        report.summary.listening_ports
                    );
                    println!(
                        "  Established Conns:      {}",
                        report.summary.established_connections
                    );
                    println!(
                        "  TIME_WAIT Sockets:      {}",
                        report.summary.time_wait_sockets
                    );
                    println!(
                        "  Orphan Sockets:         {}",
                        report.summary.orphan_sockets
                    );
                    println!(
                        "  UNIX Domain Sockets:    {}",
                        report.summary.unix_domain_sockets
                    );
                    println!("\nActive Listening Ports:");
                    println!(
                        "{:<8} {:<24} {:<10} {:<8} {:<16}",
                        "PROTO", "LOCAL ADDRESS", "INODE", "PID", "PROCESS"
                    );
                    for l in &report.listening {
                        let proc_str = l
                            .process
                            .as_ref()
                            .map(|p| p.name.as_str())
                            .unwrap_or("<orphan>");
                        let pid_str = l
                            .process
                            .as_ref()
                            .map(|p| p.pid.to_string())
                            .unwrap_or_else(|| "-".to_string());
                        let addr = format!("{}:{}", l.local_address, l.local_port);
                        println!(
                            "{:<8} {:<24} {:<10} {:<8} {}",
                            format!("{:?}", l.kind),
                            addr,
                            l.inode,
                            pid_str,
                            proc_str
                        );
                    }
                }
            }
            NetCommands::Diff {
                baseline,
                candidate,
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
                println!("{}", to_deterministic_pretty(&diff)?);
            }
        },
        Commands::Doctor {
            root,
            procfs,
            systemd_dir,
            json,
        } => {
            let report =
                doctor::run_doctor(root.as_deref(), procfs.as_deref(), systemd_dir.as_deref());
            if json {
                println!("{}", to_deterministic_pretty(&report)?);
            } else {
                println!("=== Lens System Doctor Diagnosis ===");
                println!("Overall Status: {:?}", report.overall_status);
                println!(
                    "Summary: {} Total | {} Passed | {} Warnings | {} Failures\n",
                    report.summary.total,
                    report.summary.passed,
                    report.summary.warnings,
                    report.summary.failures
                );
                println!("{:<12} {:<30} {:<8} MESSAGE", "CATEGORY", "CHECK", "STATUS");
                for c in &report.checks {
                    let status_str = match c.status {
                        HealthStatus::Pass => "[PASS]",
                        HealthStatus::Warn => "[WARN]",
                        HealthStatus::Fail => "[FAIL]",
                    };
                    println!(
                        "{:<12} {:<30} {:<8} {}",
                        c.category, c.name, status_str, c.message
                    );
                    if let Some(ref rec) = c.recommendation {
                        println!("             -> Recommendation: {}", rec);
                    }
                }
            }
        }
        Commands::Tui { path } => {
            lens_tui::run(&path).map_err(|e| lens_core::LensError::InvalidInput {
                message: format!("TUI failed: {}", e),
            })?;
        }
        Commands::Completion { shell } => {
            use clap::CommandFactory;
            let mut cmd = Cli::command();
            clap_complete::generate(shell, &mut cmd, "lens", &mut std::io::stdout());
        }
    }

    Ok(())
}
