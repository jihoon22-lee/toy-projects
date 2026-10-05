use lens_core::SetDiff;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

pub const RUN_SCHEMA_V1: &str = "testlens.run/v1";
pub const DIFF_SCHEMA_V1: &str = "testlens.diff/v1";

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

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TestDiff {
    pub schema: String,
    pub baseline_run_id: String,
    pub candidate_run_id: String,
    pub regressions: Vec<String>,
    pub fixes: Vec<String>,
    pub cases: SetDiff<String>,
    pub summary_delta: SummaryDelta,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct SummaryDelta {
    pub failed_delta: i64,
    pub passed_delta: i64,
    pub duration_delta_sec: f64,
}
