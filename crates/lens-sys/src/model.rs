use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

pub const SNAPSHOT_SCHEMA_V1: &str = "servicelens.snapshot/v1";
pub const DIFF_SCHEMA_V1: &str = "servicelens.diff/v1";

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Diagnostic {
    pub code: String,
    pub severity: String,
    pub message: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub path: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub line: Option<usize>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default, PartialEq, Eq)]
pub struct SystemdUnit {
    pub name: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub path: Option<String>,
    pub sections: BTreeMap<String, BTreeMap<String, Vec<String>>>,
    #[serde(default)]
    pub wants: Vec<String>,
    #[serde(default)]
    pub requires: Vec<String>,
    #[serde(default)]
    pub before: Vec<String>,
    #[serde(default)]
    pub after: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub exec_start: Option<String>,
    #[serde(default)]
    pub drop_ins: Vec<String>,
    #[serde(default)]
    pub diagnostics: Vec<Diagnostic>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SystemdSnapshot {
    pub schema: String,
    pub version: String,
    pub semantics: String,
    pub units: BTreeMap<String, SystemdUnit>,
    pub cycles: Vec<Vec<String>>,
    pub diagnostics: Vec<Diagnostic>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct UnitChange {
    pub unit: String,
    pub kind: String, // "added", "removed", "modified"
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub details: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SystemdDiff {
    pub schema: String,
    pub added_units: Vec<String>,
    pub removed_units: Vec<String>,
    pub modified_units: Vec<UnitChange>,
    pub new_cycles: Vec<Vec<String>>,
    pub resolved_cycles: Vec<Vec<String>>,
}
