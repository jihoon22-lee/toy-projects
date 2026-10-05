use lens_core::{Compatibility, SetDiff};
use std::collections::HashMap;

use crate::model::*;

pub fn diff_reports(left: &ElfReport, right: &ElfReport) -> DiffReport {
    let mut header_changes = Vec::new();

    if left.elf.class != right.elf.class {
        header_changes.push(format!(
            "ELF class: {} -> {}",
            left.elf.class, right.elf.class
        ));
    }
    if left.elf.endian != right.elf.endian {
        header_changes.push(format!(
            "endianness: {} -> {}",
            left.elf.endian, right.elf.endian
        ));
    }
    if left.elf.machine != right.elf.machine {
        header_changes.push(format!(
            "machine: {} -> {}",
            left.elf.machine, right.elf.machine
        ));
    }
    if left.elf.elf_type != right.elf.elf_type {
        header_changes.push(format!(
            "type: {} -> {}",
            left.elf.elf_type, right.elf.elf_type
        ));
    }
    if left.elf.dynamic != right.elf.dynamic {
        header_changes.push("linkage: dynamic/static changed".to_string());
    }

    let needed_diff = SetDiff::compute(
        left.dependencies.needed.clone(),
        right.dependencies.needed.clone(),
    );
    let rpath_diff = SetDiff::compute(
        left.dependencies.rpath.clone(),
        right.dependencies.rpath.clone(),
    );
    let runpath_diff = SetDiff::compute(
        left.dependencies.runpath.clone(),
        right.dependencies.runpath.clone(),
    );

    let symbols_diff = SetDiff::compute(left.abi.symbols.clone(), right.abi.symbols.clone());
    let vtables_diff = SetDiff::compute(left.abi.vtables.clone(), right.abi.vtables.clone());
    let types_diff = SetDiff::compute(left.abi.types.clone(), right.abi.types.clone());

    // Symbol attributes diff (size, type, binding)
    let left_evidence_map: HashMap<&str, &SymbolEvidence> = left
        .evidence
        .iter()
        .map(|s| (s.identity.as_str(), s))
        .collect();

    let mut symbol_changes = Vec::new();
    let mut breaking_attribute = false;

    for r_sym in &right.evidence {
        if let Some(l_sym) = left_evidence_map.get(r_sym.identity.as_str()) {
            let mut diffs = Vec::new();
            if l_sym.symbol_type != r_sym.symbol_type {
                diffs.push("type");
                breaking_attribute = true;
            }
            if l_sym.binding != r_sym.binding {
                diffs.push("binding");
            }
            // If data object size changed, ABI layout broken
            if l_sym.size != r_sym.size
                && (r_sym.symbol_type == "OBJECT" || r_sym.symbol_type == "COMMON")
            {
                diffs.push("size");
                breaking_attribute = true;
            }

            if !diffs.is_empty() {
                symbol_changes.push(format!("{}: {}", r_sym.identity, diffs.join(" ")));
            }
        }
    }

    let both_valid = left.status == InputStatus::Valid && right.status == InputStatus::Valid;
    let mut diagnostics = Vec::new();

    // 3-state compatibility evaluation
    let mut incompatible = false;
    let mut uncertain = false;

    if !both_valid {
        incompatible = true;
        diagnostics.push("One or both inputs are invalid ELF files".to_string());
    } else {
        if !header_changes.is_empty() {
            incompatible = true;
        }
        if !symbols_diff.removed.is_empty() {
            incompatible = true;
            diagnostics.push(format!(
                "Exported symbols removed: {}",
                symbols_diff.removed.len()
            ));
        }
        if !vtables_diff.removed.is_empty() {
            incompatible = true;
            diagnostics.push(format!(
                "Virtual tables removed: {}",
                vtables_diff.removed.len()
            ));
        }
        if breaking_attribute {
            incompatible = true;
        }
        if needed_diff.has_changed() || rpath_diff.has_changed() || runpath_diff.has_changed() {
            uncertain = true;
            diagnostics.push("Dynamic loader search paths or dependencies modified".to_string());
        }
    }

    let compatibility = if incompatible {
        Compatibility::Incompatible
    } else if uncertain {
        Compatibility::Uncertain
    } else {
        Compatibility::Compatible
    };

    let changed = !header_changes.is_empty()
        || needed_diff.has_changed()
        || rpath_diff.has_changed()
        || runpath_diff.has_changed()
        || symbols_diff.has_changed()
        || vtables_diff.has_changed()
        || types_diff.has_changed()
        || !symbol_changes.is_empty();

    DiffReport {
        schema: DIFF_SCHEMA_V2.to_string(),
        left: left.input.clone(),
        right: right.input.clone(),
        changed,
        compatible: compatibility.is_compatible(),
        compatibility,
        left_status: format!("{:?}", left.status).to_lowercase(),
        right_status: format!("{:?}", right.status).to_lowercase(),
        header_changes,
        dependencies: DiffDependencies {
            needed: needed_diff,
            rpath: rpath_diff,
            runpath: runpath_diff,
        },
        symbols: symbols_diff,
        vtables: vtables_diff,
        abi: SetDiff::default(),
        types: types_diff,
        symbol_changes,
        diagnostics,
    }
}
