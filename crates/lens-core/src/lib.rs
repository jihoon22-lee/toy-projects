//! # Lens Core (`lens-core`)
//!
//! Foundation library for the Unified Systems Diagnostics and Forensic Platform.
//!
//! This crate establishes the shared invariants across all diagnostic domain modules:
//! - **Identity ([`identity`])**: POSIX file metadata, Inode tracking, and TOCTOU race condition defense.
//! - **Hash ([`hash`])**: Standard SHA-256 hashing and streaming digest calculations.
//! - **Diff ([`diff`])**: Conservative 3-state compatibility model (`Compatible`, `Incompatible`, `Uncertain`).
//! - **Evidence ([`evidence`])**: Source provenance, exact observation coordinates, and bounded diagnostic collectors.
//! - **JSON ([`json`])**: Deterministic canonical JSON serialization with guaranteed key sorting.
//! - **Error ([`error`])**: Unified typed error handling.

pub mod diff;
pub mod error;
pub mod evidence;
pub mod hash;
pub mod identity;
pub mod json;

pub use diff::{Compatibility, SetDiff};
pub use error::{LensError, Result};
pub use evidence::{
    verify_bundle_archive, BoundedCollector, BundleVerificationReport, Diagnostic, Evidence, Source,
};
pub use hash::{digest_bytes, digest_file, digest_reader, IncrementalHasher};
pub use identity::{FileIdentity, SafeInput};
pub use json::{to_deterministic_pretty, to_deterministic_string};
