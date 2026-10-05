use crate::record::{LogLevel, LogRecordView};
use std::borrow::Cow;

pub fn parse_line<'a>(raw: &'a str, line_number: usize) -> LogRecordView<'a> {
    let trimmed = raw.trim();

    // Check JSONL format
    if trimmed.starts_with('{') && trimmed.ends_with('}') {
        if let Ok(serde_json::Value::Object(map)) =
            serde_json::from_str::<serde_json::Value>(trimmed)
        {
            let mut level = LogLevel::Unknown;
            let mut message = String::new();
            let mut source = String::new();
            let mut timestamp_ms = 0;

            for (k, v) in map {
                let k_lower = k.to_lowercase();
                if k_lower == "level" || k_lower == "severity" {
                    if let Some(s) = v.as_str() {
                        level = LogLevel::parse(s);
                    }
                } else if k_lower == "msg" || k_lower == "message" {
                    if let Some(s) = v.as_str() {
                        message = s.to_string();
                    }
                } else if k_lower == "source" || k_lower == "logger" || k_lower == "component" {
                    if let Some(s) = v.as_str() {
                        source = s.to_string();
                    }
                } else if k_lower == "ts" || k_lower == "time" || k_lower == "timestamp" {
                    if let Some(n) = v.as_u64() {
                        timestamp_ms = n;
                    }
                }
            }

            let final_message = if message.is_empty() {
                Cow::Borrowed(raw)
            } else {
                Cow::Owned(message)
            };

            return LogRecordView {
                timestamp_ms,
                level,
                source: Cow::Owned(source),
                message: final_message,
                raw,
                line_number,
                fields: Vec::new(),
            };
        }
    }

    // Standard log format heuristic: [LEVEL] or LEVEL
    let mut detected_level = LogLevel::Unknown;
    let words: Vec<&'a str> = trimmed.split_whitespace().collect();

    for &word in words.iter().take(5) {
        let clean = word.trim_matches(|c| c == '[' || c == ']' || c == '(' || c == ')' || c == ':');
        let lvl = LogLevel::parse(clean);
        if lvl != LogLevel::Unknown {
            detected_level = lvl;
            break;
        }
    }

    LogRecordView {
        timestamp_ms: 0,
        level: detected_level,
        source: Cow::Borrowed(""),
        message: Cow::Borrowed(raw),
        raw,
        line_number,
        fields: Vec::new(),
    }
}
