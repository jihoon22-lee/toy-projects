#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace abilens {

inline constexpr const char* kAbiLensVersion = "0.1.2";  // x-release-please-version

enum class InputStatus {
    Valid,
    NonElf,
    Corrupt,
    Unsupported,
    Unreadable,
    ToolError,
};

struct ElfHeader {
    std::string elf_class;
    std::string endian;
    std::string type;
    std::string machine;
    std::uint16_t raw_type = 0;
    std::uint16_t raw_machine = 0;
    bool has_dynamic = false;
    bool has_program_headers = false;
    bool has_section_headers = false;
};

struct HeaderCheck {
    InputStatus status = InputStatus::Unreadable;
    ElfHeader header;
    std::string message;
};

struct VersionRequirement {
    std::string namespace_name;
    std::string version;
    std::string library;
};

struct ToolInfo {
    std::string name;
    std::string version;
};

struct Policy {
    std::string expected_class;
    std::string expected_machine;
    std::string max_glibc;
    std::string max_glibcxx;
    std::string max_cxxabi;
    bool forbid_absolute_rpath = false;
    std::vector<std::string> forbidden_needed;
    // Exported-symbol rules match the name@version identities the report
    // emits. A rule containing `@` pins one version definition exactly; a bare
    // name matches the symbol under any version (or none).
    std::vector<std::string> forbidden_symbols;
    std::vector<std::string> required_symbols;
    bool forbid_stripped = false;
    bool forbid_rpath = false;
    bool forbid_runpath = false;
};

struct PolicyEvaluation {
    bool applied = false;
    bool passed = true;
    std::vector<std::string> violations;
};

struct ElfReport {
    static constexpr const char* schema = "abilens.report/v1";

    std::string input;
    InputStatus status = InputStatus::Unreadable;
    std::string message;
    ToolInfo tool;
    ElfHeader header;
    bool stripped_known = false;
    bool stripped = false;
    std::vector<std::string> needed;
    std::vector<std::string> rpath;
    std::vector<std::string> runpath;
    std::vector<VersionRequirement> versions;
    std::vector<std::string> symbols;
    std::vector<std::string> vtables;
    // False only for reports loaded from JSON written before the field
    // existed: an absent axis is unknown evidence, not an empty set. A report
    // without "vtables" also predates name@version symbol identities.
    bool symbols_known = true;
    bool vtables_known = true;
    std::vector<std::string> diagnostics;
    PolicyEvaluation policy;
};

struct SetDiff {
    std::vector<std::string> added;
    std::vector<std::string> removed;
};

struct DiffReport {
    static constexpr const char* schema = "abilens.diff/v1";

    std::string left;
    std::string right;
    bool changed = false;
    bool compatible = true;
    std::string left_status;
    std::string right_status;
    SetDiff needed;
    SetDiff rpath;
    SetDiff runpath;
    SetDiff abi;
    SetDiff symbols;
    SetDiff vtables;
    std::vector<std::string> header_changes;
    std::vector<std::string> diagnostics;
};

const char* input_status_name(InputStatus status) noexcept;

}  // namespace abilens
