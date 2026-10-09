use std::io::Read;
use std::path::PathBuf;
use std::process::{Command, Output, Stdio};

fn lens(args: &[&str]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_lens"))
        .args(args)
        .output()
        .expect("failed to spawn lens")
}

fn tmp_dir(tag: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("lens-cli-test-{}-{}", std::process::id(), tag));
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

fn tmp_file(tag: &str, name: &str, contents: &str) -> PathBuf {
    let dir = tmp_dir(tag);
    let path = dir.join(name);
    std::fs::write(&path, contents).unwrap();
    path
}

fn stderr_of(output: &Output) -> String {
    String::from_utf8_lossy(&output.stderr).into_owned()
}

fn stdout_of(output: &Output) -> String {
    String::from_utf8_lossy(&output.stdout).into_owned()
}

const JUNIT_XML: &str =
    r#"<testsuite tests="1"><testcase name="a" classname="C" time="0"/></testsuite>"#;

#[test]
fn stdout_pipe_closed_early_exits_cleanly() {
    // `lens net inspect --json | head -1`: the reader goes away mid-write.
    let mut child = Command::new(env!("CARGO_BIN_EXE_lens"))
        .args(["net", "inspect", "--json"])
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("spawn lens");

    let mut stdout = child.stdout.take().unwrap();
    let mut buf = [0u8; 256];
    let _ = stdout.read(&mut buf);
    drop(stdout);

    let status = child.wait().expect("wait");
    assert!(
        status.success(),
        "lens should exit 0 on a closed pipe, got {status:?}"
    );
}

#[test]
fn env_check_rejects_nonexistent_venv() {
    let out = lens(&["env", "check", "/nonexistent-venv-lens-test"]);
    assert!(!out.status.success());
    assert!(
        stderr_of(&out).contains("virtualenv"),
        "{}",
        stderr_of(&out)
    );
}

#[test]
fn env_check_rejects_non_venv_dir() {
    let dir = tmp_dir("notvenv");
    let out = lens(&["env", "check", dir.to_str().unwrap()]);
    assert!(!out.status.success());
    assert!(
        stderr_of(&out).contains("virtualenv"),
        "{}",
        stderr_of(&out)
    );
}

#[test]
fn net_inspect_rejects_nonexistent_proc_dir() {
    let out = lens(&["net", "inspect", "--proc-dir", "/nonexistent-proc"]);
    assert!(!out.status.success());
    assert!(!stderr_of(&out).is_empty());
}

#[test]
fn doctor_rejects_nonexistent_override_dirs() {
    let out = lens(&[
        "doctor",
        "--systemd-dir",
        "/nonexistent-systemd",
        "--procfs",
        "/nonexistent-proc",
    ]);
    assert!(!out.status.success());
    assert!(
        stderr_of(&out).contains("does not exist"),
        "{}",
        stderr_of(&out)
    );

    let out = lens(&["doctor", "--procfs", "/nonexistent-proc"]);
    assert!(!out.status.success());
    assert!(stderr_of(&out).contains("--procfs"), "{}", stderr_of(&out));
}

#[test]
fn test_parse_rejects_non_xml() {
    let file = tmp_file("junit", "report.xml", "not xml\n");
    let out = lens(&["test", "parse", file.to_str().unwrap()]);
    assert!(!out.status.success());
    assert!(stderr_of(&out).contains("JUnit"), "{}", stderr_of(&out));
}

#[test]
fn test_parse_accepts_junit_xml() {
    let file = tmp_file("junit-ok", "report.xml", JUNIT_XML);
    let out = lens(&["test", "parse", file.to_str().unwrap()]);
    assert!(out.status.success(), "{}", stderr_of(&out));
}

#[test]
fn trace_analyze_rejects_non_strace() {
    let file = tmp_file(
        "trace",
        "notatrace.txt",
        "the quick brown fox\njumps over the lazy dog\npack my box with five dozen liquor jugs\n",
    );
    let out = lens(&["trace", "analyze", file.to_str().unwrap()]);
    assert!(!out.status.success());
    assert!(stderr_of(&out).contains("strace"), "{}", stderr_of(&out));
}

#[test]
fn bundle_create_isolates_source_failures() {
    let junit = tmp_file("bundle", "junit.xml", JUNIT_XML);
    let output = tmp_dir("bundle").join("evidence.lens");

    let out = lens(&[
        "bundle",
        "create",
        output.to_str().unwrap(),
        "--test",
        junit.to_str().unwrap(),
        "--trace",
        "/nonexistent-trace",
    ]);
    assert!(
        out.status.success(),
        "bundle should be created despite the failing trace source: {}",
        stderr_of(&out)
    );
    assert!(output.exists());

    // The trace failure is preserved as a manifest diagnostic.
    let inspect = lens(&["bundle", "inspect", output.to_str().unwrap()]);
    assert!(inspect.status.success());
    let stdout = stdout_of(&inspect);
    assert!(stdout.contains("trace"), "{stdout}");

    // Refuse to overwrite without --force.
    let out = lens(&[
        "bundle",
        "create",
        output.to_str().unwrap(),
        "--test",
        junit.to_str().unwrap(),
    ]);
    assert!(!out.status.success());
    assert!(stderr_of(&out).contains("--force"), "{}", stderr_of(&out));

    // --force allows the overwrite.
    let out = lens(&[
        "bundle",
        "create",
        output.to_str().unwrap(),
        "--test",
        junit.to_str().unwrap(),
        "--force",
    ]);
    assert!(out.status.success(), "{}", stderr_of(&out));
}

#[cfg(unix)]
#[test]
fn disk_scan_reports_incomplete_on_unreadable_dirs() {
    use std::os::unix::fs::PermissionsExt;

    let dir = tmp_dir("scan");
    let locked = dir.join("locked");
    std::fs::create_dir(&locked).unwrap();
    std::fs::set_permissions(&locked, std::fs::Permissions::from_mode(0o000)).unwrap();

    let out = lens(&["disk", "scan", dir.to_str().unwrap()]);
    assert!(out.status.success(), "{}", stderr_of(&out));

    // When the filesystem honors the permission drop, the scan records an
    // error and the banner must admit the result is incomplete. (Running as
    // root bypasses the chmod — nothing to assert then.)
    if stderr_of(&out).contains("scan warning") {
        assert!(
            stdout_of(&out).contains("INCOMPLETE"),
            "{}",
            stdout_of(&out)
        );
    }

    std::fs::set_permissions(&locked, std::fs::Permissions::from_mode(0o755)).unwrap();
}
