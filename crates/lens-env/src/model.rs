use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

pub const SNAPSHOT_SCHEMA_V1: &str = "envlens.snapshot/v1";
pub const DIFF_SCHEMA_V1: &str = "envlens.diff/v1";

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct PyPackage {
    pub name: String,
    pub version: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub summary: Option<String>,
    #[serde(default)]
    pub requires_dist: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub dist_info: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Hash)]
pub struct ShadowingIssue {
    pub module_name: String,
    pub local_path: String,
    pub shadows: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PyVenv {
    pub path: String,
    pub python_version: String,
    pub home: String,
    pub packages: BTreeMap<String, PyPackage>,
    pub missing_dependencies: Vec<String>,
    /// Requirements whose version specifier conflicts with the
    /// installed version (package present but out of range).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub version_conflicts: Vec<String>,
    /// Requirements whose environment marker or specifier could not be
    /// evaluated statically — reported as uncertain, not missing.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub unevaluated_dependencies: Vec<String>,
    pub shadowing_issues: Vec<ShadowingIssue>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EnvSnapshot {
    pub schema: String,
    pub version: String,
    pub venv: PyVenv,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct PackageVersionDelta {
    pub package: String,
    pub old_version: String,
    pub new_version: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EnvDiff {
    pub schema: String,
    pub added_packages: Vec<String>,
    pub removed_packages: Vec<String>,
    pub version_changes: Vec<PackageVersionDelta>,
    pub new_shadowing: Vec<ShadowingIssue>,
}
