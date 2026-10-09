use std::io::Write;

/// Write one formatted line to stdout. A downstream reader that closed the
/// pipe early (`| head`, `| grep -m1`, quitting `less`) is a clean exit, not
/// a panic on `BrokenPipe` like `println!`.
pub fn line(args: std::fmt::Arguments<'_>) {
    let stdout = std::io::stdout();
    let mut out = stdout.lock();
    let result = out
        .write_fmt(args)
        .and_then(|()| out.write_all(b"\n"))
        .and_then(|()| out.flush());
    match result {
        Ok(()) => {}
        Err(e) if e.kind() == std::io::ErrorKind::BrokenPipe => std::process::exit(0),
        Err(e) => {
            drop(out);
            eprintln!("error: failed to write to stdout: {e}");
            std::process::exit(1);
        }
    }
}

macro_rules! outln {
    () => {
        $crate::output::line(format_args!(""))
    };
    ($($arg:tt)*) => {
        $crate::output::line(format_args!($($arg)*))
    };
}

pub(crate) use outln;
