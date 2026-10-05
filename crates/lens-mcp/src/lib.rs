pub mod handler;
pub mod protocol;

pub use handler::execute_tool;
pub use protocol::{list_tools, JsonRpcError, JsonRpcRequest, JsonRpcResponse, McpTool};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_list_tools() {
        let tools = list_tools();
        assert!(!tools.is_empty());
        assert!(tools.iter().any(|t| t.name == "lens_disk_scan"));
        assert!(tools.iter().any(|t| t.name == "lens_trace_analyze"));
    }

    #[test]
    fn test_execute_unknown_tool() {
        let res = execute_tool("nonexistent", &serde_json::json!({}));
        assert!(res.is_err());
    }
}
