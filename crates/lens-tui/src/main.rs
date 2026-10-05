use clap::Parser;
use std::path::PathBuf;

#[derive(Parser)]
#[command(name = "lens-tui")]
#[command(about = "Interactive Terminal UI Dashboard for Lens Platform")]
struct Cli {
    /// Initial path to inspect
    #[arg(default_value = ".")]
    path: PathBuf,
}

fn main() -> std::io::Result<()> {
    let args = Cli::parse();
    lens_tui::run(&args.path)
}
