use object::{Object, ObjectSymbol, SymbolKind, SymbolScope};
use std::path::Path;

use crate::model::*;

pub fn demangle_symbol(name: &str) -> Option<String> {
    let raw_name = name.split_once('@').map(|(s, _)| s).unwrap_or(name);

    if let Ok(sym) = cpp_demangle::Symbol::new(raw_name) {
        if let Ok(demangled) = sym.demangle(&cpp_demangle::DemangleOptions::default()) {
            return Some(demangled);
        }
    }

    let rust_demangled = rustc_demangle::demangle(raw_name).to_string();
    if rust_demangled != raw_name {
        Some(rust_demangled)
    } else {
        None
    }
}

pub fn inspect_elf<P: AsRef<Path>>(path: P, data: &[u8]) -> ElfReport {
    let input_path = path.as_ref().to_string_lossy().into_owned();

    // Check ELF magic
    if data.len() < 4 || &data[0..4] != b"\x7fELF" {
        return ElfReport {
            schema: REPORT_SCHEMA_V2.to_string(),
            input: input_path,
            status: InputStatus::NonElf,
            message: "file is not an ELF binary".to_string(),
            tool: ToolInfo {
                name: "abilens".to_string(),
                version: "0.3.0".to_string(),
            },
            elf: ElfHeaderInfo::default(),
            dependencies: Dependencies::default(),
            abi: AbiData {
                versions: Vec::new(),
                symbols: Vec::new(),
                vtables: Vec::new(),
                types: Vec::new(),
            },
            policy: PolicyEvaluation::default(),
            diagnostics: Vec::new(),
            evidence: Vec::new(),
        };
    }

    let file = match object::File::parse(data) {
        Ok(f) => f,
        Err(e) => {
            return ElfReport {
                schema: REPORT_SCHEMA_V2.to_string(),
                input: input_path,
                status: InputStatus::Corrupt,
                message: format!("ELF parsing failed: {}", e),
                tool: ToolInfo {
                    name: "abilens".to_string(),
                    version: "0.3.0".to_string(),
                },
                elf: ElfHeaderInfo::default(),
                dependencies: Dependencies::default(),
                abi: AbiData {
                    versions: Vec::new(),
                    symbols: Vec::new(),
                    vtables: Vec::new(),
                    types: Vec::new(),
                },
                policy: PolicyEvaluation::default(),
                diagnostics: vec![e.to_string()],
                evidence: Vec::new(),
            };
        }
    };

    let is_64 = file.is_64();
    let class = if is_64 { "ELF64" } else { "ELF32" };
    let endian = if file.is_little_endian() {
        "little-endian"
    } else {
        "big-endian"
    };
    let machine = format!("{:?}", file.architecture());
    let elf_type = format!("{:?}", file.kind());

    let mut needed = Vec::new();
    let mut symbols = Vec::new();
    let mut vtables = Vec::new();
    let mut evidence = Vec::new();

    // Check stripped status: .symtab present?
    let stripped = file.section_by_name(".symtab").is_none();

    // Dynamic symbols
    let mut dynamic = false;
    for sym in file.dynamic_symbols() {
        dynamic = true;
        if let Ok(name) = sym.name() {
            if name.is_empty() {
                continue;
            }

            let is_vtable = name.starts_with("_ZTV");
            if is_vtable {
                vtables.push(name.to_string());
            } else {
                symbols.push(name.to_string());
            }

            let binding = match sym.scope() {
                SymbolScope::Dynamic => "global",
                SymbolScope::Linkage => "weak",
                SymbolScope::Compilation => "local",
                _ => "global",
            };

            let symbol_type = match sym.kind() {
                SymbolKind::Text => "FUNC",
                SymbolKind::Data => "OBJECT",
                SymbolKind::Section => "SECTION",
                SymbolKind::File => "FILE",
                _ => "NOTYPE",
            };

            evidence.push(SymbolEvidence {
                identity: name.to_string(),
                size: sym.size(),
                binding: binding.to_string(),
                visibility: "default".to_string(),
                symbol_type: symbol_type.to_string(),
                default_version: name.contains("@@"),
                demangled: demangle_symbol(name),
            });
        }
    }

    // Dynamic libraries (DT_NEEDED)
    for lib in file.imports().into_iter().flatten() {
        let name = String::from_utf8_lossy(lib.library()).to_string();
        if !name.is_empty() && !needed.contains(&name) {
            needed.push(name);
        }
    }

    symbols.sort();
    symbols.dedup();
    vtables.sort();
    vtables.dedup();
    needed.sort();
    evidence.sort_by(|a, b| a.identity.cmp(&b.identity));

    ElfReport {
        schema: REPORT_SCHEMA_V2.to_string(),
        input: input_path,
        status: InputStatus::Valid,
        message: "ELF inspection completed successfully".to_string(),
        tool: ToolInfo {
            name: "abilens".to_string(),
            version: "0.3.0".to_string(),
        },
        elf: ElfHeaderInfo {
            class: class.to_string(),
            endian: endian.to_string(),
            machine,
            elf_type,
            dynamic,
            stripped,
        },
        dependencies: Dependencies {
            needed,
            rpath: Vec::new(),
            runpath: Vec::new(),
            soname: None,
            interpreter: None,
        },
        abi: AbiData {
            versions: Vec::new(),
            symbols,
            vtables,
            types: Vec::new(),
        },
        policy: PolicyEvaluation {
            applied: false,
            passed: true,
            violations: Vec::new(),
        },
        diagnostics: Vec::new(),
        evidence,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_demangle_symbol() {
        // C++ mangled symbol
        let cpp_mangled = "_ZNSt6vectorIiSaIiEE9push_backERKi";
        let demangled = demangle_symbol(cpp_mangled);
        assert!(demangled.is_some());
        let s = demangled.unwrap();
        assert!(s.contains("std::vector") && s.contains("push_back"));

        // Plain C symbol
        assert_eq!(demangle_symbol("simple_function"), None);
    }
}
