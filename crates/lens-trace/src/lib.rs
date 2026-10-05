pub mod diff;
pub mod model;
pub mod parser;

pub use diff::diff_snapshots;
pub use model::*;
pub use parser::{parse_strace_line, TraceAnalyzer};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_single_line() {
        let line =
            "1234  12:00:00.123456 openat(AT_FDCWD, \"/etc/passwd\", O_RDONLY) = 3 <0.000045>";
        let ev = parse_strace_line(line).expect("Failed to parse strace line");
        assert_eq!(ev.tid, 1234);
        assert_eq!(ev.syscall, "openat");
        assert_eq!(ev.arguments, "AT_FDCWD, \"/etc/passwd\", O_RDONLY");
        assert_eq!(ev.result, "3");
        assert_eq!(ev.error, None);
        assert_eq!(ev.duration_ns, Some(45_000));
    }

    #[test]
    fn test_parse_error_line() {
        let line = "5678  openat(AT_FDCWD, \"/nonexistent\", O_RDONLY) = -1 ENOENT (No such file or directory) <0.000010>";
        let ev = parse_strace_line(line).expect("Failed to parse strace line");
        assert_eq!(ev.tid, 5678);
        assert_eq!(ev.syscall, "openat");
        assert_eq!(ev.error, Some("ENOENT".to_string()));
        assert_eq!(ev.duration_ns, Some(10_000));
    }

    #[test]
    fn test_parse_mixed_fixture() {
        let fixture = include_str!("../fixtures/mixed.strace");
        let analyzer = TraceAnalyzer::new();
        let snap = analyzer.analyze_lines(fixture.lines());

        assert!(snap.total_calls >= 5);
        assert_eq!(snap.total_errors, 1);
        assert_eq!(snap.errors.get("ENOENT"), Some(&1));
        // read should be stitched properly from unfinished and resumed
        assert!(snap.syscalls.contains_key("read"));
    }

    #[test]
    fn test_trace_diff() {
        let sample1 = r#"
100 openat(AT_FDCWD, "file1", 0) = 3 <0.000010>
100 read(3, "buf", 1024) = 1024 <0.000010>
100 close(3) = 0 <0.000005>
"#;
        let sample2 = r#"
100 openat(AT_FDCWD, "file1", 0) = -1 ENOENT (No such file) <0.000020>
100 write(2, "error", 5) = 5 <0.000010>
"#;

        let analyzer = TraceAnalyzer::new();
        let snap1 = analyzer.analyze_lines(sample1.lines());
        let snap2 = analyzer.analyze_lines(sample2.lines());

        assert_eq!(snap1.total_calls, 3);
        assert_eq!(snap1.total_errors, 0);

        assert_eq!(snap2.total_calls, 2);
        assert_eq!(snap2.total_errors, 1);

        let diff = diff_snapshots(&snap1, &snap2);
        assert_eq!(diff.call_delta, -1);
        assert_eq!(diff.error_delta, 1);
        assert_eq!(diff.new_errors, vec!["ENOENT"]);
        assert!(diff.resolved_errors.is_empty());
    }
}
