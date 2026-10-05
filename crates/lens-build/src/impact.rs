use crate::model::ImpactReport;
use std::collections::{BTreeMap, BTreeSet, HashSet, VecDeque};

#[derive(Debug, Clone, Default)]
pub struct ImpactGraph {
    /// header -> set of translation units directly depending on it
    pub header_to_units: BTreeMap<String, BTreeSet<String>>,
    /// header -> set of headers it includes
    pub header_to_headers: BTreeMap<String, BTreeSet<String>>,
}

impl ImpactGraph {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn add_unit_include(&mut self, unit_file: &str, header_path: &str) {
        self.header_to_units
            .entry(header_path.to_string())
            .or_default()
            .insert(unit_file.to_string());
    }

    pub fn add_header_include(&mut self, parent_header: &str, included_header: &str) {
        self.header_to_headers
            .entry(parent_header.to_string())
            .or_default()
            .insert(included_header.to_string());
    }

    /// Find all translation units impacted by changes to `target_header`.
    /// Transitive impact: if A.cpp includes B.h, and B.h includes C.h, modifying C.h impacts A.cpp.
    pub fn compute_impact(&self, target_header: &str) -> ImpactReport {
        // First, find all headers that directly or transitively include target_header.
        // We need the reverse of header_to_headers: included_header -> parent_headers
        let mut reverse_headers: BTreeMap<&str, Vec<&str>> = BTreeMap::new();
        for (parent, includes) in &self.header_to_headers {
            for inc in includes {
                reverse_headers
                    .entry(inc.as_str())
                    .or_default()
                    .push(parent.as_str());
            }
        }

        let mut visited_headers = HashSet::new();
        let mut queue = VecDeque::new();
        queue.push_back(target_header);
        visited_headers.insert(target_header);

        let mut all_impacted_headers = Vec::new();
        while let Some(h) = queue.pop_front() {
            all_impacted_headers.push(h);
            if let Some(parents) = reverse_headers.get(h) {
                for &p in parents {
                    if visited_headers.insert(p) {
                        queue.push_back(p);
                    }
                }
            }
        }

        let mut impacted_units = BTreeSet::new();
        for h in all_impacted_headers {
            if let Some(units) = self.header_to_units.get(h) {
                for u in units {
                    impacted_units.insert(u.clone());
                }
            }
        }

        let impacted_vec: Vec<String> = impacted_units.into_iter().collect();
        let total = impacted_vec.len();

        ImpactReport {
            schema: crate::model::IMPACT_SCHEMA_V1.to_string(),
            target_header: target_header.to_string(),
            impacted_units: impacted_vec,
            total_impacted: total,
        }
    }
}

pub fn extract_includes(source_code: &str) -> Vec<String> {
    let mut includes = Vec::new();

    for line in source_code.lines() {
        let trimmed = line.trim();
        if let Some(after_hash) = trimmed.strip_prefix('#') {
            let after_hash = after_hash.trim();
            if let Some(rest) = after_hash.strip_prefix("include") {
                let rest = rest.trim();
                if (rest.starts_with('"') && rest.ends_with('"'))
                    || (rest.starts_with('<') && rest.ends_with('>'))
                {
                    let inner = &rest[1..rest.len() - 1].trim();
                    if !inner.is_empty() {
                        includes.push(inner.to_string());
                    }
                }
            }
        }
    }

    includes
}
