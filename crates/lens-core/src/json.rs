use serde::Serialize;
use serde_json::Value;

use crate::error::{LensError, Result};

/// Recursively sort JSON object keys to guarantee deterministic byte serialization.
fn sort_json_keys(value: Value) -> Value {
    match value {
        Value::Object(map) => {
            let mut sorted: std::collections::BTreeMap<String, Value> =
                std::collections::BTreeMap::new();
            for (k, v) in map {
                sorted.insert(k, sort_json_keys(v));
            }
            Value::Object(sorted.into_iter().collect())
        }
        Value::Array(list) => Value::Array(list.into_iter().map(sort_json_keys).collect()),
        scalar => scalar,
    }
}

/// Serializes any Serializable data structure to a deterministic, sorted-key pretty JSON string.
pub fn to_deterministic_pretty<T: Serialize>(value: &T) -> Result<String> {
    let uncanonical = serde_json::to_value(value).map_err(LensError::Json)?;
    let canonical = sort_json_keys(uncanonical);
    serde_json::to_string_pretty(&canonical).map_err(LensError::Json)
}

/// Serializes any Serializable data structure to a deterministic compact JSON string.
pub fn to_deterministic_string<T: Serialize>(value: &T) -> Result<String> {
    let uncanonical = serde_json::to_value(value).map_err(LensError::Json)?;
    let canonical = sort_json_keys(uncanonical);
    serde_json::to_string(&canonical).map_err(LensError::Json)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    #[test]
    fn test_deterministic_key_ordering() {
        let mut map = HashMap::new();
        map.insert("z", 1);
        map.insert("a", 2);
        map.insert("m", 3);

        let json = to_deterministic_string(&map).unwrap();
        assert_eq!(json, r#"{"a":2,"m":3,"z":1}"#);
    }
}
