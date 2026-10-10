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
                        // journald-style numeric severity ("3" == err).
                        if level == LogLevel::Unknown {
                            if let Ok(n) = s.parse::<u32>() {
                                if n <= 7 {
                                    level = syslog_severity(n);
                                }
                            }
                        }
                    } else if let Some(n) = v.as_u64() {
                        if n <= 7 {
                            level = syslog_severity(n as u32);
                        }
                    }
                } else if k.eq_ignore_ascii_case("priority") {
                    // journald JSON export field — only a fallback when
                    // no textual level/severity was recognized.
                    if level == LogLevel::Unknown {
                        if let Some(n) = v
                            .as_u64()
                            .or_else(|| v.as_str().and_then(|s| s.parse::<u64>().ok()))
                        {
                            if n <= 7 {
                                level = syslog_severity(n as u32);
                            }
                        }
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
                    } else if let Some(n) = v.as_i64() {
                        timestamp_ms = n.max(0) as u64;
                    } else if let Some(n) = v.as_f64() {
                        timestamp_ms = n.max(0.0) as u64;
                    } else if let Some(s) = v.as_str() {
                        // ISO/RFC3339 string timestamp, same forms as
                        // --since/--until accept.
                        timestamp_ms = crate::timestamp::parse_bound(s)
                            .and_then(|t| u64::try_from(t).ok())
                            .unwrap_or(0);
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

    // ISO/RFC3339 or syslog (`Oct  9 …`, current UTC year) prefixes
    // yield a usable timestamp; anything else stays 0 = unknown.
    let timestamp_ms =
        crate::timestamp::extract_timestamp_ms(raw, crate::timestamp::default_syslog_year())
            .and_then(|t| u64::try_from(t).ok())
            .unwrap_or(0);

    LogRecordView {
        timestamp_ms,
        level: detected_level,
        source: Cow::Borrowed(""),
        message: Cow::Borrowed(raw),
        raw,
        line_number,
        fields: Vec::new(),
    }
}

/// Map a syslog severity (RFC 5424, 0=emerg .. 7=debug) to a `LogLevel`.
fn syslog_severity(sev: u32) -> LogLevel {
    match sev & 7 {
        // emerg, alert, crit
        0..=2 => LogLevel::Fatal,
        3 => LogLevel::Error,
        4 => LogLevel::Warn,
        // notice, info
        5 | 6 => LogLevel::Info,
        _ => LogLevel::Debug,
    }
}

/// A `<N>` token carrying a syslog PRI (`<13>`, RFC5424 `<13>1`) or a
/// kernel printk level (`<3>`). `prefix_form` allows a non-space suffix
/// (RFC5424 version digit or the message text fused to the marker).
fn syslog_priority_token(word: &str, prefix_form: bool, max: u32) -> Option<LogLevel> {
    let inner = word.strip_prefix('<')?;
    let end = inner.find('>')?;
    let digits = &inner[..end];
    if digits.is_empty() || digits.len() > 3 || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    if !prefix_form && end != inner.len() - 1 {
        return None;
    }
    let n: u32 = digits.parse().ok()?;
    if n > max {
        return None;
    }
    Some(syslog_severity(n))
}

/// Scan only the first few words of `raw` for a level token.
/// Used by callers that need the level without building a record.
pub fn detect_level(raw: &str) -> LogLevel {
    let trimmed = raw.trim();
    let mut after_tag = false;
    // The word-name scan keeps its historical 5-word bound; the
    // `<PRI>`-after-tag scan reaches a few words further because syslog
    // prefixes (`ts host kernel:`) can push the printk marker to ~word 5.
    for (i, word) in trimmed.split_whitespace().take(8).enumerate() {
        if i == 0 {
            // Syslog/RFC5424 line head: `<PRI>` / `<PRI>1`.
            if let Some(lvl) = syslog_priority_token(word, true, 191) {
                return lvl;
            }
        } else if after_tag {
            // printk level embedded at message start: `kernel: <3>msg`.
            // Restricted to 0-7 so hex dumps like `Code: <89> ...` are
            // not misread as levels.
            if let Some(lvl) = syslog_priority_token(word, true, 7) {
                return lvl;
            }
        }
        if i < 5 {
            // journald short/verbose exports carry PRIORITY=<0-7>.
            if let Some(rest) = word.strip_prefix("PRIORITY=") {
                if let Ok(n) = rest.parse::<u32>() {
                    if n <= 7 {
                        return syslog_severity(n);
                    }
                }
            }
            let clean =
                word.trim_matches(|c| c == '[' || c == ']' || c == '(' || c == ')' || c == ':');
            let lvl = LogLevel::parse(clean);
            if lvl != LogLevel::Unknown {
                return lvl;
            }
        }
        after_tag = word.ends_with(':');
    }
    LogLevel::Unknown
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_detect_level_textual() {
        assert_eq!(detect_level("2026-10-05 [ERROR] boom"), LogLevel::Error);
        assert_eq!(detect_level("plain line without level"), LogLevel::Unknown);
    }

    #[test]
    fn test_detect_level_syslog_pri() {
        // rsyslog PRI: facility*8 + severity. 131 = local0.err.
        assert_eq!(
            detect_level("<131>Oct  9 12:00:00 host app: failure"),
            LogLevel::Error
        );
        // RFC5424: <PRI>VERSION ...
        assert_eq!(
            detect_level("<13>1 2026-10-09T00:00:00Z host app - - - notice"),
            LogLevel::Info
        );
        assert_eq!(detect_level("<27>Oct  9 host app: oops"), LogLevel::Error);
    }

    #[test]
    fn test_detect_level_kernel_printk() {
        assert_eq!(
            detect_level("Oct  9 12:00:00 host kernel: <3>ata1.00: failed command"),
            LogLevel::Error
        );
        assert_eq!(
            detect_level("host kernel: <6>usb 1-1: new device"),
            LogLevel::Info
        );
        // Hex dump markers are not levels.
        assert_eq!(
            detect_level("host kernel: Code: a2 fe <89> c2 3d 00"),
            LogLevel::Unknown
        );
        // kern.log-style lines carry no level token.
        assert_eq!(
            detect_level("2026-10-04T01:09:12.800201+09:00 Main kernel: Command line: initrd"),
            LogLevel::Unknown
        );
    }

    #[test]
    fn test_detect_level_journald_priority() {
        assert_eq!(detect_level("PRIORITY=3"), LogLevel::Error);
        let rec = parse_line(
            r#"{"PRIORITY":"4","MESSAGE":"degraded","_HOSTNAME":"h"}"#,
            1,
        );
        assert_eq!(rec.level, LogLevel::Warn);
        let rec2 = parse_line(r#"{"severity":"2","msg":"crit"}"#, 2);
        assert_eq!(rec2.level, LogLevel::Fatal);
    }
}
