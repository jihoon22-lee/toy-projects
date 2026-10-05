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
    #[serde(default)]
    pub device: u64,
    #[serde(default)]
    pub inode: u64,
    #[serde(default)]
    pub size: u64,
    #[serde(default)]
    pub scanned_bytes: u64,
    #[serde(default)]
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

/// Verification report for a Forensic Flight Recorder bundle (.tar.gz).
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct BundleVerificationReport {
    pub bundle_path: String,
    pub manifest_found: bool,
    pub total_files: usize,
    pub verified_files: usize,
    pub tampered_files: Vec<String>,
    pub missing_files: Vec<String>,
    pub is_valid: bool,
    pub error: Option<String>,
}

/// Verifies the cryptographic integrity of a forensic bundle archive against its manifest.
pub fn verify_bundle_archive<P: AsRef<std::path::Path>>(
    bundle_path: P,
) -> crate::error::Result<BundleVerificationReport> {
    use crate::error::LensError;
    use crate::hash::digest_bytes;
    use flate2::read::GzDecoder;
    use std::collections::HashMap;
    use std::fs::File;
    use std::io::Read;
    use tar::Archive;

    let path_ref = bundle_path.as_ref();
    let file = File::open(path_ref).map_err(|e| LensError::Io {
        path: path_ref.to_path_buf(),
        source: e,
    })?;

    let gz = GzDecoder::new(file);
    let mut archive = Archive::new(gz);

    let mut file_hashes = HashMap::new();
    let mut manifest_content = None;

    let entries = archive.entries().map_err(|e| LensError::Io {
        path: path_ref.to_path_buf(),
        source: e,
    })?;

    for entry_res in entries {
        let mut entry = entry_res.map_err(|e| LensError::Io {
            path: path_ref.to_path_buf(),
            source: e,
        })?;

        let entry_path = entry
            .path()
            .map_err(|e| LensError::Io {
                path: path_ref.to_path_buf(),
                source: e,
            })?
            .to_string_lossy()
            .to_string();

        let mut content = Vec::new();
        entry.read_to_end(&mut content).map_err(|e| LensError::Io {
            path: path_ref.to_path_buf(),
            source: e,
        })?;

        let hash = digest_bytes(&content);
        if entry_path.ends_with("manifest.json") {
            manifest_content = Some(content);
        } else {
            file_hashes.insert(entry_path, hash);
        }
    }

    let manifest_bytes = match manifest_content {
        Some(b) => b,
        None => {
            return Ok(BundleVerificationReport {
                bundle_path: path_ref.display().to_string(),
                manifest_found: false,
                total_files: file_hashes.len(),
                verified_files: 0,
                tampered_files: Vec::new(),
                missing_files: Vec::new(),
                is_valid: false,
                error: Some("Archive does not contain manifest.json".to_string()),
            });
        }
    };

    #[derive(Deserialize)]
    struct Manifest {
        #[serde(default)]
        sources: Vec<Source>,
    }

    let manifest: Manifest = match serde_json::from_slice(&manifest_bytes) {
        Ok(m) => m,
        Err(e) => {
            return Ok(BundleVerificationReport {
                bundle_path: path_ref.display().to_string(),
                manifest_found: true,
                total_files: file_hashes.len(),
                verified_files: 0,
                tampered_files: Vec::new(),
                missing_files: Vec::new(),
                is_valid: false,
                error: Some(format!("Invalid manifest.json: {}", e)),
            });
        }
    };

    let mut verified = 0;
    let mut tampered = Vec::new();
    let mut missing = Vec::new();

    for src in &manifest.sources {
        // Find matching entry by suffix or exact name
        let matching_hash = file_hashes
            .iter()
            .find(|(k, _)| k.as_str() == src.path || k.ends_with(&src.path))
            .map(|(_, v)| v.as_str());

        match matching_hash {
            Some(actual_hash) => {
                if actual_hash.eq_ignore_ascii_case(&src.sha256) {
                    verified += 1;
                } else {
                    tampered.push(format!(
                        "{}: expected {} got {}",
                        src.path, src.sha256, actual_hash
                    ));
                }
            }
            None => {
                missing.push(src.path.clone());
            }
        }
    }

    let is_valid = tampered.is_empty() && missing.is_empty();

    Ok(BundleVerificationReport {
        bundle_path: path_ref.display().to_string(),
        manifest_found: true,
        total_files: manifest.sources.len(),
        verified_files: verified,
        tampered_files: tampered,
        missing_files: missing,
        is_valid,
        error: None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use flate2::write::GzEncoder;
    use flate2::Compression;
    use tempfile::NamedTempFile;

    #[test]
    fn test_verify_bundle_archive() {
        let manifest_json = r#"{
            "sources": [
                {
                    "path": "test.txt",
                    "sha256": "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"
                }
            ]
        }"#;

        let temp = NamedTempFile::new().unwrap();
        let gz = GzEncoder::new(temp.as_file(), Compression::default());
        let mut tar = tar::Builder::new(gz);

        let mut manifest_header = tar::Header::new_gnu();
        manifest_header.set_size(manifest_json.len() as u64);
        manifest_header.set_mode(0o644);
        manifest_header.set_cksum();
        tar.append_data(
            &mut manifest_header,
            "manifest.json",
            manifest_json.as_bytes(),
        )
        .unwrap();

        let file_data = b"test";
        let mut file_header = tar::Header::new_gnu();
        file_header.set_size(file_data.len() as u64);
        file_header.set_mode(0o644);
        file_header.set_cksum();
        tar.append_data(&mut file_header, "test.txt", &file_data[..])
            .unwrap();

        let gz = tar.into_inner().unwrap();
        gz.finish().unwrap();

        let report = verify_bundle_archive(temp.path()).unwrap();
        assert!(report.is_valid, "report was: {:?}", report);
        assert_eq!(report.verified_files, 1);
        assert!(report.tampered_files.is_empty());
    }
}
