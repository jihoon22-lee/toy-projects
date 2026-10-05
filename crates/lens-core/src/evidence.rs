use serde::{Deserialize, Serialize};

/// Exact location within an input stream or file where an observation was made.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize, Default)]
pub struct Evidence {
    pub source: usize,
    pub line: u64,
    pub offset: u64,
    pub length: u64,
}

/// Metadata describing an analyzed source file, ensuring provenance and change detection.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
pub struct Source {
    pub path: String,
    pub sha256: String,
    pub device: u64,
    pub inode: u64,
    pub size: u64,
    pub scanned_bytes: u64,
    pub lines: u64,
    #[serde(skip_serializing_if = "String::is_empty", default)]
    pub modified_ns: String,
    #[serde(default)]
    pub changed: bool,
}

/// A structured diagnostic message linked to evidence where available.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Diagnostic {
    pub code: String,
    pub message: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub evidence: Option<Evidence>,
}

impl Diagnostic {
    pub fn new(code: impl Into<String>, message: impl Into<String>) -> Self {
        Self {
            code: code.into(),
            message: message.into(),
            evidence: None,
        }
    }

    pub fn with_evidence(mut self, evidence: Evidence) -> Self {
        self.evidence = Some(evidence);
        self
    }
}

/// A capacity-bounded collector to prevent denial-of-service / memory blowup on malformed inputs.
#[derive(Debug, Clone)]
pub struct BoundedCollector<T> {
    items: Vec<T>,
    limit: usize,
    truncated: bool,
}

impl<T> BoundedCollector<T> {
    pub fn new(limit: usize) -> Self {
        Self {
            items: Vec::with_capacity(limit.min(1024)),
            limit,
            truncated: false,
        }
    }

    pub fn push(&mut self, item: T) -> bool {
        if self.items.len() < self.limit {
            self.items.push(item);
            true
        } else {
            self.truncated = true;
            false
        }
    }

    pub fn is_truncated(&self) -> bool {
        self.truncated
    }

    pub fn into_inner(self) -> (Vec<T>, bool) {
        (self.items, self.truncated)
    }

    pub fn len(&self) -> usize {
        self.items.len()
    }

    pub fn is_empty(&self) -> bool {
        self.items.is_empty()
    }
}
