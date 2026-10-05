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
use std::io::Read;
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

/// The integrity inventory of a `.lens` bundle. It is unsigned: verification
/// detects corruption, not an attacker who can rewrite the whole archive.
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

/// True when `name` is a safe relative tar entry name (no absolute paths,
/// no `..` components, no backslashes or NUL).
fn valid_entry_name(name: &str) -> bool {
    if name.is_empty()
        || name.starts_with('/')
        || name.starts_with("./")
        || name.contains('\\')
        || name.contains('\0')
    {
        return false;
    }
    !name.split('/').any(|c| c == ".." || c.is_empty())
}

/// Writes `artifacts` plus a generated `manifest.json` to `bundle_path` as a
/// `.tar.gz` archive and returns the manifest that was embedded. The archive
/// is staged to a temporary sibling file and atomically renamed on success,
/// so a failed write never leaves a partial bundle behind.
pub fn create_bundle_archive<P: AsRef<Path>>(
    bundle_path: P,
    tool_version: &str,
    artifacts: &[BundleArtifact],
    diagnostics: Vec<String>,
) -> Result<BundleManifest> {
    let path_ref = bundle_path.as_ref();

    let mut seen = std::collections::HashSet::new();
    for art in artifacts {
        if !valid_entry_name(&art.name) {
            return Err(LensError::InvalidInput {
                message: format!("unsafe bundle entry name: {:?}", art.name),
            });
        }
        if !seen.insert(art.name.as_str()) {
            return Err(LensError::InvalidInput {
                message: format!("duplicate bundle entry name: {}", art.name),
            });
        }
        if art.name == MANIFEST_ENTRY {
            return Err(LensError::InvalidInput {
                message: format!("artifact name {} is reserved", MANIFEST_ENTRY),
            });
        }
    }

    let mut sources = Vec::with_capacity(artifacts.len());
    for art in artifacts {
        sources.push(Source {
            path: art.name.clone(),
            sha256: digest_bytes(&art.data),
            size: art.data.len() as u64,
            ..Default::default()
        });
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

    let tmp_path = path_ref.with_extension("lens.tmp");
    let write_result = (|| -> std::result::Result<(), std::io::Error> {
        let file = File::create(&tmp_path)?;
        let gz = GzEncoder::new(file, Compression::default());
        let mut tar = Builder::new(gz);
        let mut append = |name: &str, data: &[u8]| -> std::result::Result<(), std::io::Error> {
            let mut header = Header::new_gnu();
            header.set_size(data.len() as u64);
            header.set_mode(0o644);
            header.set_cksum();
            tar.append_data(&mut header, name, data)
        };
        for art in artifacts {
            append(&art.name, &art.data)?;
        }
        append(MANIFEST_ENTRY, &manifest_json)?;
        let gz = tar.into_inner()?;
        gz.finish()?;
        Ok(())
    })();

    match write_result {
        Ok(()) => {
            std::fs::rename(&tmp_path, path_ref).map_err(|e| io_err(path_ref, e))?;
            Ok(manifest)
        }
        Err(e) => {
            let _ = std::fs::remove_file(&tmp_path);
            Err(io_err(path_ref, e))
        }
    }
}

/// Verification report for a forensic bundle archive.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct BundleVerificationReport {
    pub bundle_path: String,
    pub manifest_found: bool,
    /// Number of regular-file entries actually present in the archive.
    pub total_files: usize,
    pub verified_files: usize,
    pub tampered_files: Vec<String>,
    pub missing_files: Vec<String>,
    /// Archive entries not listed in the manifest (or listed twice).
    #[serde(default)]
    pub unexpected_files: Vec<String>,
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

/// Read wrapper that turns a decompression bomb into a hard error once the
/// decoded stream exceeds `limit` bytes.
struct LimitedReader<R> {
    inner: R,
    remaining: u64,
}

impl<R: std::io::Read> std::io::Read for LimitedReader<R> {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        if self.remaining == 0 {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                "bundle exceeds maximum decompressed size",
            ));
        }
        let max = self.remaining.min(buf.len() as u64) as usize;
        let n = self.inner.read(&mut buf[..max])?;
        self.remaining -= n as u64;
        Ok(n)
    }
}

/// Streams every entry of the archive, invoking `on_entry(name, reader)` for
/// regular files. Enforces an entry-count cap on *all* entry types and a cap
/// on actual decompressed bytes (not just header-declared sizes).
fn stream_entries<P, F>(bundle_path: P, mut on_entry: F) -> Result<()>
where
    P: AsRef<Path>,
    F: FnMut(&str, &mut dyn std::io::Read, u64) -> Result<()>,
{
    let path_ref = bundle_path.as_ref();
    let file = File::open(path_ref).map_err(|e| io_err(path_ref, e))?;
    let gz = LimitedReader {
        inner: GzDecoder::new(file),
        remaining: MAX_BUNDLE_BYTES,
    };
    let mut archive = Archive::new(gz);
    let entries = archive.entries().map_err(|e| io_err(path_ref, e))?;

    let mut count = 0usize;
    for entry_res in entries {
        let mut entry = entry_res.map_err(|e| {
            let err = io_err(path_ref, e);
            // Surface the decompressed-size limit as a typed error.
            if let LensError::Io { source, .. } = &err {
                if source.kind() == std::io::ErrorKind::InvalidData {
                    return LensError::LimitExceeded {
                        message: format!(
                            "bundle exceeds {} decompressed bytes",
                            MAX_BUNDLE_BYTES
                        ),
                    };
                }
            }
            err
        })?;
        count += 1;
        if count > MAX_BUNDLE_ENTRIES {
            return Err(LensError::LimitExceeded {
                message: format!("bundle exceeds {} entries", MAX_BUNDLE_ENTRIES),
            });
        }
        if !entry.header().entry_type().is_file() {
            continue;
        }
        let size = entry.header().size().unwrap_or(0);
        let name = entry
            .path()
            .map_err(|e| io_err(path_ref, e))?
            .to_string_lossy()
            .to_string();
        on_entry(&name, &mut entry, size)?;
    }
    Ok(())
}

/// Normalizes an archive entry name for comparison: strips a leading `./`.
fn normalize_entry_name(name: &str) -> &str {
    name.strip_prefix("./").unwrap_or(name)
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
    let mut manifest_count = 0usize;

    stream_entries(path_ref, |name, reader, _size| {
        // Only an exact top-level `manifest.json` (optionally `./`-prefixed)
        // is the manifest. `foo/manifest.json` is an ordinary artifact.
        if normalize_entry_name(name) == MANIFEST_ENTRY {
            manifest_count += 1;
            let mut content = Vec::new();
            reader
                .take(MAX_BUNDLE_BYTES)
                .read_to_end(&mut content)
                .map_err(|e| io_err(path_ref, e))?;
            manifest_content = Some(content);
        } else {
            let hash = digest_reader(reader).map_err(|e| io_err(path_ref, e))?;
            let key = normalize_entry_name(name).to_string();
            if file_hashes.insert(key.clone(), hash).is_some() {
                // Duplicate entry name — archive is not reproducible evidence.
                file_hashes.insert(key, "DUPLICATE".to_string());
            }
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
        unexpected_files: Vec::new(),
        is_valid: false,
        error: None,
    };

    if manifest_count > 1 {
        invalid(
            &mut report,
            format!("Archive contains {} manifest.json entries", manifest_count),
        );
        return Ok(report);
    }

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

    if manifest.schema != BUNDLE_SCHEMA_V2 {
        invalid(
            &mut report,
            format!(
                "Unsupported manifest schema {:?} (expected {:?})",
                manifest.schema, BUNDLE_SCHEMA_V2
            ),
        );
        return Ok(report);
    }

    let mut consumed: std::collections::HashSet<&str> = std::collections::HashSet::new();
    for src in &manifest.sources {
        let normalized = normalize_entry_name(&src.path);
        match file_hashes.get(normalized) {
            Some(actual_hash) => {
                consumed.insert(normalized);
                if actual_hash == "DUPLICATE" {
                    report
                        .unexpected_files
                        .push(format!("duplicate entry: {}", normalized));
                } else if actual_hash.eq_ignore_ascii_case(&src.sha256) {
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
    for key in file_hashes.keys() {
        if !consumed.contains(key.as_str()) {
            report.unexpected_files.push(key.clone());
        }
    }
    report.unexpected_files.sort();

    report.is_valid = report.tampered_files.is_empty()
        && report.missing_files.is_empty()
        && report.unexpected_files.is_empty()
        && !manifest.sources.is_empty();
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
        if normalize_entry_name(name) == MANIFEST_ENTRY && manifest_bytes.is_none() {
            let mut content = Vec::new();
            reader
                .take(MAX_BUNDLE_BYTES)
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

    fn rebuild_with(
        path: &Path,
        mutate: impl FnOnce(&mut Vec<(String, Vec<u8>)>),
    ) {
        let raw = std::fs::read(path).unwrap();
        let mut archive = Archive::new(GzDecoder::new(&raw[..]));
        let mut rebuilt: Vec<(String, Vec<u8>)> = Vec::new();
        for entry in archive.entries().unwrap().flatten() {
            let mut e = entry;
            let mut data = Vec::new();
            std::io::Read::read_to_end(&mut e, &mut data).unwrap();
            let name = e.path().unwrap().to_string_lossy().into_owned();
            rebuilt.push((name, data));
        }
        mutate(&mut rebuilt);
        let out = File::create(path).unwrap();
        let gz = GzEncoder::new(out, Compression::default());
        let mut builder = Builder::new(gz);
        for (name, data) in rebuilt {
            let mut header = Header::new_gnu();
            header.set_size(data.len() as u64);
            header.set_mode(0o644);
            header.set_cksum();
            builder.append_data(&mut header, name, data.as_slice()).unwrap();
        }
        builder.into_inner().unwrap().finish().unwrap();
    }

    #[test]
    fn test_verify_detects_tampering() {
        let tmp = NamedTempFile::new().unwrap();
        create_bundle_archive(tmp.path(), "x", &sample_artifacts(), vec![]).unwrap();

        // Rebuild the archive deterministically with one artifact's payload
        // altered — bit-flipping the compressed stream is unreliable (the
        // flipped byte can land in tar padding that nothing hashes).
        rebuild_with(tmp.path(), |entries| {
            for (name, data) in entries.iter_mut() {
                if name == "reports/disk_snapshot.json" {
                    data[0] ^= 0xFF; // tamper the artifact, keep the manifest intact
                }
            }
        });

        let report = verify_bundle_archive(tmp.path()).unwrap();
        assert!(!report.is_valid);
        assert_eq!(report.tampered_files.len(), 1);
        assert!(report.tampered_files[0].contains("disk_snapshot"));
    }

    #[test]
    fn test_verify_rejects_spoofed_nested_manifest() {
        let tmp = NamedTempFile::new().unwrap();
        create_bundle_archive(tmp.path(), "x", &sample_artifacts(), vec![]).unwrap();

        // Attack: tamper an artifact, then append an `evil/manifest.json`
        // claiming the tampered hash. An older suffix-match verifier would
        // treat the appended file as the manifest and verify "clean".
        rebuild_with(tmp.path(), |entries| {
            for (name, data) in entries.iter_mut() {
                if name == "reports/disk_snapshot.json" {
                    data[0] ^= 0xFF;
                }
            }
            let fake = serde_json::json!({
                "schema": BUNDLE_SCHEMA_V2,
                "tool": "lens",
                "version": "x",
                "created_at": "2026-01-01T00:00:00Z",
                "sources": []
            });
            entries.push((
                "evil/manifest.json".to_string(),
                serde_json::to_vec_pretty(&fake).unwrap(),
            ));
        });

        let report = verify_bundle_archive(tmp.path()).unwrap();
        assert!(!report.is_valid);
        // Tampering is still caught, and the injected manifest is flagged.
        assert_eq!(report.tampered_files.len(), 1);
        assert_eq!(report.unexpected_files, vec!["evil/manifest.json"]);
    }

    #[test]
    fn test_verify_rejects_unlisted_entries() {
        let tmp = NamedTempFile::new().unwrap();
        create_bundle_archive(tmp.path(), "x", &sample_artifacts(), vec![]).unwrap();

        rebuild_with(tmp.path(), |entries| {
            entries.push((
                "reports/smuggled.json".to_string(),
                b"{\"hidden\": true}".to_vec(),
            ));
        });

        let report = verify_bundle_archive(tmp.path()).unwrap();
        assert!(!report.is_valid);
        assert_eq!(report.unexpected_files, vec!["reports/smuggled.json"]);
    }

    #[test]
    fn test_create_rejects_unsafe_entry_names() {
        let tmp = NamedTempFile::new().unwrap();
        for bad in ["../x.json", "/abs.json", "a//b.json", "manifest.json"] {
            let artifacts = vec![BundleArtifact {
                name: bad.to_string(),
                data: b"{}".to_vec(),
            }];
            assert!(
                create_bundle_archive(tmp.path(), "x", &artifacts, vec![]).is_err(),
                "{bad} should be rejected"
            );
        }
    }

    #[test]
    fn test_failed_create_leaves_no_partial_bundle() {
        let dir = tempfile::tempdir().unwrap();
        let out = dir.path().join("out.lens");
        let artifacts = vec![
            BundleArtifact {
                name: "ok.json".to_string(),
                data: b"{}".to_vec(),
            },
            BundleArtifact {
                name: "bad/../name".to_string(),
                data: b"{}".to_vec(),
            },
        ];
        assert!(create_bundle_archive(&out, "x", &artifacts, vec![]).is_err());
        assert!(!out.exists());
        assert!(!dir.path().join("out.lens.tmp").exists());
    }
}
