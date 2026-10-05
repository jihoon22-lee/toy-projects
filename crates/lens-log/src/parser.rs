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
            let mut fields = Vec::new();

            for (k, v) in map {
                // eq_ignore_ascii_case: no per-key lowercase allocation.
                if k.eq_ignore_ascii_case("level") || k.eq_ignore_ascii_case("severity") {
                    if let Some(s) = v.as_str() {
                        level = LogLevel::parse(s);
                    }
                } else if k.eq_ignore_ascii_case("msg") || k.eq_ignore_ascii_case("message") {
                    if let Some(s) = v.as_str() {
                        message = s.to_string();
                    }
                } else if k.eq_ignore_ascii_case("source")
                    || k.eq_ignore_ascii_case("logger")
                    || k.eq_ignore_ascii_case("component")
                {
                    if let Some(s) = v.as_str() {
                        source = s.to_string();
                    }
                } else if k.eq_ignore_ascii_case("ts")
                    || k.eq_ignore_ascii_case("time")
                    || k.eq_ignore_ascii_case("timestamp")
                {
                    if let Some(n) = v.as_u64() {
                        timestamp_ms = n;
                    }
                } else {
                    // Preserve unrecognized structured fields instead of
                    // dropping them silently.
                    let value = v
                        .as_str()
                        .map(|s| s.to_string())
                        .unwrap_or_else(|| v.to_string());
                    fields.push((k, value));
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
                fields,
            };
        }
    }

    // Standard log format heuristic: [LEVEL] or LEVEL
    let detected_level = detect_level(raw);

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

/// Scan only the first few words of `raw` for a level token.
/// Used by callers that need the level without building a record.
pub fn detect_level(raw: &str) -> LogLevel {
    let trimmed = raw.trim();
    for word in trimmed.split_whitespace().take(5) {
        let clean = word.trim_matches(|c| c == '[' || c == ']' || c == '(' || c == ')' || c == ':');
        let lvl = LogLevel::parse(clean);
        if lvl != LogLevel::Unknown {
            return lvl;
        }
    }
    LogLevel::Unknown
}
