use object::{Object, ObjectSection, ObjectSymbol, SymbolKind, SymbolScope};
use std::path::Path;

use crate::dwarf::extract_dwarf_types;
use crate::model::*;

/// Parsed ELF_DYNAMIC payload: loader search paths, SONAME, interpreter, and
/// symbol version definitions/requirements.
#[derive(Default)]
struct DynamicInfo {
    rpath: Vec<String>,
    runpath: Vec<String>,
    soname: Option<String>,
    interpreter: Option<String>,
    /// (library, version) pairs from .gnu.version_r / .gnu.version_d
    versions: Vec<VersionRequirement>,
}

const DT_STRTAB: i64 = 5;
const DT_SONAME: i64 = 14;
const DT_RPATH: i64 = 15;
const DT_RUNPATH: i64 = 29;
const DT_VERDEF: i64 = 0x6ffffffc;
const DT_VERDEFNUM: i64 = 0x6ffffffd;
const DT_VERNEED: i64 = 0x6ffffffe;
const DT_VERNEEDNUM: i64 = 0x6fffffff;

fn rd_u16(d: &[u8], off: usize, le: bool) -> Option<u64> {
    let b: [u8; 2] = d.get(off..off + 2)?.try_into().ok()?;
    Some(if le {
        u16::from_le_bytes(b) as u64
    } else {
        u16::from_be_bytes(b) as u64
    })
}

fn rd_u32(d: &[u8], off: usize, le: bool) -> Option<u64> {
    let b: [u8; 4] = d.get(off..off + 4)?.try_into().ok()?;
    Some(if le {
        u32::from_le_bytes(b) as u64
    } else {
        u32::from_be_bytes(b) as u64
    })
}

fn rd_dyn(d: &[u8], off: usize, is64: bool, le: bool) -> Option<(i64, u64)> {
    if is64 {
        let tag: [u8; 8] = d.get(off..off + 8)?.try_into().ok()?;
        let val: [u8; 8] = d.get(off + 8..off + 16)?.try_into().ok()?;
        let (t, v) = if le {
            (i64::from_le_bytes(tag), u64::from_le_bytes(val))
        } else {
            (i64::from_be_bytes(tag), u64::from_be_bytes(val))
        };
        Some((t, v))
    } else {
        let tag: [u8; 4] = d.get(off..off + 4)?.try_into().ok()?;
        let val: [u8; 4] = d.get(off + 4..off + 8)?.try_into().ok()?;
        let (t, v) = if le {
            (
                i32::from_le_bytes(tag) as i64,
                u32::from_le_bytes(val) as u64,
            )
        } else {
            (
                i32::from_be_bytes(tag) as i64,
                u32::from_be_bytes(val) as u64,
            )
        };
        Some((t, v))
    }
}

/// Map a virtual address into file bytes via the section that covers it.
fn vaddr_bytes<'a>(file: &'a object::File<'a>, vaddr: u64) -> Option<&'a [u8]> {
    for s in file.sections() {
        let start = s.address();
        let end = start.saturating_add(s.size());
        if s.size() > 0 && vaddr >= start && vaddr < end {
            if let Ok(data) = s.data() {
                let off = (vaddr - start) as usize;
                if off <= data.len() {
                    return Some(&data[off..]);
                }
            }
        }
    }
    None
}

fn cstr_at(d: &[u8], off: usize) -> Option<String> {
    let end = d.get(off..)?.iter().position(|&b| b == 0)? + off;
    Some(String::from_utf8_lossy(&d[off..end]).into_owned())
}

fn parse_verneed(
    file: &object::File,
    base_vaddr: u64,
    count: u64,
    strtab: &[u8],
    le: bool,
    out: &mut Vec<VersionRequirement>,
) {
    // Elf*_Verneed: vn_version@0 u16, vn_cnt@2 u16, vn_file@4 u32,
    //               vn_aux@8 u32, vn_next@12 u32  (16 bytes)
    let mut cur = base_vaddr;
    for _ in 0..count.min(1024) {
        let Some(ent) = vaddr_bytes(file, cur) else {
            break;
        };
        let Some(cnt) = rd_u16(ent, 2, le) else { break };
        let Some(file_off) = rd_u32(ent, 4, le) else {
            break;
        };
        let Some(aux) = rd_u32(ent, 8, le) else { break };
        let Some(next) = rd_u32(ent, 12, le) else {
            break;
        };
        let library = cstr_at(strtab, file_off as usize).unwrap_or_default();

        // Vernaux follows at cur + vn_aux; chain via vna_next.
        let mut aux_cur = cur.saturating_add(aux);
        for _ in 0..cnt.min(1024) {
            let Some(a) = vaddr_bytes(file, aux_cur) else {
                break;
            };
            let Some(name_off) = rd_u32(a, 8, le) else {
                break;
            };
            let Some(anext) = rd_u32(a, 12, le) else {
                break;
            };
            if let Some(version) = cstr_at(strtab, name_off as usize) {
                out.push(VersionRequirement {
                    library: library.clone(),
                    namespace: library.clone(),
                    version,
                });
            }
            if anext == 0 {
                break;
            }
            aux_cur = aux_cur.saturating_add(anext);
        }

        if next == 0 {
            break;
        }
        cur = cur.saturating_add(next);
    }
}

fn parse_verdef(
    file: &object::File,
    base_vaddr: u64,
    count: u64,
    strtab: &[u8],
    le: bool,
    out: &mut Vec<VersionRequirement>,
) {
    // Elf*_Verdef: vd_version@0, vd_flags@2, vd_ndx@4, vd_cnt@6 u16,
    //              vd_hash@8, vd_aux@12, vd_next@16 u32  (20 bytes)
    let mut cur = base_vaddr;
    for _ in 0..count.min(1024) {
        let Some(ent) = vaddr_bytes(file, cur) else {
            break;
        };
        let Some(cnt) = rd_u16(ent, 6, le) else { break };
        let Some(aux) = rd_u32(ent, 12, le) else {
            break;
        };
        let Some(next) = rd_u32(ent, 16, le) else {
            break;
        };

        let mut aux_cur = cur.saturating_add(aux);
        // Verdaux: vda_name@0 u32, vda_next@4 u32 (8 bytes)
        for _ in 0..cnt.min(1024) {
            let Some(a) = vaddr_bytes(file, aux_cur) else {
                break;
            };
            let Some(name_off) = rd_u32(a, 0, le) else {
                break;
            };
            let Some(anext) = rd_u32(a, 4, le) else { break };
            if let Some(version) = cstr_at(strtab, name_off as usize) {
                out.push(VersionRequirement {
                    library: String::new(),
                    namespace: "self".to_string(),
                    version,
                });
            }
            if anext == 0 {
                break;
            }
            aux_cur = aux_cur.saturating_add(anext);
        }

        if next == 0 {
            break;
        }
        cur = cur.saturating_add(next);
    }
}

fn parse_dynamic(file: &object::File, data: &[u8]) -> DynamicInfo {
    let le = file.is_little_endian();
    let is64 = file.is_64();
    let mut info = DynamicInfo {
        interpreter: file
            .section_by_name(".interp")
            .and_then(|s| s.data().ok())
            .and_then(|d| cstr_at(d, 0)),
        ..Default::default()
    };

    let Some(dyn_sec) = file.section_by_name(".dynamic") else {
        return info;
    };
    let Ok(dyn_data) = dyn_sec.data() else {
        return info;
    };

    let entry_size = if is64 { 16 } else { 8 };
    let mut strtab_vaddr = None;
    let mut strtab_size = usize::MAX;
    let mut rpath_off = None;
    let mut runpath_off = None;
    let mut soname_off = None;
    let mut verneed = None;
    let mut verneednum = 0u64;
    let mut verdef = None;
    let mut verdefnum = 0u64;

    for off in (0..dyn_data.len()).step_by(entry_size) {
        let Some((tag, val)) = rd_dyn(dyn_data, off, is64, le) else {
            break;
        };
        match tag {
            DT_STRTAB => strtab_vaddr = Some(val),
            10 => strtab_size = val as usize, // DT_STRSZ
            DT_SONAME => soname_off = Some(val),
            DT_RPATH => rpath_off = Some(val),
            DT_RUNPATH => runpath_off = Some(val),
            DT_VERNEED => verneed = Some(val),
            DT_VERNEEDNUM => verneednum = val,
            DT_VERDEF => verdef = Some(val),
            DT_VERDEFNUM => verdefnum = val,
            0 => break, // DT_NULL
            _ => {}
        }
        let _ = data;
    }

    let strtab: &[u8] = strtab_vaddr
        .and_then(|v| vaddr_bytes(file, v))
        .map(|d| &d[..d.len().min(strtab_size)])
        .or_else(|| file.section_by_name(".dynstr").and_then(|s| s.data().ok()))
        .unwrap_or(&[]);

    if let Some(off) = soname_off {
        info.soname = cstr_at(strtab, off as usize);
    }
    if let Some(off) = rpath_off {
        info.rpath = cstr_at(strtab, off as usize)
            .map(|s| s.split(':').map(|x| x.to_string()).collect())
            .unwrap_or_default();
    }
    if let Some(off) = runpath_off {
        info.runpath = cstr_at(strtab, off as usize)
            .map(|s| s.split(':').map(|x| x.to_string()).collect())
            .unwrap_or_default();
    }

    if let (Some(v), n) = (verneed, verneednum) {
        parse_verneed(file, v, n, strtab, le, &mut info.versions);
    }
    if let (Some(v), n) = (verdef, verdefnum) {
        parse_verdef(file, v, n, strtab, le, &mut info.versions);
    }
    info.versions.sort_by(|a, b| {
        (&a.library, &a.namespace, &a.version).cmp(&(&b.library, &b.namespace, &b.version))
    });
    info.versions.dedup();
    info
}

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

fn tool_info() -> ToolInfo {
    ToolInfo {
        name: "abilens".to_string(),
        version: env!("CARGO_PKG_VERSION").to_string(),
    }
}

fn invalid_report(
    input_path: String,
    status: InputStatus,
    message: String,
    diagnostics: Vec<String>,
) -> ElfReport {
    ElfReport {
        schema: REPORT_SCHEMA_V2.to_string(),
        input: input_path,
        status,
        message,
        tool: tool_info(),
        elf: ElfHeaderInfo::default(),
        dependencies: Dependencies::default(),
        abi: AbiData::default(),
        policy: PolicyEvaluation::default(),
        diagnostics,
        evidence: Vec::new(),
    }
}

pub fn inspect_elf<P: AsRef<Path>>(path: P, data: &[u8]) -> ElfReport {
    let input_path = path.as_ref().to_string_lossy().into_owned();

    // Check ELF magic
    if data.len() < 4 || &data[0..4] != b"\x7fELF" {
        return invalid_report(
            input_path,
            InputStatus::NonElf,
            "file is not an ELF binary".to_string(),
            Vec::new(),
        );
    }

    let file = match object::File::parse(data) {
        Ok(f) => f,
        Err(e) => {
            return invalid_report(
                input_path,
                InputStatus::Corrupt,
                format!("ELF parsing failed: {}", e),
                vec![e.to_string()],
            );
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
                defined: !sym.is_undefined(),
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

    // .dynamic payload: rpath/runpath/soname/interp + symbol versions
    let dyn_info = parse_dynamic(&file, data);

    // DWARF declared types (empty on stripped binaries — recorded below)
    let (types, dwarf_diag) = extract_dwarf_types(&file);
    let mut diagnostics = Vec::new();
    if let Some(d) = dwarf_diag {
        diagnostics.push(d);
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
        tool: tool_info(),
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
            rpath: dyn_info.rpath,
            runpath: dyn_info.runpath,
            soname: dyn_info.soname,
            interpreter: dyn_info.interpreter,
        },
        abi: AbiData {
            versions: dyn_info.versions,
            symbols,
            vtables,
            types,
        },
        policy: PolicyEvaluation {
            applied: false,
            passed: true,
            violations: Vec::new(),
        },
        diagnostics,
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

#[cfg(test)]
mod dyn_tests {
    #[test]
    fn test_dynamic_metadata_parsing() {
        // The test binary itself is a dynamically linked ELF with PT_INTERP
        // and glibc version requirements on this platform.
        let exe = std::env::current_exe().unwrap();
        let bytes = std::fs::read(&exe).unwrap();
        let file = object::File::parse(&*bytes).unwrap();
        let info = super::parse_dynamic(&file, &bytes);
        if file.section_by_name(".interp").is_some() {
            assert!(info.interpreter.is_some());
        }
        if file.section_by_name(".gnu.version_r").is_some() {
            assert!(!info.versions.is_empty());
            assert!(info.versions.iter().all(|v| !v.version.is_empty()));
        }
    }
}
