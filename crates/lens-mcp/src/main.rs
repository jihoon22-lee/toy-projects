use serde_json::Value;
use std::io::{self, BufRead, Write};

use lens_mcp::{execute_tool, list_tools, JsonRpcError, JsonRpcRequest, JsonRpcResponse};

fn main() -> io::Result<()> {
    let stdin = io::stdin();
    let mut stdout = io::stdout();

    for line in stdin.lock().lines() {
        let line = match line {
            Ok(l) => l,
            Err(_) => break,
        };

        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }

        let req: JsonRpcRequest = match serde_json::from_str(trimmed) {
            Ok(r) => r,
            Err(e) => {
                let err_resp = JsonRpcResponse {
                    jsonrpc: "2.0".to_string(),
                    id: None,
                    result: None,
                    error: Some(JsonRpcError {
                        code: -32700,
                        message: format!("Parse error: {}", e),
                        data: None,
                    }),
                };
                let out = serde_json::to_string(&err_resp)?;
                writeln!(stdout, "{}", out)?;
                stdout.flush()?;
                continue;
            }
        };

        let resp = handle_request(&req);
        if let Some(r) = resp {
            let out = serde_json::to_string(&r)?;
            writeln!(stdout, "{}", out)?;
            stdout.flush()?;
        }
    }

    Ok(())
}

fn handle_request(req: &JsonRpcRequest) -> Option<JsonRpcResponse> {
    match req.method.as_str() {
        "initialize" => Some(JsonRpcResponse {
            jsonrpc: "2.0".to_string(),
            id: req.id.clone(),
            result: Some(serde_json::json!({
                "protocolVersion": "2024-11-05",
                "capabilities": {
                    "tools": {}
                },
                "serverInfo": {
                    "name": "lens-mcp",
                    "version": env!("CARGO_PKG_VERSION")
                }
            })),
            error: None,
        }),
        "notifications/initialized" => None,
        "tools/list" => {
            let tools = list_tools();
            Some(JsonRpcResponse {
                jsonrpc: "2.0".to_string(),
                id: req.id.clone(),
                result: Some(serde_json::json!({
                    "tools": tools
                })),
                error: None,
            })
        }
        "tools/call" => {
            let params = req.params.as_ref().unwrap_or(&Value::Null);
            let name = params.get("name").and_then(|v| v.as_str()).unwrap_or("");
            let args = params.get("arguments").unwrap_or(&Value::Null);

            match execute_tool(name, args) {
                Ok(text) => Some(JsonRpcResponse {
                    jsonrpc: "2.0".to_string(),
                    id: req.id.clone(),
                    result: Some(serde_json::json!({
                        "content": [
                            {
                                "type": "text",
                                "text": text
                            }
                        ]
                    })),
                    error: None,
                }),
                Err(err) => Some(JsonRpcResponse {
                    jsonrpc: "2.0".to_string(),
                    id: req.id.clone(),
                    result: None,
                    error: Some(JsonRpcError {
                        code: -32000,
                        message: err,
                        data: None,
                    }),
                }),
            }
        }
        _ => req.id.as_ref().map(|id| JsonRpcResponse {
            jsonrpc: "2.0".to_string(),
            id: Some(id.clone()),
            result: None,
            error: Some(JsonRpcError {
                code: -32601,
                message: format!("Method not found: {}", req.method),
                data: None,
            }),
        }),
    }
}
