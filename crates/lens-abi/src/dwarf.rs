//! Minimal DWARF `.debug_info` extraction: collects the names of declared
//! types (structs, classes, unions, enums, typedefs) so `abi.types` is real
//! evidence instead of an empty placeholder. Bounded and best-effort —
//! stripped binaries simply yield no types and a diagnostic note.

use gimli::Reader;
use object::{Object, ObjectSection};
use std::collections::BTreeSet;
use std::rc::Rc;

/// Upper bound on extracted type names to keep reports bounded.
const MAX_TYPES: usize = 10_000;

type GimliReader = gimli::EndianRcSlice<gimli::RunTimeEndian>;

/// Extract declared type names from a parsed object file.
/// Returns `(types, diagnostic)`.
pub fn extract_dwarf_types(file: &object::File) -> (Vec<String>, Option<String>) {
    if file
        .section_by_name(".debug_info")
        .map(|s| s.size() == 0)
        .unwrap_or(true)
    {
        return (
            Vec::new(),
            Some("no .debug_info section (stripped or built without -g)".to_string()),
        );
    }

    let endian = if file.is_little_endian() {
        gimli::RunTimeEndian::Little
    } else {
        gimli::RunTimeEndian::Big
    };

    let dwarf = gimli::Dwarf::load(|id| -> Result<GimliReader, gimli::Error> {
        let data: Vec<u8> = file
            .section_by_name(id.name())
            .and_then(|s| s.data().ok())
            .map(|c| c.to_vec())
            .unwrap_or_default();
        Ok(gimli::EndianRcSlice::new(Rc::from(data), endian))
    });

    let dwarf = match dwarf {
        Ok(d) => d,
        Err(e) => {
            return (
                Vec::new(),
                Some(format!("failed to load DWARF sections: {}", e)),
            )
        }
    };

    let mut types: BTreeSet<String> = BTreeSet::new();
    let mut units = dwarf.units();
    let mut truncated = false;

    while let Ok(Some(header)) = units.next() {
        let unit = match dwarf.unit(header) {
            Ok(u) => u,
            Err(_) => continue,
        };
        let mut entries = unit.entries();
        loop {
            match entries.next_entry() {
                Ok(Some(())) => {
                    let entry = match entries.current() {
                        Some(e) => e,
                        None => continue,
                    };
                    if matches!(
                        entry.tag(),
                        gimli::DW_TAG_structure_type
                            | gimli::DW_TAG_class_type
                            | gimli::DW_TAG_union_type
                            | gimli::DW_TAG_enumeration_type
                            | gimli::DW_TAG_typedef
                    ) {
                        if let Some(attr) = entry.attr(gimli::DW_AT_name).ok().flatten() {
                            if let Ok(reader) = dwarf.attr_string(&unit, attr.value()) {
                                if let Ok(name) = reader.to_string() {
                                    if !name.is_empty() {
                                        types.insert(name.into_owned());
                                    }
                                }
                            }
                        }
                    }
                    if types.len() >= MAX_TYPES {
                        truncated = true;
                        break;
                    }
                }
                Ok(None) => break,
                Err(_) => break,
            }
        }
        if truncated {
            break;
        }
    }

    let diagnostic = if truncated {
        Some(format!("type list truncated at {} entries", MAX_TYPES))
    } else if types.is_empty() {
        Some(".debug_info present but contained no named types".to_string())
    } else {
        None
    };

    (types.into_iter().collect(), diagnostic)
}
