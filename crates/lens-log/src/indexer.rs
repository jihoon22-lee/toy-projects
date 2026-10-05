use memmap2::Mmap;
use std::path::Path;

use lens_core::{Result, SafeInput};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct LineSpan {
    pub offset: usize,
    pub length: usize,
}

pub struct LogIndexer {
    mmap: Mmap,
    spans: Vec<LineSpan>,
}

impl LogIndexer {
    pub fn open<P: AsRef<Path>>(path: P) -> Result<Self> {
        let safe_input = SafeInput::open(path)?;
        let mmap = safe_input.mmap()?;
        let data = &mmap[..];

        let mut spans = Vec::with_capacity(data.len() / 64);
        let mut line_start = 0;

        for (idx, &byte) in data.iter().enumerate() {
            if byte == b'\n' {
                let mut line_end = idx;
                // Strip trailing \r
                if line_end > line_start && data[line_end - 1] == b'\r' {
                    line_end -= 1;
                }
                spans.push(LineSpan {
                    offset: line_start,
                    length: line_end - line_start,
                });
                line_start = idx + 1;
            }
        }

        // Remaining bytes without trailing newline
        if line_start < data.len() {
            let mut line_end = data.len();
            if line_end > line_start && data[line_end - 1] == b'\r' {
                line_end -= 1;
            }
            spans.push(LineSpan {
                offset: line_start,
                length: line_end - line_start,
            });
        }

        Ok(Self { mmap, spans })
    }

    pub fn len(&self) -> usize {
        self.spans.len()
    }

    pub fn is_empty(&self) -> bool {
        self.spans.is_empty()
    }

    pub fn get_line(&self, index: usize) -> Option<&str> {
        let span = self.spans.get(index)?;
        let bytes = &self.mmap[span.offset..span.offset + span.length];
        std::str::from_utf8(bytes).ok()
    }

    pub fn get_raw_bytes(&self, index: usize) -> Option<&[u8]> {
        let span = self.spans.get(index)?;
        Some(&self.mmap[span.offset..span.offset + span.length])
    }
}
