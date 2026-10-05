use clap::Parser;
use lens_cli::cli::Cli;
use lens_core::Result;

fn main() -> Result<()> {
    let cli = Cli::parse();
    lens_cli::ops::dispatch(cli.command)
}
