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
