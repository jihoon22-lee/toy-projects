use serde::{Deserialize, Serialize};
use serde_json::Value;

#[derive(Debug, Serialize, Deserialize)]
pub struct JsonRpcRequest {
    pub jsonrpc: String,
    pub id: Option<Value>,
    pub method: String,
    #[serde(default)]
    pub params: Option<Value>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct JsonRpcResponse {
    pub jsonrpc: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub id: Option<Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub result: Option<Value>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub error: Option<JsonRpcError>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct JsonRpcError {
    pub code: i64,
    pub message: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub data: Option<Value>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct McpTool {
    pub name: String,
    pub description: String,
    #[serde(rename = "inputSchema")]
    pub input_schema: Value,
}

pub fn list_tools() -> Vec<McpTool> {
    vec![
        McpTool {
            name: "lens_disk_scan".to_string(),
            description: "Scan filesystem directory and return logical/allocated storage usage."
                .to_string(),
            input_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "path": { "type": "string", "description": "Directory path to scan" },
                    "json": { "type": "boolean", "description": "Return full snapshot JSON" }
                },
                "required": ["path"]
            }),
        },
        McpTool {
            name: "lens_disk_duplicates".to_string(),
            description: "Find duplicate files based on SHA-256 hash, ignoring hardlink inodes."
                .to_string(),
            input_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "path": { "type": "string", "description": "Directory path to scan" },
                    "min_size": { "type": "integer", "description": "Minimum file size in bytes" }
                },
                "required": ["path"]
            }),
        },
        McpTool {
            name: "lens_abi_inspect".to_string(),
            description: "Inspect ELF binary dynamic symbols, version definitions, and DWARF info."
                .to_string(),
            input_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "binary": { "type": "string", "description": "Path to ELF binary" }
                },
                "required": ["binary"]
            }),
        },
        McpTool {
            name: "lens_log_filter".to_string(),
            description: "Filter high-volume log files by substring query and minimum log level."
                .to_string(),
            input_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "path": { "type": "string", "description": "Path to log file" },
                    "query": { "type": "string", "description": "Substring query" },
                    "min_level": { "type": "string", "description": "Minimum level (debug, info, warn, error)" }
                },
                "required": ["path"]
            }),
        },
        McpTool {
            name: "lens_trace_analyze".to_string(),
            description:
                "Analyze raw strace log and extract syscall metrics, latencies, and errors."
                    .to_string(),
            input_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "trace_file": { "type": "string", "description": "Path to strace log file" }
                },
                "required": ["trace_file"]
            }),
        },
        McpTool {
            name: "lens_sys_cycles".to_string(),
            description: "Detect dependency ordering cycles among systemd service units."
                .to_string(),
            input_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "dir": { "type": "string", "description": "Directory containing systemd unit files" }
                },
                "required": ["dir"]
            }),
        },
        McpTool {
            name: "lens_build_impact".to_string(),
            description:
                "Calculate transitive reverse compilation impact of changing a header file."
                    .to_string(),
            input_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "compile_commands": { "type": "string", "description": "Path to compile_commands.json" },
                    "header": { "type": "string", "description": "Target header file to inspect" }
                },
                "required": ["compile_commands", "header"]
            }),
        },
        McpTool {
            name: "lens_env_check".to_string(),
            description:
                "Check Python virtual environment for missing or unsatisfied dependencies."
                    .to_string(),
            input_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "venv_path": { "type": "string", "description": "Path to Python virtualenv root" }
                },
                "required": ["venv_path"]
            }),
        },
        McpTool {
            name: "lens_net_inspect".to_string(),
            description:
                "Inspect Linux network sockets, listening ports, established connections, and map them to processes."
                    .to_string(),
            input_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "proc_dir": { "type": "string", "description": "Optional proc filesystem root (default /proc)" }
                }
            }),
        },
        McpTool {
            name: "lens_bundle_verify".to_string(),
            description:
                "Verify a .lens forensic archive's contents against the SHA-256 checksums in its embedded manifest."
                    .to_string(),
            input_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "bundle_path": { "type": "string", "description": "Path to .lens (.tar.gz) forensic bundle" }
                },
                "required": ["bundle_path"]
            }),
        },
        McpTool {
            name: "lens_doctor".to_string(),
            description:
                "Run a comprehensive health check across storage capacity, open network ports, systemd cycles, and environment security."
                    .to_string(),
            input_schema: serde_json::json!({
                "type": "object",
                "properties": {
                    "root_path": { "type": "string", "description": "Optional root filesystem path (default /)" },
                    "proc_dir": { "type": "string", "description": "Optional procfs path (default /proc)" },
                    "systemd_dir": { "type": "string", "description": "Optional systemd unit dir (default /etc/systemd/system)" }
                }
            }),
        },
    ]
}
