use clap::{Parser, Subcommand, ValueEnum};
use std::path::PathBuf;

/// `--fail-on` severity threshold for `lens doctor`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, ValueEnum)]
pub enum FailOn {
    Warn,
    Fail,
}

/// `--format` output selector, unified across report commands.
/// Commands keep their historical default when the flag is absent.
#[derive(Clone, Copy, Debug, PartialEq, Eq, ValueEnum)]
pub enum OutputFormat {
    Text,
    Json,
}

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
    /// High-speed JUnit test report parsing and regression diffing
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
        /// Emit the report as JSON (equivalent to --format json)
        #[arg(long)]
        json: bool,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
        /// Lowest severity that exits 1: `fail` (default) or `warn`
        #[arg(long, value_enum, default_value_t = FailOn::Fail)]
        fail_on: FailOn,
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
        /// Emit the snapshot as JSON (equivalent to --format json)
        #[arg(long)]
        json: bool,
        /// Output format; default preserves the current text summary
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
        /// Parallelize per-entry stat with rayon
        #[arg(long)]
        parallel: bool,
        /// Maximum directory depth to descend
        #[arg(long)]
        max_depth: Option<usize>,
        /// Skip entries whose name contains this string (repeatable)
        #[arg(long)]
        exclude: Vec<String>,
        /// Do not cross filesystem boundaries
        #[arg(short = 'x', long)]
        one_file_system: bool,
        /// Show the N largest top-level entries in text output (0 disables)
        #[arg(long, default_value_t = 10)]
        top: usize,
    },
    /// Find duplicate files with hard link deduplication
    Duplicates {
        path: PathBuf,
        #[arg(long, default_value = "1024")]
        min_size: u64,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Safely move a file to trash following the FreeDesktop Trash spec
    Trash { path: PathBuf },
}

#[derive(Subcommand)]
pub enum AbiCommands {
    /// Inspect an ELF binary and output JSON report
    Inspect {
        binary: PathBuf,
        /// Output format; default is the JSON report
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Compare two ELF binaries and report 3-state ABI compatibility
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum LogCommands {
    /// Inspect log file metrics and line counts
    Inspect {
        path: PathBuf,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Search and filter log lines (memory-mapped, memchr-accelerated)
    Filter {
        path: PathBuf,
        #[arg(long)]
        query: Option<String>,
        #[arg(long)]
        min_level: Option<String>,
        /// Keep lines whose level could not be determined when --min-level is set
        #[arg(long)]
        include_unknown: bool,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum TestCommands {
    /// Parse JUnit XML test report(s) — file, directory of *.xml, or glob
    Parse {
        file: PathBuf,
        #[arg(long, default_value = "default")]
        project: String,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Compare two JUnit XML runs (file, directory of *.xml, or glob)
    /// and report structured regressions
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum TraceCommands {
    /// Analyze strace output log and output JSON snapshot
    Analyze {
        trace_file: PathBuf,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Compare two strace log runs and detect latency shifts/new errors
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum SysCommands {
    /// Inspect systemd unit file or unit directory
    Inspect {
        path: PathBuf,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Detect ordering cycles in a directory of systemd unit files
    Cycles {
        dir: PathBuf,
        /// Output format; default is the text cycle listing
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Compare two systemd snapshots
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum BuildCommands {
    /// Inspect compile_commands.json database
    Inspect {
        file: PathBuf,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Calculate reverse compilation impact for a header
    Impact {
        file: PathBuf,
        #[arg(long)]
        header: String,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Diff two compile_commands.json databases
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum EnvCommands {
    /// Inspect Python virtualenv and optionally check local project shadowing
    Inspect {
        venv_path: PathBuf,
        #[arg(long)]
        project: Option<PathBuf>,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Check for missing dependencies in a virtualenv
    Check {
        venv_path: PathBuf,
        /// Activate `extra == "name"` dependency markers (comma-separated)
        #[arg(long, value_delimiter = ',')]
        extras: Vec<String>,
        /// Output format; default is the text report
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Diff two virtual environment snapshots
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum NetCommands {
    /// Inspect open listening ports, sockets, and associated processes
    Inspect {
        #[arg(long)]
        proc_dir: Option<PathBuf>,
        /// Emit the report as JSON (equivalent to --format json)
        #[arg(long)]
        json: bool,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
        /// Omit unix-domain sockets from the output
        #[arg(long)]
        no_unix: bool,
    },
    /// Compare two network snapshot JSON reports
    Diff {
        baseline: PathBuf,
        candidate: PathBuf,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum BundleCommands {
    /// Create a consolidated .lens forensic archive (tar.gz + embedded manifest)
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
        /// Overwrite the output file if it already exists
        #[arg(long)]
        force: bool,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Inspect contents and diagnostics of a .lens bundle
    Inspect {
        bundle: PathBuf,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Verify bundle contents against its embedded manifest's SHA-256 checksums
    Verify {
        bundle: PathBuf,
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}
