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

// --- Phase 1: accuracy ---

/// Build a minimal venv fixture: pyvenv.cfg + one dist-info per
/// (name, version, requires) tuple.
fn make_venv(tag: &str, dists: &[(&str, &str, &[&str])]) -> PathBuf {
    let dir = tmp_dir(tag);
    std::fs::write(
        dir.join("pyvenv.cfg"),
        "home = /usr/bin\nversion = 3.14.4\n",
    )
    .unwrap();
    let sp = dir.join("lib/python3.14/site-packages");
    std::fs::create_dir_all(&sp).unwrap();
    for (name, version, requires) in dists {
        let di = sp.join(format!("{}-{}.dist-info", name, version));
        std::fs::create_dir_all(&di).unwrap();
        let mut meta = format!(
            "Metadata-Version: 2.1\nName: {}\nVersion: {}\n",
            name, version
        );
        for r in *requires {
            meta.push_str(&format!("Requires-Dist: {}\n", r));
        }
        std::fs::write(di.join("METADATA"), meta).unwrap();
    }
    dir
}

#[test]
fn env_check_evaluates_markers_and_extras() {
    let venv = make_venv(
        "env-markers",
        &[
            (
                "app",
                "1.0",
                &[
                    // python_version is 3.14 -> marker false -> skipped.
                    "importlib-metadata>=3.6.0; python_version < '3.10'",
                    // extra == "testing" inactive without --extras.
                    "pytest>=9.0; extra == 'testing'",
                    // Present but out of range -> conflict, not missing.
                    "werkzeug<3",
                ],
            ),
            ("werkzeug", "3.1.9", &[]),
        ],
    );

    let out = lens(&["env", "check", venv.to_str().unwrap()]);
    assert!(out.status.success(), "{}", stderr_of(&out));
    let stdout = stdout_of(&out);
    assert!(!stdout.contains("missing dependenc"), "{stdout}");
    assert!(stdout.contains("Version conflicts"), "{stdout}");

    // With the extra activated the unsatisfied dep surfaces.
    let out = lens(&[
        "env",
        "check",
        "--extras",
        "testing",
        venv.to_str().unwrap(),
    ]);
    assert!(out.status.success(), "{}", stderr_of(&out));
    assert!(stdout_of(&out).contains("pytest"));
}

#[test]
fn log_filter_min_level_excludes_unknown_lines() {
    let log = tmp_file(
        "minlevel",
        "app.log",
        "2026-10-09 [ERROR] disk full\n\
         a plain line without any level\n\
         {\"level\":\"error\",\"msg\":\"gateway down\"}\n",
    );

    let out = lens(&[
        "log",
        "filter",
        log.to_str().unwrap(),
        "--min-level",
        "error",
    ]);
    assert!(out.status.success(), "{}", stderr_of(&out));
    let stdout = stdout_of(&out);
    assert!(stdout.contains("disk full"), "{stdout}");
    assert!(stdout.contains("gateway down"), "{stdout}");
    assert!(!stdout.contains("plain line"), "{stdout}");
    assert!(
        stderr_of(&out).contains("Excluded 1 line(s)"),
        "{}",
        stderr_of(&out)
    );

    let out = lens(&[
        "log",
        "filter",
        log.to_str().unwrap(),
        "--min-level",
        "error",
        "--include-unknown",
    ]);
    assert!(stdout_of(&out).contains("plain line"));
}

#[test]
fn trace_analyze_thread_shared_fds_and_errno_keys() {
    // F05 repro: a CLONE_FILES thread closes the shared fd 3, and fd 5
    // is O_CLOEXEC across execve — neither may appear as a leak.
    let trace = tmp_file(
        "f05",
        "t.strace",
        "100 openat(AT_FDCWD, \"/etc/hosts\", O_RDONLY) = 3\n\
         100 clone(child_stack=0x7f, flags=CLONE_VM|CLONE_FILES|CLONE_THREAD) = 101\n\
         101 close(3) = 0\n\
         101 +++ exited with 0 +++\n\
         100 openat(AT_FDCWD, \"/tmp/x\", O_RDONLY|O_CLOEXEC) = 5\n\
         100 execve(\"/bin/true\", [\"/bin/true\"], 0x55) = 0\n\
         100 openat(AT_FDCWD, \"/nope\", O_RDONLY) = -1 ENOENT (No such file or directory)\n\
         100 +++ exited with 0 +++\n",
    );

    let out = lens(&["trace", "analyze", trace.to_str().unwrap()]);
    assert!(out.status.success(), "{}", stderr_of(&out));
    let json: serde_json::Value =
        serde_json::from_str(&stdout_of(&out)).expect("analyze emits JSON");
    assert_eq!(json["fd_leaks"], serde_json::json!([]));
    assert!(
        json["fd_leaks_by_process"].is_null()
            || json["fd_leaks_by_process"].as_object().unwrap().is_empty(),
        "{}",
        json["fd_leaks_by_process"]
    );
    assert_eq!(json["errors"]["openat:ENOENT"], serde_json::json!(1));
}

#[test]
fn sys_cycles_report_directive_path_and_origin() {
    let dir = tmp_dir("sys-cycles");
    std::fs::write(
        dir.join("a.service"),
        "[Unit]\nBefore=b.service\nAfter=c.service\n[Service]\nExecStart=/bin/a\n",
    )
    .unwrap();
    std::fs::write(
        dir.join("b.service"),
        "[Unit]\nBefore=c.service\n[Service]\nExecStart=/bin/b\n",
    )
    .unwrap();
    std::fs::write(
        dir.join("c.service"),
        "[Unit]\n[Service]\nExecStart=/bin/c\n",
    )
    .unwrap();

    let out = lens(&["sys", "cycles", dir.to_str().unwrap()]);
    assert!(out.status.success(), "{}", stderr_of(&out));
    let stdout = stdout_of(&out);
    // Real directed path annotated with directive(file:line) origin.
    assert!(stdout.contains("--Before(a.service:2)-->"), "{stdout}");
    assert!(stdout.contains("--Before(b.service:2)-->"), "{stdout}");
    assert!(stdout.contains("--After(a.service:3)-->"), "{stdout}");
    // Not the old alphabetical SCC join.
    assert!(
        !stdout.contains("a.service -> b.service -> c.service"),
        "{stdout}"
    );
}

#[test]
fn sys_diff_accepts_directories_and_sees_all_keys() {
    let base = tmp_dir("sys-diff-base");
    let cand = tmp_dir("sys-diff-cand");
    std::fs::write(base.join("a.service"), "[Service]\nExecStart=/bin/a\n").unwrap();
    std::fs::write(
        cand.join("a.service"),
        "[Service]\nUser=svc\nExecStart=/bin/a\n",
    )
    .unwrap();

    let out = lens(&[
        "sys",
        "diff",
        base.to_str().unwrap(),
        cand.to_str().unwrap(),
    ]);
    assert!(out.status.success(), "{}", stderr_of(&out));
    let json: serde_json::Value = serde_json::from_str(&stdout_of(&out)).expect("diff JSON");
    let details = json["modified_units"][0]["details"]
        .as_str()
        .map(String::from);
    let details_json = json["modified_units"].to_string();
    assert!(
        details_json.contains("User"),
        "User= addition must appear in diff: {details_json}"
    );
    assert_eq!(json["modified_units"][0]["unit"], "a.service");
    let _ = details;
}

#[test]
fn abi_diff_import_only_change_is_compatible() {
    // Needs a C toolchain; self-skip when none is available.
    let dir = tmp_dir("abi-import");
    let src_v1 = dir.join("v1.c");
    let src_v2 = dir.join("v2.c");
    let so_v1 = dir.join("libx_v1.so");
    let so_v2 = dir.join("libx_v2.so");
    std::fs::write(
        &src_v1,
        "extern int helper_old(void);\n\
         __attribute__((weak)) int weak_fn(void) { return 1; }\n\
         int api_fn(void) { return helper_old(); }\n",
    )
    .unwrap();
    std::fs::write(
        &src_v2,
        "extern int helper_new(void);\n\
         __attribute__((weak)) int weak_fn(void) { return 1; }\n\
         int api_fn(void) { return helper_new(); }\n",
    )
    .unwrap();
    let ok = [(src_v1, so_v1.clone()), (src_v2, so_v2.clone())]
        .iter()
        .all(|(src, so)| {
            Command::new("cc")
                .args(["-shared", "-fPIC", "-o"])
                .arg(so)
                .arg(src)
                .status()
                .map(|s| s.success())
                .unwrap_or(false)
        });
    if !ok {
        return;
    }

    // Weak symbol shows as weak in inspect evidence.
    let out = lens(&["abi", "inspect", so_v1.to_str().unwrap()]);
    assert!(out.status.success(), "{}", stderr_of(&out));
    let inspect: serde_json::Value = serde_json::from_str(&stdout_of(&out)).expect("inspect JSON");
    let evidence = inspect["evidence"].as_array().unwrap();
    let weak = evidence
        .iter()
        .find(|s| s["identity"] == "weak_fn")
        .expect("weak_fn in evidence");
    assert_eq!(weak["binding"], "weak");
    // Imports are listed separately, not as exports.
    assert!(inspect["abi"]["imports"]
        .as_array()
        .unwrap()
        .iter()
        .any(|s| s == "helper_old"));
    assert!(!inspect["abi"]["symbols"]
        .as_array()
        .unwrap()
        .iter()
        .any(|s| s == "helper_old"));

    // An import-only change is compatible — it is not a removed export.
    let out = lens(&[
        "abi",
        "diff",
        so_v1.to_str().unwrap(),
        so_v2.to_str().unwrap(),
    ]);
    assert!(out.status.success(), "{}", stderr_of(&out));
    let diff: serde_json::Value = serde_json::from_str(&stdout_of(&out)).expect("diff JSON");
    assert_eq!(diff["compatibility"], "compatible", "{diff}");
    assert!(diff["compatible"].as_bool().unwrap());
    assert!(diff["imports"]["added"]
        .as_array()
        .unwrap()
        .iter()
        .any(|s| s == "helper_new"));
    assert!(diff["symbols"]["removed"].as_array().unwrap().is_empty());
}

#[test]
fn build_inspect_reports_transitive_impact() {
    let dir = tmp_dir("build-impact");
    let inc = dir.join("include");
    let src = dir.join("src");
    std::fs::create_dir_all(&inc).unwrap();
    std::fs::create_dir_all(&src).unwrap();
    std::fs::write(inc.join("leaf.h"), "#define L 1\n").unwrap();
    std::fs::write(src.join("mid.h"), "#include \"leaf.h\"\n").unwrap();
    std::fs::write(src.join("a.c"), "#include \"mid.h\"\n").unwrap();
    let cc_db = dir.join("compile_commands.json");
    std::fs::write(
        &cc_db,
        serde_json::to_string(&serde_json::json!([{
            "directory": dir,
            "file": src.join("a.c"),
            "arguments": ["cc", format!("-I{}", inc.display()), "-c", "a.c"],
        }]))
        .unwrap(),
    )
    .unwrap();

    let out = lens(&["build", "inspect", cc_db.to_str().unwrap()]);
    assert!(out.status.success(), "{}", stderr_of(&out));
    let json: serde_json::Value = serde_json::from_str(&stdout_of(&out)).expect("inspect JSON");
    let leaf = inc.join("leaf.h").to_string_lossy().into_owned();
    let a_c = src.join("a.c").to_string_lossy().into_owned();
    // Direct includers (reverse_impact) do not list a.c for leaf.h.
    assert!(json["reverse_impact"].get(&leaf).is_none());
    // transitive_impact does — a.c reaches leaf.h through mid.h.
    assert!(
        json["transitive_impact"][&leaf]
            .as_array()
            .unwrap()
            .iter()
            .any(|u| *u == a_c),
        "{}",
        json["transitive_impact"]
    );
}
