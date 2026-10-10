use lens_core::SetDiff;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

pub const RUN_SCHEMA_V1: &str = "testlens.run/v1";
pub const DIFF_SCHEMA_V2: &str = "testlens.diff/v2";

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum TestStatus {
    Passed,
    Failed,
    Error,
    Skipped,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TestCase {
    pub identity: String,
    pub name: String,
    #[serde(skip_serializing_if = "String::is_empty", default)]
    pub classname: String,
    /// Name of the enclosing `<testsuite>` (or nearest ancestor suite).
    #[serde(skip_serializing_if = "String::is_empty", default)]
    pub suite: String,
    pub status: TestStatus,
    pub duration_sec: f64,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub message: Option<String>,
    /// Captured `system-out`/`system-err` text for this case, if any.
    #[serde(skip_serializing_if = "Option::is_none", default)]
    pub output: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct TestSummary {
    pub total: usize,
    pub passed: usize,
    pub failed: usize,
    pub errors: usize,
    pub skipped: usize,
    pub duration_sec: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TestProducer {
    pub name: String,
    pub version: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TestRun {
    pub schema: String,
    pub producer: TestProducer,
    pub run_id: String,
    pub project: String,
    pub collected_at: String,
    pub complete: bool,
    pub summary: TestSummary,
    pub cases: Vec<TestCase>,
    /// `<properties>` entries declared on the suite/case level.
    #[serde(default, skip_serializing_if = "BTreeMap::is_empty")]
    pub properties: BTreeMap<String, String>,
    /// Suite-level `<system-out>`/`<system-err>` text (outside testcases).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub suite_output: Option<String>,
}

/// A structured per-case status transition in a diff (`before`/`after`
/// are `null` for cases present on only one side).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CaseChange {
    pub id: String,
    pub before: Option<TestStatus>,
    pub after: Option<TestStatus>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub message: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TestDiff {
    pub schema: String,
    pub baseline_run_id: String,
    pub candidate_run_id: String,
    /// passed -> failed/error transitions.
    pub regressions: Vec<CaseChange>,
    /// failed/error -> passed transitions (skipped transitions are not fixes).
    pub fixes: Vec<CaseChange>,
    /// Cases absent from the baseline that fail/error in the candidate.
    pub new_failures: Vec<CaseChange>,
    /// Cases present in the baseline but absent from the candidate.
    pub removed_tests: Vec<CaseChange>,
    /// Transitions to or from `skipped`.
    pub skipped_changes: Vec<CaseChange>,
    pub cases: SetDiff<String>,
    pub summary_delta: SummaryDelta,
    /// Non-fatal issues such as duplicate case identities.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub diagnostics: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct SummaryDelta {
    pub failed_delta: i64,
    pub passed_delta: i64,
    pub duration_delta_sec: f64,
}
