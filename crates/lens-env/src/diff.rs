use crate::model::{EnvDiff, PackageVersionDelta, PyVenv, ShadowingIssue, DIFF_SCHEMA_V1};
use std::collections::{BTreeSet, HashSet};

pub fn diff_environments(baseline: &PyVenv, candidate: &PyVenv) -> EnvDiff {
    let mut added_packages = Vec::new();
    let mut removed_packages = Vec::new();
    let mut version_changes = Vec::new();

    let all_pkg_names: BTreeSet<&str> = baseline
        .packages
        .keys()
        .chain(candidate.packages.keys())
        .map(String::as_str)
        .collect();

    for name in all_pkg_names {
        match (baseline.packages.get(name), candidate.packages.get(name)) {
            (None, Some(c)) => added_packages.push(c.name.clone()),
            (Some(b), None) => removed_packages.push(b.name.clone()),
            (Some(b), Some(c)) => {
                if b.version != c.version {
                    version_changes.push(PackageVersionDelta {
                        package: b.name.clone(),
                        old_version: b.version.clone(),
                        new_version: c.version.clone(),
                    });
                }
            }
            (None, None) => unreachable!(),
        }
    }

    let base_shadow_set: HashSet<ShadowingIssue> =
        baseline.shadowing_issues.iter().cloned().collect();
    let cand_shadow_set: HashSet<ShadowingIssue> =
        candidate.shadowing_issues.iter().cloned().collect();

    let mut new_shadowing: Vec<ShadowingIssue> = cand_shadow_set
        .difference(&base_shadow_set)
        .cloned()
        .collect();
    new_shadowing.sort_by(|a, b| a.module_name.cmp(&b.module_name));

    EnvDiff {
        schema: DIFF_SCHEMA_V1.to_string(),
        added_packages,
        removed_packages,
        version_changes,
        new_shadowing,
    }
}
