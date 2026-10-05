use crate::record::{LogLevel, LogRecordView};

/// Checks if `haystack` contains `needle_lower` ignoring ASCII case without allocating any memory.
///
/// Invariant: `needle_lower` MUST be pre-lowercased.
pub fn contains_insensitive(haystack: &str, needle_lower: &str) -> bool {
    if needle_lower.is_empty() {
        return true;
    }
    let needle_bytes = needle_lower.as_bytes();
    let haystack_bytes = haystack.as_bytes();
    if needle_bytes.len() > haystack_bytes.len() {
        return false;
    }
    haystack_bytes.windows(needle_bytes.len()).any(|window| {
        window
            .iter()
            .zip(needle_bytes.iter())
            .all(|(&h, &n)| h.to_ascii_lowercase() == n)
    })
}

#[derive(Debug, Clone, Default)]
pub struct LogFilter {
    pub min_level: Option<LogLevel>,
    pub query_lower: Option<String>,
    pub source: Option<String>,
}

impl LogFilter {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn with_min_level(mut self, level: LogLevel) -> Self {
        self.min_level = Some(level);
        self
    }

    pub fn with_query(mut self, query: &str) -> Self {
        if query.trim().is_empty() {
            self.query_lower = None;
        } else {
            self.query_lower = Some(query.to_ascii_lowercase());
        }
        self
    }

    pub fn with_source(mut self, source: &str) -> Self {
        self.source = Some(source.to_string());
        self
    }

    /// Match a raw line without constructing a `LogRecordView` when the
    /// configured filters allow it: the substring check alone can reject, and
    /// a no-op filter accepts everything without parsing.
    pub fn matches_line(&self, raw: &str, line_number: usize) -> bool {
        if let Some(needle_lower) = &self.query_lower {
            if !contains_insensitive(raw, needle_lower) {
                return false;
            }
        }
        if self.min_level.is_none() && self.source.is_none() {
            return true;
        }
        self.matches(&crate::parse_line(raw, line_number))
    }

    pub fn matches(&self, record: &LogRecordView) -> bool {
        if let Some(min_lvl) = self.min_level {
            if record.level != LogLevel::Unknown && record.level < min_lvl {
                return false;
            }
        }

        if let Some(src) = &self.source {
            if !record.source.is_empty() && record.source != src.as_str() {
                return false;
            }
        }

        if let Some(needle_lower) = &self.query_lower {
            if !contains_insensitive(record.raw, needle_lower) {
                return false;
            }
        }

        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_non_allocating_contains_insensitive() {
        assert!(contains_insensitive("Error occurred in subsystem", "error"));
        assert!(contains_insensitive("ERROR occurred in subsystem", "error"));
        assert!(contains_insensitive("system error", "error"));
        assert!(!contains_insensitive("All clean", "error"));
    }
}
