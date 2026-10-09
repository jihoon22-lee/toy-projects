//! # Lens ABI (`lens-abi`)
//!
//! Linux ELF binary inspection, dynamic symbol surface tracking, and ABI diffing engine.
//!
//! Re-architects and replaces legacy `abilens` with:
//! - **High-Performance ELF Parser ([`elf::inspect_elf`])**: Zero-copy parsing via `object`.
//! - **Unbounded Version Namespaces**: Automatically extracts dynamic symbol version definitions beyond `GLIBC*`.
//! - **3-State Compatibility Engine ([`diff::diff_reports`])**: Evaluates `compatible`, `incompatible`, and `uncertain` fail-closed status.
//! - **DWARF Type Surface ([`dwarf::extract_dwarf_types`])**: Extracts declared type names from `.debug_info` when present.
//! - **Schemas ([`model::REPORT_SCHEMA_V2`], [`model::DIFF_SCHEMA_V3`])**: report stays `abilens.report/v2`; the diff schema is v3.

pub mod diff;
pub mod dwarf;
pub mod elf;
pub mod model;

pub use diff::diff_reports;
pub use elf::inspect_elf;
pub use model::{DiffReport, ElfReport, InputStatus, DIFF_SCHEMA_V3, REPORT_SCHEMA_V2};

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

    #[test]
    fn test_diff_import_only_change_is_compatible() {
        let fake_elf =
            b"\x7fELF\x02\x01\x01\0\0\0\0\0\0\0\0\0\x02\0\x3e\0\x01\0\0\0\0\0\0\0\0\0\0\0";
        let mut a = inspect_elf("app_v1", fake_elf);
        a.status = InputStatus::Valid;
        a.abi.symbols = vec!["run".to_string()];
        a.abi.imports = vec!["malloc".to_string()];

        let mut b = a.clone();
        b.input = "app_v2".to_string();
        // The app now imports a different helper — a dependency change,
        // not a removed export.
        b.abi.imports = vec!["malloc".to_string(), "pthread_create".to_string()];

        let diff = diff_reports(&a, &b);
        assert!(diff.changed);
        assert!(diff.compatible);
        assert_eq!(diff.compatibility, lens_core::Compatibility::Compatible);
        assert_eq!(diff.imports.added, vec!["pthread_create"]);
        assert!(diff.symbols.removed.is_empty());
    }

    #[test]
    fn test_diff_invalid_input_is_uncertain() {
        let report_a = inspect_elf("a.txt", b"not an elf");
        let mut report_b = inspect_elf("b.bin", b"not an elf either");
        report_b.status = InputStatus::Valid;
        let diff = diff_reports(&report_a, &report_b);
        assert_eq!(diff.compatibility, lens_core::Compatibility::Uncertain);
        assert!(!diff.compatible);
    }

    #[test]
    fn test_diff_version_requirements() {
        let fake = b"\x7fELF\x02\x01\x01\0\0\0\0\0\0\0\0\0\x02\0\x3e\0\x01\0\0\0\0\0\0\0\0\0\0\0";
        let mut a = inspect_elf("lib_v1.so", fake);
        a.status = InputStatus::Valid;
        a.abi.versions = vec![model::VersionRequirement {
            library: "libc.so.6".to_string(),
            namespace: "libc.so.6".to_string(),
            version: "GLIBC_2.17".to_string(),
        }];
        let mut b = a.clone();
        b.abi.versions.push(model::VersionRequirement {
            library: "libc.so.6".to_string(),
            namespace: "libc.so.6".to_string(),
            version: "GLIBC_2.38".to_string(),
        });

        let diff = diff_reports(&a, &b);
        assert!(diff.changed);
        assert_eq!(diff.compatibility, lens_core::Compatibility::Uncertain);
        assert_eq!(diff.abi.added, vec!["libc.so.6:GLIBC_2.38"]);
        // status strings share the kebab-case vocabulary of ElfReport.status
        assert_eq!(diff.left_status, "valid");
        assert_eq!(diff.schema, "abilens.diff/v3");
    }
}
