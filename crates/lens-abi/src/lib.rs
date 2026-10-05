//! # Lens ABI (`lens-abi`)
//!
//! Linux ELF binary inspection, dynamic symbol surface tracking, and ABI diffing engine.
//!
//! Re-architects and replaces legacy `abilens` with:
//! - **High-Performance ELF Parser ([`elf::inspect_elf`])**: Zero-copy parsing via `object`.
//! - **Unbounded Version Namespaces**: Automatically extracts dynamic symbol version definitions beyond `GLIBC*`.
//! - **3-State Compatibility Engine ([`diff::diff_reports`])**: Evaluates `compatible`, `incompatible`, and `uncertain` fail-closed status.
//! - **DWARF Type Surface ([`dwarf::extract_dwarf_types`])**: Extracts declared type names from `.debug_info` when present.
//! - **Schema V2 ([`model::REPORT_SCHEMA_V2`], [`model::DIFF_SCHEMA_V2`])**: 100% compliant with existing contracts.

pub mod diff;
pub mod dwarf;
pub mod elf;
pub mod model;

pub use diff::diff_reports;
pub use elf::inspect_elf;
pub use model::{DiffReport, ElfReport, InputStatus, DIFF_SCHEMA_V2, REPORT_SCHEMA_V2};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_non_elf_file() {
        let fake_data = b"This is a plain text file, not ELF";
        let report = inspect_elf("sample.txt", fake_data);
        assert_eq!(report.status, InputStatus::NonElf);
        assert_eq!(report.schema, REPORT_SCHEMA_V2);
    }

    #[test]
    fn test_inspect_real_binary() {
        // Inspect the current running binary (cargo/test runner itself)
        if let Ok(current_exe) = std::env::current_exe() {
            if let Ok(bytes) = std::fs::read(&current_exe) {
                let report = inspect_elf(&current_exe, &bytes);
                assert_eq!(report.status, InputStatus::Valid);
                assert_eq!(report.elf.class, "ELF64");
                assert!(!report.elf.endian.is_empty());
            }
        }
    }

    #[test]
    fn test_diff_reports() {
        let fake_elf_a =
            b"\x7fELF\x02\x01\x01\0\0\0\0\0\0\0\0\0\x02\0\x3e\0\x01\0\0\0\0\0\0\0\0\0\0\0";
        let mut report_a = inspect_elf("lib_v1.so", fake_elf_a);
        report_a.status = InputStatus::Valid;
        report_a.abi.symbols = vec!["func_a".to_string(), "func_b".to_string()];

        let mut report_b = report_a.clone();
        report_b.input = "lib_v2.so".to_string();
        // Remove func_a in v2 (breaking change)
        report_b.abi.symbols = vec!["func_b".to_string(), "func_c".to_string()];

        let diff = diff_reports(&report_a, &report_b);
        assert!(diff.changed);
        assert!(!diff.compatible);
        assert_eq!(diff.compatibility, lens_core::Compatibility::Incompatible);
        assert_eq!(diff.symbols.removed, vec!["func_a"]);
        assert_eq!(diff.symbols.added, vec!["func_c"]);
    }
}
