use clap::Parser;
use lens_cli::cli::Cli;

fn main() -> std::process::ExitCode {
    let cli = Cli::parse();
    match lens_cli::ops::dispatch(cli.command) {
        Ok(()) => std::process::ExitCode::SUCCESS,
        Err(e) => {
            // Human-readable Display variant (Debug dumps raw io::Error structs).
            eprintln!("error: {e}");
            std::process::ExitCode::FAILURE
        }
    }
}
