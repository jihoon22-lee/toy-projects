use serde_json::Value;
use std::io::{Read, Write};
use std::process::{Command, Stdio};

/// Feed newline-delimited JSON-RPC requests to lens-mcp and parse the
/// response lines. The server exits when stdin reaches EOF.
fn roundtrip(requests: &[&str]) -> Vec<Value> {
    let mut child = Command::new(env!("CARGO_BIN_EXE_lens-mcp"))
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .expect("spawn lens-mcp");

    {
        let mut stdin = child.stdin.take().unwrap();
        for req in requests {
            writeln!(stdin, "{req}").unwrap();
        }
    }

    let mut stdout = String::new();
    child
        .stdout
        .take()
        .unwrap()
        .read_to_string(&mut stdout)
        .unwrap();
    let status = child.wait().expect("wait");
    assert!(status.success());

    stdout
        .lines()
        .filter(|l| !l.trim().is_empty())
        .map(|l| serde_json::from_str(l).expect("response is JSON"))
        .collect()
}

#[test]
fn ping_returns_empty_result() {
    let responses = roundtrip(&[r#"{"jsonrpc":"2.0","id":1,"method":"ping"}"#]);
    assert_eq!(responses.len(), 1);
    assert_eq!(responses[0]["result"], serde_json::json!({}));
    assert!(responses[0].get("error").is_none());
}

#[test]
fn tool_failure_is_an_iserror_result() {
    // lens_disk_scan without the required "path" argument fails inside the
    // tool — the failure must come back as result.isError, not a JSON-RPC
    // error, so the model can read and react to it.
    let responses = roundtrip(&[
        r#"{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"lens_disk_scan","arguments":{}}}"#,
    ]);
    assert_eq!(responses.len(), 1);
    let resp = &responses[0];
    assert!(resp.get("error").is_none(), "{resp}");
    assert_eq!(resp["result"]["isError"], Value::Bool(true));
    assert_eq!(resp["result"]["content"][0]["type"], "text");
    assert!(resp["result"]["content"][0]["text"]
        .as_str()
        .unwrap()
        .contains("path"));
}

#[test]
fn unknown_tool_stays_a_jsonrpc_error() {
    let responses = roundtrip(&[
        r#"{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"lens_nope","arguments":{}}}"#,
    ]);
    assert_eq!(responses.len(), 1);
    assert_eq!(responses[0]["error"]["code"], Value::from(-32602));
    assert!(responses[0].get("result").is_none());
}

#[test]
fn unknown_method_and_parse_errors_stay_jsonrpc_errors() {
    let responses = roundtrip(&[
        r#"{"jsonrpc":"2.0","id":4,"method":"bogus/method"}"#,
        "this is not json",
    ]);
    assert_eq!(responses.len(), 2);
    assert_eq!(responses[0]["error"]["code"], Value::from(-32601));
    assert_eq!(responses[1]["error"]["code"], Value::from(-32700));
}

#[test]
fn successful_tool_call_returns_content() {
    let responses = roundtrip(&[
        r#"{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"lens_disk_scan","arguments":{"path":"/tmp"}}}"#,
    ]);
    assert_eq!(responses.len(), 1);
    let resp = &responses[0];
    assert!(resp.get("error").is_none(), "{resp}");
    assert!(resp["result"]["content"][0]["text"].is_string());
    assert!(resp["result"].get("isError").is_none());
}

fn tool_call(id: u64, name: &str, args: Value) -> String {
    let call = format!(
        r#"{{"jsonrpc":"2.0","id":{},"method":"tools/call","params":{{"name":"{}","arguments":{}}}}}"#,
        id, name, args
    );
    let responses = roundtrip(&[&call]);
    assert_eq!(responses.len(), 1);
    let resp = &responses[0];
    assert!(resp.get("error").is_none(), "{resp}");
    resp["result"]["content"][0]["text"]
        .as_str()
        .expect("text content")
        .to_string()
}

fn tool_result(id: u64, name: &str, args: Value) -> Value {
    let call = format!(
        r#"{{"jsonrpc":"2.0","id":{},"method":"tools/call","params":{{"name":"{}","arguments":{}}}}}"#,
        id, name, args
    );
    let responses = roundtrip(&[&call]);
    assert_eq!(responses.len(), 1);
    responses[0]["result"].clone()
}

fn tmpdir(tag: &str) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!("lensmcp-{}-{}", tag, std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

#[test]
fn initialize_echoes_client_protocol_version() {
    let responses = roundtrip(&[
        r#"{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"t","version":"0"}}}"#,
    ]);
    assert_eq!(responses[0]["result"]["protocolVersion"], "2025-03-26");

    // No version in the request → server baseline.
    let responses = roundtrip(&[r#"{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}"#]);
    assert_eq!(responses[0]["result"]["protocolVersion"], "2024-11-05");
}

#[test]
fn log_filter_tail_reaches_end_of_file() {
    // MARKER sits past the old first-1000-lines scan bound — only a tail
    // scan finds it.
    let dir = tmpdir("logtail");
    let log = dir.join("big.log");
    let mut content = String::new();
    for i in 0..5000 {
        content.push_str(&format!("INFO line {}\n", i));
    }
    content.push_str("ERROR MARKER_AT_TAIL\n");
    std::fs::write(&log, content).unwrap();

    let text = tool_call(
        10,
        "lens_log_filter",
        serde_json::json!({
            "path": log.to_string_lossy(),
            "query": "MARKER_AT_TAIL",
            "tail": 100
        }),
    );
    let v: Value = serde_json::from_str(&text).unwrap();
    assert_eq!(v["matches"].as_array().unwrap().len(), 1, "{text}");
    assert_eq!(v["matches"][0]["line"], 5001);
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn log_filter_limit_paginates_matches() {
    let dir = tmpdir("loglim");
    let log = dir.join("m.log");
    let content: String = (0..50).map(|i| format!("ERROR e{}\n", i)).collect();
    std::fs::write(&log, content).unwrap();

    let text = tool_call(
        11,
        "lens_log_filter",
        serde_json::json!({
            "path": log.to_string_lossy(),
            "query": "ERROR",
            "limit": 10,
            "offset": 5
        }),
    );
    let v: Value = serde_json::from_str(&text).unwrap();
    let matches = v["matches"].as_array().unwrap();
    assert_eq!(matches.len(), 10, "{text}");
    // offset 5 → first emitted match is the 6th ERROR (line 6).
    assert_eq!(matches[0]["line"], 6);
    assert_eq!(v["truncated"], true);
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn tool_results_paginate_arrays() {
    // lens_doctor's checks array is capped by `limit` and carries a
    // structured truncation marker.
    let text = tool_call(
        12,
        "lens_doctor",
        serde_json::json!({ "root_path": "/", "limit": 1 }),
    );
    let v: Value = serde_json::from_str(&text).unwrap();
    let checks = v["checks"].as_array().unwrap();
    let marker = checks
        .iter()
        .find(|c| c["_truncated"] == true)
        .unwrap_or_else(|| panic!("no truncation marker: {text}"));
    assert!(marker["omitted"].as_u64().unwrap() > 0);
}

#[test]
fn doctor_bad_override_path_is_iserror() {
    let result = tool_result(
        13,
        "lens_doctor",
        serde_json::json!({ "systemd_dir": "/nonexistent-dir-for-mcp-test" }),
    );
    assert_eq!(result["isError"], Value::Bool(true));
    assert!(result["content"][0]["text"]
        .as_str()
        .unwrap()
        .contains("systemd_dir"));
}

#[test]
fn sys_diff_between_unit_dirs() {
    let base = tmpdir("sysb");
    let cand = tmpdir("sysc");
    let unit = "[Service]\nExecStart=/bin/a\n";
    std::fs::write(base.join("a.service"), unit).unwrap();
    std::fs::write(
        cand.join("a.service"),
        "[Service]\nUser=svc\nExecStart=/bin/a\n",
    )
    .unwrap();

    let text = tool_call(
        14,
        "lens_sys_diff",
        serde_json::json!({
            "baseline": base.to_string_lossy(),
            "candidate": cand.to_string_lossy()
        }),
    );
    let v: Value = serde_json::from_str(&text).unwrap();
    assert_eq!(v["modified_units"][0]["unit"], "a.service", "{text}");
    let _ = std::fs::remove_dir_all(&base);
    let _ = std::fs::remove_dir_all(&cand);
}

#[test]
fn test_diff_reports_regressions() {
    let dir = tmpdir("tdiff");
    std::fs::write(
        dir.join("base.xml"),
        r#"<testsuite name="s"><testcase name="a" time="0.1"/></testsuite>"#,
    )
    .unwrap();
    std::fs::write(
        dir.join("cand.xml"),
        r#"<testsuite name="s"><testcase name="a" time="0.1"><failure>oops</failure></testcase></testsuite>"#,
    )
    .unwrap();

    let text = tool_call(
        15,
        "lens_test_diff",
        serde_json::json!({
            "baseline": dir.join("base.xml").to_string_lossy(),
            "candidate": dir.join("cand.xml").to_string_lossy()
        }),
    );
    let v: Value = serde_json::from_str(&text).unwrap();
    assert_eq!(v["regressions"].as_array().unwrap().len(), 1, "{text}");
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn net_diff_between_snapshots() {
    let dir = tmpdir("ndiff");
    let sock = |port: u16| {
        format!(
            r#"{{"kind":"tcp","local_address":"127.0.0.1","local_port":{},"remote_address":"","remote_port":0,"state":"LISTEN","inode":{},"uid":0,"tx_queue":0,"rx_queue":0,"process":null}}"#,
            port, port
        )
    };
    let snap = |listeners: String| {
        format!(
            r#"{{"schema_version":"lens.net/v1","summary":{{"total_sockets":0,"listening_ports":0,"established_connections":0,"time_wait_sockets":0,"orphan_sockets":0,"unix_domain_sockets":0}},"sockets":[],"listening":[{}]}}"#,
            listeners
        )
    };
    let b = dir.join("b.json");
    let c = dir.join("c.json");
    std::fs::write(&b, snap(sock(80))).unwrap();
    std::fs::write(&c, snap(format!("{},{}", sock(80), sock(443)))).unwrap();

    let text = tool_call(
        16,
        "lens_net_diff",
        serde_json::json!({
            "baseline": b.to_string_lossy(),
            "candidate": c.to_string_lossy()
        }),
    );
    let v: Value = serde_json::from_str(&text).unwrap();
    assert_eq!(v["new_listeners"][0]["local_port"], 443, "{text}");
    let _ = std::fs::remove_dir_all(&dir);
}

#[test]
fn abi_diff_identical_binary_is_compatible() {
    // The lens-mcp binary itself is an ELF — diffing it against itself is
    // trivially compatible.
    let exe = env!("CARGO_BIN_EXE_lens-mcp");
    let text = tool_call(
        17,
        "lens_abi_diff",
        serde_json::json!({ "baseline": exe, "candidate": exe }),
    );
    let v: Value = serde_json::from_str(&text).unwrap();
    assert_eq!(v["compatibility"], "compatible", "{text}");
}
