use lens_core::{Compatibility, SetDiff};
use serde::{Deserialize, Serialize};

pub const REPORT_SCHEMA_V2: &str = "abilens.report/v2";
pub const DIFF_SCHEMA_V2: &str = "abilens.diff/v2";

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum InputStatus {
    Valid,
    NonElf,
    Corrupt,
    Unsupported,
    Unreadable,
    ToolError,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ToolInfo {
    pub name: String,
    pub version: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct ElfHeaderInfo {
    pub class: String,
    pub endian: String,
    pub machine: String,
    #[serde(rename = "type")]
    pub elf_type: String,
    pub dynamic: bool,
    pub stripped: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct Dependencies {
    pub needed: Vec<String>,
    pub rpath: Vec<String>,
    pub runpath: Vec<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub soname: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub interpreter: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct VersionRequirement {
    pub library: String,
    pub namespace: String,
    pub version: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct SymbolEvidence {
    pub identity: String,
    pub size: u64,
    pub binding: String,
    pub visibility: String,
    #[serde(rename = "type")]
    pub symbol_type: String,
    pub default_version: bool,
    /// False for undefined (imported) dynamic symbols — consumers must not
    /// treat an import as an export.
    #[serde(default)]
    pub defined: bool,
    #[serde(skip_serializing_if = "Option::is_none", default)]
    pub demangled: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct PolicyEvaluation {
    pub applied: bool,
    pub passed: bool,
    pub violations: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
pub struct AbiData {
    pub versions: Vec<VersionRequirement>,
    pub symbols: Vec<String>,
    pub vtables: Vec<String>,
    #[serde(default)]
    pub types: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ElfReport {
    pub schema: String,
    pub input: String,
    pub status: InputStatus,
    pub message: String,
    pub tool: ToolInfo,
    pub elf: ElfHeaderInfo,
    pub dependencies: Dependencies,
    pub abi: AbiData,
    pub policy: PolicyEvaluation,
    pub diagnostics: Vec<String>,
    pub evidence: Vec<SymbolEvidence>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DiffDependencies {
    pub needed: SetDiff<String>,
    pub rpath: SetDiff<String>,
    pub runpath: SetDiff<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DiffReport {
    pub schema: String,
    pub left: String,
    pub right: String,
    pub changed: bool,
    pub compatible: bool,
    /// Three-state verdict using the shared vocabulary: `compatible`,
    /// `incompatible`, or `uncertain` (fail-closed on ambiguous evidence).
    pub compatibility: Compatibility,
    pub left_status: String,
    pub right_status: String,
    pub header_changes: Vec<String>,
    pub dependencies: DiffDependencies,
    pub symbols: SetDiff<String>,
    pub vtables: SetDiff<String>,
    pub abi: SetDiff<String>,
    pub types: SetDiff<String>,
    pub symbol_changes: Vec<String>,
    pub diagnostics: Vec<String>,
}
