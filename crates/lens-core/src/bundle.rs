//! Forensic flight recorder bundles (`.lens` files).
//!
//! A bundle is a gzip-compressed tar archive containing a `manifest.json`
//! (SHA-256 + size for every artifact) plus one JSON entry per collected
//! artifact under `reports/`. Create and verify share this module so the
//! two halves of the format can never drift apart.

use flate2::read::GzDecoder;
use flate2::write::GzEncoder;
use flate2::Compression;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs::File;
use std::path::Path;
use tar::{Archive, Builder, Header};

use crate::error::{LensError, Result};
use crate::evidence::Source;
use crate::hash::{digest_bytes, digest_reader};

pub const BUNDLE_SCHEMA_V2: &str = "lens.bundle/v2";
pub const MANIFEST_ENTRY: &str = "manifest.json";

/// Defensive caps against hostile archives (decompression bombs).
const MAX_BUNDLE_ENTRIES: usize = 4096;
const MAX_BUNDLE_BYTES: u64 = 512 * 1024 * 1024;

/// The signed inventory of a `.lens` bundle.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct BundleManifest {
    pub schema: String,
    pub tool: String,
    pub version: String,
    /// Real UTC creation time (RFC 3339). Never fabricated.
    pub created_at: String,
    #[serde(default)]
    pub sources: Vec<Source>,
    /// Collection failures recorded at bundle time — evidence of absence.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub diagnostics: Vec<String>,
}

/// One artifact destined for the bundle (typically a serialized report).
#[derive(Debug, Clone)]
pub struct BundleArtifact {
    /// Tar entry name, e.g. `reports/disk_snapshot.json`.
    pub name: String,
    pub data: Vec<u8>,
}

impl BundleArtifact {
    pub fn json<T: Serialize>(name: &str, value: &T) -> Result<Self> {
        Ok(Self {
            name: name.to_string(),
            data: crate::json::to_deterministic_pretty(value)?.into_bytes(),
        })
    }
}

/// Writes `artifacts` plus a generated `manifest.json` to `bundle_path` as a
/// `.tar.gz` archive and returns the manifest that was embedded.
pub fn create_bundle_archive<P: AsRef<Path>>(
    bundle_path: P,
    tool_version: &str,
    artifacts: &[BundleArtifact],
    diagnostics: Vec<String>,
) -> Result<BundleManifest> {
    let path_ref = bundle_path.as_ref();
    let file = File::create(path_ref).map_err(|e| LensError::Io {
        path: path_ref.to_path_buf(),
        source: e,
    })?;

    let mut sources = Vec::with_capacity(artifacts.len());
    let mut entries: Vec<(String, Vec<u8>)> = Vec::with_capacity(artifacts.len() + 1);
    for art in artifacts {
        sources.push(Source {
            path: art.name.clone(),
            sha256: digest_bytes(&art.data),
            size: art.data.len() as u64,
            ..Default::default()
        });
        entries.push((art.name.clone(), art.data.clone()));
    }

    let manifest = BundleManifest {
        schema: BUNDLE_SCHEMA_V2.to_string(),
        tool: "lens".to_string(),
        version: tool_version.to_string(),
        created_at: crate::time::utc_now_iso(),
        sources,
        diagnostics,
    };
    let manifest_json = crate::json::to_deterministic_pretty(&manifest)?.into_bytes();
    entries.push((MANIFEST_ENTRY.to_string(), manifest_json));

    let gz = GzEncoder::new(file, Compression::default());
    let mut tar = Builder::new(gz);
    for (name, data) in &entries {
        let mut header = Header::new_gnu();
        header.set_size(data.len() as u64);
        header.set_mode(0o644);
        header.set_cksum();
        tar.append_data(&mut header, name, data.as_slice())
            .map_err(|e| LensError::Io {
                path: path_ref.to_path_buf(),
                source: e,
            })?;
    }
    let gz = tar.into_inner().map_err(|e| LensError::Io {
        path: path_ref.to_path_buf(),
        source: e,
    })?;
    gz.finish().map_err(|e| LensError::Io {
        path: path_ref.to_path_buf(),
        source: e,
    })?;

    Ok(manifest)
}

/// Verification report for a forensic bundle archive.
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

/// Inspection summary used by `lens bundle inspect`.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct BundleInspection {
    pub bundle_path: String,
    pub manifest: Option<BundleManifest>,
    pub entries: Vec<BundleEntryInfo>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct BundleEntryInfo {
    pub name: String,
    pub size: u64,
}

fn io_err(path: &Path, e: std::io::Error) -> LensError {
    LensError::Io {
        path: path.to_path_buf(),
        source: e,
    }
}

/// Streams every entry of the archive, invoking `on_entry(name, reader)` for
/// regular files. Enforces entry-count and decompressed-size caps.
fn stream_entries<P, F>(bundle_path: P, mut on_entry: F) -> Result<()>
where
    P: AsRef<Path>,
    F: FnMut(&str, &mut dyn std::io::Read, u64) -> Result<()>,
{
    let path_ref = bundle_path.as_ref();
    let file = File::open(path_ref).map_err(|e| io_err(path_ref, e))?;
    let gz = GzDecoder::new(file);
    let mut archive = Archive::new(gz);
    let entries = archive.entries().map_err(|e| io_err(path_ref, e))?;

    let mut count = 0usize;
    let mut total_bytes = 0u64;
    for entry_res in entries {
        let mut entry = entry_res.map_err(|e| io_err(path_ref, e))?;
        if !entry.header().entry_type().is_file() {
            continue;
        }
        count += 1;
        if count > MAX_BUNDLE_ENTRIES {
            return Err(LensError::LimitExceeded {
                message: format!("bundle exceeds {} entries", MAX_BUNDLE_ENTRIES),
            });
        }
        let size = entry.header().size().unwrap_or(0);
        total_bytes = total_bytes.saturating_add(size);
        if total_bytes > MAX_BUNDLE_BYTES {
            return Err(LensError::LimitExceeded {
                message: format!("bundle exceeds {} decompressed bytes", MAX_BUNDLE_BYTES),
            });
        }
        let name = entry
            .path()
            .map_err(|e| io_err(path_ref, e))?
            .to_string_lossy()
            .to_string();
        on_entry(&name, &mut entry, size)?;
    }
    Ok(())
}

/// True when `entry_name` names manifest path `src` exactly or as a
/// path-suffix on a `/` boundary (never a bare substring suffix).
fn entry_matches(entry_name: &str, src_path: &str) -> bool {
    entry_name == src_path
        || (entry_name.len() > src_path.len()
            && entry_name.ends_with(src_path)
            && entry_name.as_bytes()[entry_name.len() - src_path.len() - 1] == b'/')
}

/// Verifies every manifest-listed file's SHA-256 against the archive contents.
pub fn verify_bundle_archive<P: AsRef<Path>>(bundle_path: P) -> Result<BundleVerificationReport> {
    let path_ref = bundle_path.as_ref();
    let invalid = |report: &mut BundleVerificationReport, msg: String| {
        report.error = Some(msg);
        report.is_valid = false;
    };

    let mut file_hashes: HashMap<String, String> = HashMap::new();
    let mut manifest_content: Option<Vec<u8>> = None;

    stream_entries(path_ref, |name, reader, _size| {
        if name == MANIFEST_ENTRY || name.ends_with(&format!("/{}", MANIFEST_ENTRY)) {
            let mut content = Vec::new();
            reader
                .read_to_end(&mut content)
                .map_err(|e| io_err(path_ref, e))?;
            manifest_content = Some(content);
        } else {
            let hash = digest_reader(reader).map_err(|e| io_err(path_ref, e))?;
            file_hashes.insert(name.to_string(), hash);
        }
        Ok(())
    })?;

    let mut report = BundleVerificationReport {
        bundle_path: path_ref.display().to_string(),
        manifest_found: false,
        total_files: file_hashes.len(),
        verified_files: 0,
        tampered_files: Vec::new(),
        missing_files: Vec::new(),
        is_valid: false,
        error: None,
    };

    let manifest_bytes = match manifest_content {
        Some(b) => b,
        None => {
            invalid(
                &mut report,
                "Archive does not contain manifest.json".to_string(),
            );
            return Ok(report);
        }
    };
    report.manifest_found = true;

    let manifest: BundleManifest = match serde_json::from_slice(&manifest_bytes) {
        Ok(m) => m,
        Err(e) => {
            invalid(&mut report, format!("Invalid manifest.json: {}", e));
            return Ok(report);
        }
    };
    report.total_files = manifest.sources.len();

    for src in &manifest.sources {
        let matching_hash = file_hashes
            .iter()
            .find(|(k, _)| entry_matches(k, &src.path))
            .map(|(_, v)| v.as_str());

        match matching_hash {
            Some(actual_hash) => {
                if actual_hash.eq_ignore_ascii_case(&src.sha256) {
                    report.verified_files += 1;
                } else {
                    report.tampered_files.push(format!(
                        "{}: expected {} got {}",
                        src.path, src.sha256, actual_hash
                    ));
                }
            }
            None => report.missing_files.push(src.path.clone()),
        }
    }

    report.is_valid = report.tampered_files.is_empty() && report.missing_files.is_empty();
    Ok(report)
}

/// Reads the manifest and entry listing of a bundle without hashing payloads.
pub fn inspect_bundle<P: AsRef<Path>>(bundle_path: P) -> Result<BundleInspection> {
    let path_ref = bundle_path.as_ref();
    let mut manifest_bytes = None;
    let mut entries = Vec::new();

    stream_entries(path_ref, |name, reader, size| {
        entries.push(BundleEntryInfo {
            name: name.to_string(),
            size,
        });
        if name == MANIFEST_ENTRY || name.ends_with(&format!("/{}", MANIFEST_ENTRY)) {
            let mut content = Vec::new();
            reader
                .read_to_end(&mut content)
                .map_err(|e| io_err(path_ref, e))?;
            manifest_bytes = Some(content);
        }
        Ok(())
    })?;

    let manifest = match manifest_bytes {
        Some(b) => Some(serde_json::from_slice::<BundleManifest>(&b).map_err(|e| {
            LensError::InvalidInput {
                message: format!("Invalid manifest.json: {}", e),
            }
        })?),
        None => None,
    };

    Ok(BundleInspection {
        bundle_path: path_ref.display().to_string(),
        manifest,
        entries,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::NamedTempFile;

    fn sample_artifacts() -> Vec<BundleArtifact> {
        vec![
            BundleArtifact::json(
                "reports/disk_snapshot.json",
                &serde_json::json!({"schema": "diskmap.snapshot/v2", "node_count": 7}),
            )
            .unwrap(),
            BundleArtifact::json(
                "reports/net_report.json",
                &serde_json::json!({"schema_version": "lens.net/v1"}),
            )
            .unwrap(),
        ]
    }

    #[test]
    fn test_create_verify_roundtrip() {
        let tmp = NamedTempFile::new().unwrap();
        let manifest =
            create_bundle_archive(tmp.path(), "0.5.0-test", &sample_artifacts(), vec![]).unwrap();

        assert_eq!(manifest.schema, BUNDLE_SCHEMA_V2);
        assert_eq!(manifest.sources.len(), 2);
        assert!(manifest.created_at.ends_with('Z'));

        let report = verify_bundle_archive(tmp.path()).unwrap();
        assert!(report.is_valid, "report was: {:?}", report);
        assert_eq!(report.verified_files, 2);
        assert_eq!(report.total_files, 2);
        assert!(report.tampered_files.is_empty());
        assert!(report.missing_files.is_empty());

        let inspection = inspect_bundle(tmp.path()).unwrap();
        assert_eq!(inspection.entries.len(), 3); // 2 reports + manifest
        let m = inspection.manifest.unwrap();
        assert_eq!(m.tool, "lens");
        assert_eq!(m.version, "0.5.0-test");
    }

    #[test]
    fn test_verify_detects_tampering() {
        let tmp = NamedTempFile::new().unwrap();
        create_bundle_archive(tmp.path(), "x", &sample_artifacts(), vec![]).unwrap();

        // Flip a byte inside the archive -> decompression or hash failure.
        let mut raw = std::fs::read(tmp.path()).unwrap();
        let idx = raw.len() / 2;
        raw[idx] ^= 0xFF;
        std::fs::write(tmp.path(), &raw).unwrap();

        let report = verify_bundle_archive(tmp.path());
        // Either the archive fails to parse, or it verifies as invalid.
        if let Ok(rep) = report {
            assert!(!rep.is_valid);
        }
    }

    #[test]
    fn test_suffix_collision_is_rejected() {
        // "attest.txt" must NOT satisfy a manifest entry for "test.txt".
        assert!(!entry_matches("attest.txt", "test.txt"));
        assert!(!entry_matches("xtest.txt", "test.txt"));
        assert!(entry_matches("test.txt", "test.txt"));
        assert!(entry_matches("reports/test.txt", "test.txt"));
        assert!(entry_matches("./test.txt", "test.txt"));
    }
}
