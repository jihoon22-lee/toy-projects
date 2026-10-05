use lens_core::SetDiff;
use std::collections::HashMap;

use crate::model::*;

pub fn diff_test_runs(baseline: &TestRun, candidate: &TestRun) -> TestDiff {
    let baseline_map: HashMap<&str, &TestCase> = baseline
        .cases
        .iter()
        .map(|c| (c.identity.as_str(), c))
        .collect();

    let candidate_map: HashMap<&str, &TestCase> = candidate
        .cases
        .iter()
        .map(|c| (c.identity.as_str(), c))
        .collect();

    let mut regressions = Vec::new();
    let mut fixes = Vec::new();

    for (&identity, &c_case) in &candidate_map {
        if let Some(&b_case) = baseline_map.get(identity) {
            let was_success = b_case.status == TestStatus::Passed;
            let is_success = c_case.status == TestStatus::Passed;

            if was_success
                && (c_case.status == TestStatus::Failed || c_case.status == TestStatus::Error)
            {
                regressions.push(format!("REGRESSION: {} failed in candidate", identity));
            } else if !was_success && is_success {
                fixes.push(format!("FIX: {} recovered in candidate", identity));
            }
        }
    }

    regressions.sort();
    fixes.sort();

    let baseline_ids: Vec<String> = baseline.cases.iter().map(|c| c.identity.clone()).collect();
    let candidate_ids: Vec<String> = candidate.cases.iter().map(|c| c.identity.clone()).collect();
    let cases_diff = SetDiff::compute(baseline_ids, candidate_ids);

    let failed_delta = (candidate.summary.failed + candidate.summary.errors) as i64
        - (baseline.summary.failed + baseline.summary.errors) as i64;
    let passed_delta = candidate.summary.passed as i64 - baseline.summary.passed as i64;
    let duration_delta_sec = candidate.summary.duration_sec - baseline.summary.duration_sec;

    TestDiff {
        schema: DIFF_SCHEMA_V1.to_string(),
        baseline_run_id: baseline.run_id.clone(),
        candidate_run_id: candidate.run_id.clone(),
        regressions,
        fixes,
        cases: cases_diff,
        summary_delta: SummaryDelta {
            failed_delta,
            passed_delta,
            duration_delta_sec,
        },
    }
}
