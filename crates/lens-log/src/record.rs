use serde::{Deserialize, Serialize};
use std::borrow::Cow;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    Unknown,
}

impl LogLevel {
    pub fn parse(s: &str) -> Self {
        // Allocation-free: eq_ignore_ascii_case avoids the per-line String
        // that to_uppercase() would allocate.
        if s.eq_ignore_ascii_case("trace") {
            Self::Trace
        } else if s.eq_ignore_ascii_case("debug") {
            Self::Debug
        } else if s.eq_ignore_ascii_case("info") {
            Self::Info
        } else if s.eq_ignore_ascii_case("warn") || s.eq_ignore_ascii_case("warning") {
            Self::Warn
        } else if s.eq_ignore_ascii_case("err") || s.eq_ignore_ascii_case("error") {
            Self::Error
        } else if s.eq_ignore_ascii_case("crit")
            || s.eq_ignore_ascii_case("critical")
            || s.eq_ignore_ascii_case("fatal")
        {
            Self::Fatal
        } else {
            Self::Unknown
        }
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Trace => "trace",
            Self::Debug => "debug",
            Self::Info => "info",
            Self::Warn => "warn",
            Self::Error => "error",
            Self::Fatal => "fatal",
            Self::Unknown => "unknown",
        }
    }
}

/// Zero-copy parsed view of a single log line directly borrowing from the mmap buffer where possible.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LogRecordView<'a> {
    pub timestamp_ms: u64,
    pub level: LogLevel,
    pub source: Cow<'a, str>,
    pub message: Cow<'a, str>,
    pub raw: &'a str,
    pub line_number: usize,
    pub fields: Vec<(String, String)>,
}

/// Owned version of LogRecord for persistent storage, export or serialization.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct OwnedLogRecord {
    pub timestamp_ms: u64,
    pub level: LogLevel,
    pub source: String,
    pub message: String,
    pub raw: String,
    pub line_number: usize,
    pub fields: Vec<(String, String)>,
}

impl<'a> LogRecordView<'a> {
    pub fn to_owned(&self) -> OwnedLogRecord {
        OwnedLogRecord {
            timestamp_ms: self.timestamp_ms,
            level: self.level,
            source: self.source.clone().into_owned(),
            message: self.message.clone().into_owned(),
            raw: self.raw.to_string(),
            line_number: self.line_number,
            fields: self.fields.clone(),
        }
    }
}
