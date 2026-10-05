use clap::{Parser, Subcommand};
use std::path::PathBuf;

#[derive(Parser)]
#[command(name = "lens")]
#[command(version)]
#[command(about = "Unified Systems Diagnostics and Forensic Platform", long_about = None)]
pub struct Cli {
    #[command(subcommand)]
    pub command: Commands,
}

#[derive(Subcommand)]
pub enum Commands {
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
    /// Linux socket, listening port, and network connection forensics
    Net {
        #[command(subcommand)]
        action: NetCommands,
    },
    /// Forensic Flight Recorder: bundle multiple diagnostic artifacts into a .lens container
    Bundle {
        #[command(subcommand)]
        action: BundleCommands,
    },
    /// Unified system health check across storage, network, services, and environment
    Doctor {
        #[arg(long)]
        root: Option<PathBuf>,
        #[arg(long)]
        procfs: Option<PathBuf>,
        #[arg(long)]
        systemd_dir: Option<PathBuf>,
        #[arg(long)]
        json: bool,
    },
    /// Interactive terminal dashboard (storage, services, logs, network)
    Tui {
        /// Initial path to inspect
        #[arg(default_value = ".")]
        path: PathBuf,
    },
    /// Generate shell auto-completion script
    Completion {
        #[arg(value_enum)]
        shell: clap_complete::Shell,
    },
}

#[derive(Subcommand)]
pub enum DiskCommands {
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
pub enum AbiCommands {
    /// Inspect an ELF binary and output JSON report
    Inspect { binary: PathBuf },
    /// Compare two ELF binaries and report 3-state ABI compatibility
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
    },
}

#[derive(Subcommand)]
pub enum LogCommands {
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
pub enum TestCommands {
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
pub enum TraceCommands {
    /// Analyze strace output log and output JSON snapshot
    Analyze { trace_file: PathBuf },
    /// Compare two strace log runs and detect latency shifts/new errors
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
    },
}

#[derive(Subcommand)]
pub enum SysCommands {
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
pub enum BuildCommands {
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
pub enum EnvCommands {
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
pub enum NetCommands {
    /// Inspect open listening ports, sockets, and associated processes
    Inspect {
        #[arg(long)]
        proc_dir: Option<PathBuf>,
        #[arg(long)]
        json: bool,
    },
    /// Compare two network snapshot JSON reports
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
    },
}

#[derive(Subcommand)]
pub enum BundleCommands {
    /// Create a consolidated .lens forensic archive (tar.gz + signed manifest)
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
        /// Capture a live network socket report from procfs
        #[arg(long)]
        net: bool,
    },
    /// Inspect contents and diagnostics of a .lens bundle
    Inspect { bundle: PathBuf },
    /// Verify cryptographic SHA-256 integrity and authenticity of a .lens bundle
    Verify { bundle: PathBuf },
}
