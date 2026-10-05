use crate::model::{BuildDiff, ParsedUnit, UnitDiff, DIFF_SCHEMA_V1};
use std::collections::{BTreeMap, BTreeSet};

pub fn diff_compilations(baseline: &[ParsedUnit], candidate: &[ParsedUnit]) -> BuildDiff {
    let base_map: BTreeMap<&str, &ParsedUnit> =
        baseline.iter().map(|u| (u.file.as_str(), u)).collect();
    let cand_map: BTreeMap<&str, &ParsedUnit> =
        candidate.iter().map(|u| (u.file.as_str(), u)).collect();

    let all_files: BTreeSet<&str> = base_map.keys().chain(cand_map.keys()).copied().collect();

    let mut added_units = Vec::new();
    let mut removed_units = Vec::new();
    let mut modified_units = Vec::new();

    for file in all_files {
        match (base_map.get(file), cand_map.get(file)) {
            (None, Some(_)) => added_units.push(file.to_string()),
            (Some(_), None) => removed_units.push(file.to_string()),
            (Some(b), Some(c)) => {
                let base_flags: BTreeSet<&str> = b.flags.iter().map(String::as_str).collect();
                let cand_flags: BTreeSet<&str> = c.flags.iter().map(String::as_str).collect();

                let added_flags: Vec<String> = cand_flags
                    .difference(&base_flags)
                    .map(|s| s.to_string())
                    .collect();
                let removed_flags: Vec<String> = base_flags
                    .difference(&cand_flags)
                    .map(|s| s.to_string())
                    .collect();

                let base_defs: BTreeSet<&str> = b.defines.iter().map(String::as_str).collect();
                let cand_defs: BTreeSet<&str> = c.defines.iter().map(String::as_str).collect();

                let added_defines: Vec<String> = cand_defs
                    .difference(&base_defs)
                    .map(|s| s.to_string())
                    .collect();
                let removed_defines: Vec<String> = base_defs
                    .difference(&cand_defs)
                    .map(|s| s.to_string())
                    .collect();

                let base_incs: BTreeSet<&str> = b.includes.iter().map(String::as_str).collect();
                let cand_incs: BTreeSet<&str> = c.includes.iter().map(String::as_str).collect();

                let added_includes: Vec<String> = cand_incs
                    .difference(&base_incs)
                    .map(|s| s.to_string())
                    .collect();
                let removed_includes: Vec<String> = base_incs
                    .difference(&cand_incs)
                    .map(|s| s.to_string())
                    .collect();

                if !added_flags.is_empty()
                    || !removed_flags.is_empty()
                    || !added_defines.is_empty()
                    || !removed_defines.is_empty()
                    || !added_includes.is_empty()
                    || !removed_includes.is_empty()
                {
                    modified_units.push(UnitDiff {
                        file: file.to_string(),
                        added_flags,
                        removed_flags,
                        added_defines,
                        removed_defines,
                        added_includes,
                        removed_includes,
                    });
                }
            }
            (None, None) => unreachable!(),
        }
    }

    BuildDiff {
        schema: DIFF_SCHEMA_V1.to_string(),
        added_units,
        removed_units,
        modified_units,
    }
}
