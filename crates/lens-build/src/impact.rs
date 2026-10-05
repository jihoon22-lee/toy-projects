use crate::model::{ImpactReport, ParsedUnit};
use std::collections::{BTreeMap, BTreeSet, HashSet, VecDeque};
use std::path::Path;

/// Upper bound on files scanned while resolving transitive includes for a
/// single translation unit — guards against include cycles blowing up the
/// traversal on pathological trees.
const MAX_SCANNED_FILES: usize = 4096;

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

    /// Scan `unit`'s source file on disk, resolve every `#include` against the
    /// unit's search paths, add direct unit->header edges, then recursively
    /// scan resolved headers to build the header->header transitive graph.
    pub fn add_translation_unit(&mut self, unit: &ParsedUnit) {
        let include_dirs: Vec<&str> = unit.includes.iter().map(|s| s.as_str()).collect();
        let mut visited: HashSet<String> = HashSet::new();
        let mut queue: VecDeque<String> = VecDeque::new();
        queue.push_back(unit.file.clone());
        visited.insert(unit.file.clone());

        // -include headers behave as if the source included them first.
        for inc in &unit.forced_includes {
            if let Some(resolved) = resolve_include(inc, true, &unit.file, &include_dirs) {
                self.add_unit_include(&unit.file, &resolved);
                if visited.insert(resolved.clone()) {
                    queue.push_back(resolved);
                }
            }
        }

        let mut scanned = 0usize;
        while let Some(current) = queue.pop_front() {
            if scanned >= MAX_SCANNED_FILES {
                break;
            }
            let Ok(content) = std::fs::read_to_string(&current) else {
                continue;
            };
            scanned += 1;
            for inc in extract_includes(&content) {
                let Some(resolved) =
                    resolve_include(&inc.name, inc.quoted, &current, &include_dirs)
                else {
                    continue;
                };
                if current == unit.file {
                    self.add_unit_include(&unit.file, &resolved);
                } else {
                    self.add_header_include(&current, &resolved);
                }
                if visited.insert(resolved.clone()) {
                    queue.push_back(resolved);
                }
            }
        }
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

/// An `#include` directive with its spelling preserved: `quoted` is true for
/// `"..."` form, false for `<...>` form.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct IncludeDirective {
    pub name: String,
    pub quoted: bool,
}

pub fn extract_includes(source_code: &str) -> Vec<IncludeDirective> {
    let mut includes = Vec::new();

    for line in source_code.lines() {
        let trimmed = line.trim();
        if let Some(after_hash) = trimmed.strip_prefix('#') {
            let after_hash = after_hash.trim();
            if let Some(rest) = after_hash.strip_prefix("include") {
                let rest = rest.trim();
                let (quoted, inner) = if rest.starts_with('"') && rest.ends_with('"') {
                    (true, &rest[1..rest.len() - 1])
                } else if rest.starts_with('<') && rest.ends_with('>') {
                    (false, &rest[1..rest.len() - 1])
                } else {
                    continue;
                };
                let inner = inner.trim();
                if !inner.is_empty() {
                    includes.push(IncludeDirective {
                        name: inner.to_string(),
                        quoted,
                    });
                }
            }
        }
    }

    includes
}

/// Resolve an include name to an existing on-disk path.
///
/// Quoted includes search the including file's directory first (per the C/C++
/// standard), then the `-I`/`-isystem` search dirs; angle-bracket includes
/// only search the search dirs. Returns `None` when the header cannot be
/// found — system headers outside the project are expected to miss.
fn resolve_include(
    name: &str,
    quoted: bool,
    including_file: &str,
    include_dirs: &[&str],
) -> Option<String> {
    if quoted {
        if let Some(dir) = Path::new(including_file).parent() {
            let candidate = dir.join(name);
            if candidate.is_file() {
                return Some(candidate.to_string_lossy().into_owned());
            }
        }
    }
    for dir in include_dirs {
        let candidate = Path::new(dir).join(name);
        if candidate.is_file() {
            return Some(candidate.to_string_lossy().into_owned());
        }
    }
    None
}
