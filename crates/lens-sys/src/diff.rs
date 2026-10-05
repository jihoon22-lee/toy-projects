use crate::model::*;
use std::collections::{BTreeSet, HashSet};

pub fn diff_systemd(baseline: &SystemdSnapshot, candidate: &SystemdSnapshot) -> SystemdDiff {
    let mut added_units = Vec::new();
    let mut removed_units = Vec::new();
    let mut modified_units = Vec::new();

    let all_unit_names: BTreeSet<String> = baseline
        .units
        .keys()
        .chain(candidate.units.keys())
        .cloned()
        .collect();

    for name in all_unit_names {
        match (baseline.units.get(&name), candidate.units.get(&name)) {
            (None, Some(_)) => {
                added_units.push(name);
            }
            (Some(_), None) => {
                removed_units.push(name);
            }
            (Some(b), Some(c)) => {
                let mut details = Vec::new();

                if b.exec_start != c.exec_start {
                    details.push(format!(
                        "ExecStart changed from {:?} to {:?}",
                        b.exec_start, c.exec_start
                    ));
                }

                if b.wants != c.wants {
                    details.push(format!("Wants changed from {:?} to {:?}", b.wants, c.wants));
                }

                if b.requires != c.requires {
                    details.push(format!(
                        "Requires changed from {:?} to {:?}",
                        b.requires, c.requires
                    ));
                }

                if b.before != c.before {
                    details.push(format!(
                        "Before changed from {:?} to {:?}",
                        b.before, c.before
                    ));
                }

                if b.after != c.after {
                    details.push(format!("After changed from {:?} to {:?}", b.after, c.after));
                }

                if b.drop_ins != c.drop_ins {
                    details.push(format!(
                        "Drop-ins changed from {:?} to {:?}",
                        b.drop_ins, c.drop_ins
                    ));
                }

                if !details.is_empty() {
                    modified_units.push(UnitChange {
                        unit: name,
                        kind: "modified".to_string(),
                        details,
                    });
                }
            }
            (None, None) => unreachable!(),
        }
    }

    let base_cycles_set: HashSet<Vec<String>> = baseline.cycles.iter().cloned().collect();
    let cand_cycles_set: HashSet<Vec<String>> = candidate.cycles.iter().cloned().collect();

    let mut new_cycles: Vec<Vec<String>> = cand_cycles_set
        .difference(&base_cycles_set)
        .cloned()
        .collect();
    new_cycles.sort();

    let mut resolved_cycles: Vec<Vec<String>> = base_cycles_set
        .difference(&cand_cycles_set)
        .cloned()
        .collect();
    resolved_cycles.sort();

    SystemdDiff {
        schema: DIFF_SCHEMA_V1.to_string(),
        added_units,
        removed_units,
        modified_units,
        new_cycles,
        resolved_cycles,
    }
}
