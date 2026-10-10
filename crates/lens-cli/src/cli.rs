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
/// `jsonl` is honored by `log filter` (one object per matched line);
/// elsewhere it falls back to `json`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, ValueEnum)]
pub enum OutputFormat {
    Text,
    Json,
    Jsonl,
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
        /// Filesystem root to check (default /)
        #[arg(long)]
        root: Option<PathBuf>,
        /// procfs mount to read instead of /proc
        #[arg(long)]
        procfs: Option<PathBuf>,
        /// Unit directory to load instead of the systemd search path
        #[arg(long)]
        systemd_dir: Option<PathBuf>,
        /// Emit the report as JSON (equivalent to --format json)
        #[arg(long)]
        json: bool,
        /// Output format; default is the text report
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
        /// Log file to show in the Logs tab instead of auto-detection
        #[arg(long)]
        log: Option<PathBuf>,
    },
    /// Generate shell auto-completion script
    Completion {
        /// Shell to generate completions for
        #[arg(value_enum)]
        shell: clap_complete::Shell,
    },
}

#[derive(Subcommand)]
pub enum DiskCommands {
    /// Scan directory and print space usage summary or snapshot JSON
    Scan {
        /// Directory to scan
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
        /// Directory to scan for duplicates
        path: PathBuf,
        /// Minimum file size in bytes to consider
        #[arg(long, default_value = "1024")]
        min_size: u64,
        /// Output format; default preserves the current text summary
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Safely move paths to trash following the FreeDesktop Trash spec;
    /// `list`/`restore` manage existing entries
    #[command(
        args_conflicts_with_subcommands = true,
        subcommand_precedence_over_arg = true
    )]
    Trash {
        #[command(subcommand)]
        action: Option<TrashCommands>,
        /// Paths to move to trash
        paths: Vec<PathBuf>,
        /// Report what would be trashed without moving anything
        #[arg(long)]
        dry_run: bool,
        /// Output format; default is the text report
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum TrashCommands {
    /// List trash entries with their original paths
    List {
        /// Trash root to inspect instead of the home trash (e.g. a
        /// mount's `.Trash-$uid` after a cross-device fallback)
        #[arg(long)]
        trash_dir: Option<PathBuf>,
        /// Output format; default is the text listing
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Restore a trashed entry by its trash name (see `trash list`)
    Restore {
        /// Trash-internal name, e.g. `foo.txt` or `foo.txt_1`
        name: String,
        /// Trash root containing the entry instead of the home trash
        #[arg(long)]
        trash_dir: Option<PathBuf>,
    },
}

#[derive(Subcommand)]
pub enum AbiCommands {
    /// Inspect an ELF binary and output JSON report
    Inspect {
        /// ELF binary or shared library to inspect
        binary: PathBuf,
        /// Output format; default is the JSON report
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Compare two ELF binaries and report 3-state ABI compatibility
    Diff {
        /// Older/reference ELF binary
        baseline: PathBuf,
        /// Newer ELF binary to compare against the baseline
        candidate: PathBuf,
        /// Output format; default is the JSON diff
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum LogCommands {
    /// Inspect log file metrics and line counts.
    /// PATH may be `-` for stdin; `.gz` files are decompressed (bounded).
    Inspect {
        /// Log file path, `-` for stdin, or a `.gz` file
        path: PathBuf,
        /// Output format; default is the text summary
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Search and filter log lines (memory-mapped, memchr-accelerated).
    /// PATH may be `-` for stdin; `.gz` files are decompressed (bounded).
    Filter {
        /// Log file path, `-` for stdin, or a `.gz` file
        path: PathBuf,
        /// Case-insensitive substring to match
        #[arg(long)]
        query: Option<String>,
        /// Regex pattern alternative to --query's substring match
        #[arg(long, conflicts_with = "query")]
        regex: Option<String>,
        /// Lowest severity to emit: trace|debug|info|warn|error|fatal
        #[arg(long)]
        min_level: Option<String>,
        /// Keep lines whose level could not be determined when --min-level is set
        #[arg(long)]
        include_unknown: bool,
        /// Stop after N matched lines
        #[arg(long)]
        limit: Option<usize>,
        /// Show N lines of context around each match
        #[arg(long, default_value_t = 0)]
        context: usize,
        /// `jsonl` prints one object per matched line
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum TestCommands {
    /// Parse JUnit XML test report(s) — file, directory of *.xml, or glob
    Parse {
        /// JUnit XML file, directory, or glob pattern
        file: PathBuf,
        /// Project label recorded in the snapshot
        #[arg(long, default_value = "default")]
        project: String,
        /// Output format; default is the JSON snapshot
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Compare two JUnit XML runs (file, directory of *.xml, or glob)
    /// and report structured regressions
    Diff {
        /// Older/reference JUnit XML file, directory, or glob
        baseline: PathBuf,
        /// Newer JUnit XML file, directory, or glob
        candidate: PathBuf,
        /// Output format; default is the JSON diff
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum TraceCommands {
    /// Analyze strace output log and output JSON snapshot
    Analyze {
        /// strace output file to analyze
        trace_file: PathBuf,
        /// Output format; default is the JSON snapshot
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Compare two strace log runs and detect latency shifts/new errors
    Diff {
        /// Older/reference strace output file
        baseline: PathBuf,
        /// Newer strace output file
        candidate: PathBuf,
        /// Output format; default is the JSON diff
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum SysCommands {
    /// Inspect systemd unit file or unit directory; with no path, merges
    /// the systemd search path (/etc, /run, /usr/lib, /lib)
    Inspect {
        /// Unit file or directory; omit to merge the systemd search path
        path: Option<PathBuf>,
        /// Output format; default is the JSON snapshot
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Detect ordering cycles in systemd units; with no dir, merges the
    /// systemd search path (/etc, /run, /usr/lib, /lib)
    Cycles {
        /// Unit directory; omit to merge the systemd search path
        dir: Option<PathBuf>,
        /// Output format; default is the text cycle listing
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Compare two systemd snapshots
    Diff {
        /// Older/reference snapshot JSON or unit directory
        baseline: PathBuf,
        /// Newer snapshot JSON or unit directory
        candidate: PathBuf,
        /// Output format; default is the JSON diff
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum BuildCommands {
    /// Inspect compile_commands.json database
    Inspect {
        /// Path to compile_commands.json
        file: PathBuf,
        /// Output format; default is the JSON snapshot
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Calculate reverse compilation impact for a header
    Impact {
        /// Path to compile_commands.json
        file: PathBuf,
        /// Header to analyze; relative paths resolve against the
        /// compile database and each entry's `directory`
        #[arg(long)]
        header: String,
        /// Output format; default is the JSON report
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Diff two compile_commands.json databases
    Diff {
        /// Older/reference compile_commands.json
        baseline: PathBuf,
        /// Newer compile_commands.json
        candidate: PathBuf,
        /// Output format; default is the JSON diff
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum EnvCommands {
    /// Inspect Python virtualenv and optionally check local project shadowing
    Inspect {
        /// Path to the virtualenv root (contains pyvenv.cfg)
        venv_path: PathBuf,
        /// Project directory to scan for stdlib/package shadowing
        #[arg(long)]
        project: Option<PathBuf>,
        /// Output format; default is the JSON snapshot
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Check for missing dependencies in a virtualenv
    Check {
        /// Path to the virtualenv root (contains pyvenv.cfg)
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
        /// Older/reference env snapshot JSON
        baseline: PathBuf,
        /// Newer env snapshot JSON
        candidate: PathBuf,
        /// Output format; default is the JSON diff
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}

#[derive(Subcommand)]
pub enum NetCommands {
    /// Inspect open listening ports, sockets, and associated processes
    Inspect {
        /// procfs root to read instead of /proc
        #[arg(long)]
        proc_dir: Option<PathBuf>,
        /// Emit the report as JSON (equivalent to --format json)
        #[arg(long)]
        json: bool,
        /// Output format; default is the text summary
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
        /// Omit unix-domain sockets from the output
        #[arg(long)]
        no_unix: bool,
    },
    /// Compare two network snapshot JSON reports
    Diff {
        /// Older/reference net snapshot JSON
        baseline: PathBuf,
        /// Newer net snapshot JSON
        candidate: PathBuf,
        /// Output format; default is the JSON diff
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
        /// Directory to scan for a disk snapshot artifact
        #[arg(long)]
        disk: Option<PathBuf>,
        /// Log file to embed (raw content, tail-bounded)
        #[arg(long)]
        log: Option<PathBuf>,
        /// strace output file to analyze into the bundle
        #[arg(long)]
        trace: Option<PathBuf>,
        /// JUnit XML file/directory to parse into the bundle
        #[arg(long)]
        test: Option<PathBuf>,
        /// Unit directory for a systemd snapshot artifact
        #[arg(long)]
        sys: Option<PathBuf>,
        /// Virtualenv root for an environment snapshot artifact
        #[arg(long)]
        env: Option<PathBuf>,
        /// Capture a live network socket report from procfs
        #[arg(long)]
        net: bool,
        /// Overwrite the output file if it already exists
        #[arg(long)]
        force: bool,
        /// Output format; default is the text summary
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Inspect contents and diagnostics of a .lens bundle
    Inspect {
        /// Path to the .lens bundle
        bundle: PathBuf,
        /// Output format; default is the text summary
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Verify bundle contents against its embedded manifest's SHA-256 checksums
    Verify {
        /// Path to the .lens bundle
        bundle: PathBuf,
        /// Output format; default is the text summary
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Print one archive entry's contents (e.g. reports/disk_snapshot.json)
    Show {
        /// Path to the .lens bundle
        bundle: PathBuf,
        /// Entry name as listed by `bundle inspect`
        entry: String,
        /// Output format; default is the raw entry bytes
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
    /// Verify, then extract a bundle into a directory (no overwrite without --force)
    Extract {
        /// Path to the .lens bundle
        bundle: PathBuf,
        /// Destination directory
        dest: PathBuf,
        /// Overwrite existing files
        #[arg(long)]
        force: bool,
        /// Output format; default is the text summary
        #[arg(long, value_enum)]
        format: Option<OutputFormat>,
    },
}
