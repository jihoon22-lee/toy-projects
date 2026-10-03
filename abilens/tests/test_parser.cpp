#include "abilens/diff.hpp"
#include "abilens/elf.hpp"
#include "abilens/inspect.hpp"
#include "abilens/report.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "parser test failed: %s\n", message);
        std::abort();
    }
}

void expect_parse_failure(const std::string& json, const char* message) {
    bool failed = false;
    try {
        (void)abilens::parse_report_json(json);
    } catch (const std::exception&) {
        failed = true;
    }
    expect(failed, message);
}

std::string replace_once(std::string value,
                         const std::string& from,
                         const std::string& to) {
    const std::size_t position = value.find(from);
    expect(position != std::string::npos, "test fixture contains replacement text");
    value.replace(position, from.size(), to);
    return value;
}

abilens::ElfHeader synthetic_header() {
    abilens::ElfHeader header;
    header.elf_class = "ELF64";
    header.endian = "little-endian";
    header.type = "ET_DYN (Shared object file)";
    header.machine = "Advanced Micro Devices X86-64";
    header.has_dynamic = true;
    header.has_program_headers = true;
    header.has_section_headers = true;
    return header;
}

std::filesystem::path temporary_file(const std::string& name, const std::string& bytes) {
    const std::string pattern =
        (std::filesystem::temp_directory_path() / ("abilens-" + name + "-XXXXXX")).string();
    std::vector<char> mutable_pattern(pattern.begin(), pattern.end());
    mutable_pattern.push_back('\0');
    const int descriptor = ::mkstemp(mutable_pattern.data());
    expect(descriptor >= 0, "mkstemp creates a private temporary file");
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::write(descriptor, bytes.data() + offset, bytes.size() - offset);
        expect(count > 0, "temporary file receives all bytes");
        offset += static_cast<std::size_t>(count);
    }
    expect(::close(descriptor) == 0, "temporary file closes cleanly");
    return std::filesystem::path(mutable_pattern.data());
}

// Little-endian writers for the synthetic ELF fixture.
void put16(std::vector<unsigned char>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<unsigned char>(value & 0xffU);
    bytes[offset + 1U] = static_cast<unsigned char>((value >> 8U) & 0xffU);
}
void put32(std::vector<unsigned char>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned int i = 0; i < 4U; ++i) {
        bytes[offset + i] = static_cast<unsigned char>((value >> (i * 8U)) & 0xffU);
    }
}
void put64(std::vector<unsigned char>& bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned int i = 0; i < 8U; ++i) {
        bytes[offset + i] = static_cast<unsigned char>((value >> (i * 8U)) & 0xffU);
    }
}

struct StringTable {
    std::vector<unsigned char> bytes{1U, 0U};
    std::uint64_t add(const std::string& value) {
        const std::uint64_t index = bytes.size();
        bytes.insert(bytes.end(), value.begin(), value.end());
        bytes.push_back(0U);
        return index;
    }
};

// Builds a complete little-endian ELF64 shared object.  PT_LOAD maps the
// whole file 1:1 at vaddr 0 so every virtual address equals its file offset.
// Dynamic entries: NEEDED libstdc++/libc, RPATH /opt/abi:$ORIGIN/lib, RUNPATH
// $ORIGIN/plugins; verneed libc→GLIBC_2.34, libstdc++→GLIBCXX_3.4.30 +
// CXXABI_1.3.13.  Section headers hold one .symtab when requested.
std::vector<unsigned char> synthetic_elf(bool with_symtab, bool with_sections) {
    constexpr std::size_t phoff = 0x40;
    constexpr std::size_t dynoff = 0x100;
    constexpr std::size_t stroff = 0x200;
    constexpr std::size_t vnoff = 0x280;
    constexpr std::size_t shoff = 0x300;
    StringTable strtab;
    const std::uint64_t s_libstdcxx = strtab.add("libstdc++.so.6");
    const std::uint64_t s_libc = strtab.add("libc.so.6");
    const std::uint64_t s_rpath = strtab.add("/opt/abi:$ORIGIN/lib");
    const std::uint64_t s_runpath = strtab.add("$ORIGIN/plugins");
    const std::uint64_t s_glibc = strtab.add("GLIBC_2.34");
    const std::uint64_t s_glibcxx = strtab.add("GLIBCXX_3.4.30");
    const std::uint64_t s_cxxabi = strtab.add("CXXABI_1.3.13");

    const std::uint16_t shnum = with_sections ? (with_symtab ? 2U : 1U) : 0U;
    std::vector<unsigned char> file(shoff + (with_sections ? 2U * 64U : 0U), 0U);

    // ELF64 header.
    file[0] = 0x7f; file[1] = 'E'; file[2] = 'L'; file[3] = 'F';
    file[4] = 2;  // ELFCLASS64
    file[5] = 1;  // little endian
    file[6] = 1;  // EV_CURRENT
    put16(file, 16, 3);       // ET_DYN
    put16(file, 18, 62);      // EM_X86_64
    put32(file, 20, 1);       // EV_CURRENT
    put64(file, 32, phoff);   // e_phoff
    put64(file, 40, shoff);   // e_shoff
    put16(file, 52, 64);      // e_ehsize
    put16(file, 54, 56);      // e_phentsize
    put16(file, 56, 2);       // e_phnum
    put16(file, 58, 64);      // e_shentsize
    put16(file, 60, shnum);   // e_shnum
    put16(file, 62, 0);       // e_shstrndx

    // PT_LOAD: whole file, vaddr 0 == offset 0.
    put32(file, phoff, 1);
    put64(file, phoff + 8, 0);
    put64(file, phoff + 16, 0);
    put64(file, phoff + 40, file.size());

    // PT_DYNAMIC at dynoff, size for the written entries.
    put32(file, phoff + 56, 2);
    put64(file, phoff + 56 + 8, dynoff);
    put64(file, phoff + 56 + 16, dynoff);
    put64(file, phoff + 56 + 40, 9U * 16U);

    // Dynamic entries (tag, value) x9 + terminator slot left zero.
    const std::uint64_t dynamics[][2] = {
        {1, s_libstdcxx},   // DT_NEEDED
        {1, s_libc},        // DT_NEEDED
        {15, s_rpath},      // DT_RPATH
        {29, s_runpath},    // DT_RUNPATH
        {5, stroff},        // DT_STRTAB
        {10, 0},            // DT_STRSZ (patched below)
        {0x6ffffffe, vnoff},// DT_VERNEED
        {0x6fffffff, 2},    // DT_VERNEEDNUM
        {0, 0},             // DT_NULL
    };
    for (std::size_t i = 0; i < 9U; ++i) {
        put64(file, dynoff + i * 16U, dynamics[i][0]);
        put64(file, dynoff + i * 16U + 8U, dynamics[i][1]);
    }
    std::copy(strtab.bytes.begin(), strtab.bytes.end(), file.begin() + stroff);
    put64(file, dynoff + 5U * 16U + 8U, strtab.bytes.size());  // DT_STRSZ

    // Verneed record 1: libc.so.6 → GLIBC_2.34.
    put16(file, vnoff, 1);            // vn_version
    put16(file, vnoff + 2, 1);        // vn_cnt
    put32(file, vnoff + 4, static_cast<std::uint32_t>(s_libc));
    put32(file, vnoff + 8, 16);       // vn_aux
    put32(file, vnoff + 12, 32);      // vn_next
    put32(file, vnoff + 16 + 8, static_cast<std::uint32_t>(s_glibc));
    put32(file, vnoff + 16 + 12, 0);  // vna_next

    // Verneed record 2: libstdc++.so.6 → GLIBCXX_3.4.30, CXXABI_1.3.13.
    const std::size_t vn2 = vnoff + 32;
    put16(file, vn2, 1);
    put16(file, vn2 + 2, 2);
    put32(file, vn2 + 4, static_cast<std::uint32_t>(s_libstdcxx));
    put32(file, vn2 + 8, 16);
    put32(file, vn2 + 12, 0);
    put32(file, vn2 + 16 + 8, static_cast<std::uint32_t>(s_glibcxx));
    put32(file, vn2 + 16 + 12, 16);
    put32(file, vn2 + 32 + 8, static_cast<std::uint32_t>(s_cxxabi));
    put32(file, vn2 + 32 + 12, 0);

    if (with_sections) {
        // Section 0 is the null section; section 1 is a SHT_SYMTAB when asked.
        if (with_symtab) {
            put32(file, shoff + 64 + 4, 2);  // sh_type = SHT_SYMTAB
        }
    }
    return file;
}

abilens::ElfReport test_native_inspector() {
    const abilens::ElfReport parsed =
        abilens::inspect_elf_buffer(synthetic_elf(true, true), synthetic_header());
    expect(parsed.status == abilens::InputStatus::Valid, "synthetic ELF is valid");
    expect(parsed.tool.name == "abilens" && !parsed.tool.version.empty(),
           "the internal analyzer is recorded");
    expect(parsed.needed.size() == 2U && parsed.needed.front() == "libc.so.6",
           "DT_NEEDED entries are parsed and sorted");
    expect(parsed.rpath.size() == 2U && parsed.rpath.front() == "$ORIGIN/lib",
           "RPATH entries are split and sorted");
    expect(parsed.runpath.size() == 1U && parsed.runpath.front() == "$ORIGIN/plugins",
           "RUNPATH is parsed");
    expect(parsed.versions.size() == 3U, "typed ABI requirements are parsed");
    expect(parsed.stripped_known && !parsed.stripped, "symtab marks a binary as unstripped");

    const abilens::ElfReport stripped =
        abilens::inspect_elf_buffer(synthetic_elf(false, true), synthetic_header());
    expect(stripped.stripped_known && stripped.stripped,
           "missing .symtab marks the binary stripped");

    abilens::ElfHeader no_sections = synthetic_header();
    no_sections.has_section_headers = false;
    const abilens::ElfReport headerless =
        abilens::inspect_elf_buffer(synthetic_elf(false, false), no_sections);
    expect(!headerless.stripped_known,
           "missing section headers leave strippedness unknown");
    expect(headerless.needed.size() == 2U && headerless.versions.size() == 3U,
           "segment parsing still reports dependencies and ABI requirements");

    std::vector<unsigned char> corrupt = synthetic_elf(true, true);
    put64(corrupt, 0x100 + 5U * 16U + 8U, corrupt.size() * 4U);  // DT_STRSZ out of bounds
    const abilens::ElfReport misclassified =
        abilens::inspect_elf_buffer(corrupt, synthetic_header());
    expect(misclassified.status == abilens::InputStatus::Corrupt,
           "out-of-bounds dynamic metadata fails closed");
    return parsed;
}

std::filesystem::path test_policy(const abilens::ElfReport& parsed) {
    abilens::Policy policy;
    policy.expected_class = "ELF64";
    policy.expected_machine = "Advanced Micro Devices X86-64";
    policy.max_glibc = "2.31";
    policy.forbid_absolute_rpath = true;
    const abilens::PolicyEvaluation evaluation = abilens::evaluate_policy(parsed, policy);
    expect(!evaluation.passed && evaluation.violations.size() == 2U,
           "policy reports ABI floor and absolute path violations");
    const std::filesystem::path invalid_policy =
        temporary_file("invalid-policy", std::string("max_glibc=2.31") +
                                             static_cast<char>(0xff) + "\n");
    bool invalid_policy_failed = false;
    try {
        (void)abilens::load_policy_file(invalid_policy);
    } catch (const std::exception&) {
        invalid_policy_failed = true;
    }
    expect(invalid_policy_failed, "policy files reject invalid UTF-8");
    return invalid_policy;
}

void test_json_contract(const abilens::ElfReport& parsed) {
    const std::string json = abilens::serialize_report(parsed);
    const abilens::ElfReport round_trip = abilens::parse_report_json(json);
    expect(abilens::serialize_report(round_trip) == json, "report JSON is stable under round trip");
    expect(round_trip.tool.name == "abilens" && round_trip.tool.version == "0.1.0",
           "report preserves the analyzer identity");
    expect_parse_failure(json.substr(0U, json.size() - 1U) +
                             ",\"unexpected\":null}",
                         "unknown root fields are rejected");
    expect_parse_failure(replace_once(json, "\"dynamic\":true", "\"dynamic\":true,\"extra\":false"),
                         "unknown nested fields are rejected");
    expect_parse_failure(replace_once(json, "\"needed\":[\"libc.so.6\",\"libstdc++.so.6\"]",
                                      "\"needed\":[\"libc.so.6\",\"libc.so.6\"]"),
                         "duplicate dependency entries are rejected");
    const std::string version_object =
        "{\"namespace\":\"GLIBC\",\"version\":\"2.34\",\"library\":\"libc.so.6\"}";
    expect_parse_failure(replace_once(json, version_object, version_object + "," + version_object),
                         "duplicate ABI requirements are rejected");
    expect_parse_failure(replace_once(json, "\"namespace\":\"GLIBC\"", "\"namespace\":\"OTHER\""),
                         "unknown ABI namespaces are rejected");
    expect_parse_failure(replace_once(json, "\"GLIBC\":\"2.34\"", "\"GLIBC\":\"2.35\""),
                         "inconsistent ABI maximums are rejected");
    expect_parse_failure(replace_once(json, "\"name\":\"abilens\"",
                                      "\"name\":\"llvm-readelf\""),
                         "valid reports must identify the abilens analyzer");
    std::string deeply_nested;
    for (unsigned int level = 0; level < 65U; ++level) {
        deeply_nested.push_back('[');
    }
    deeply_nested += "null";
    for (unsigned int level = 0; level < 65U; ++level) {
        deeply_nested.push_back(']');
    }
    expect_parse_failure(deeply_nested, "deeply nested JSON is rejected safely");
    expect_parse_failure("\"\\ud800\"", "unpaired high surrogate is rejected safely");
    expect_parse_failure("\"\\udc00\"", "unpaired low surrogate is rejected safely");
    const std::string invalid_raw_utf8 =
        replace_once(json, "\"input\":\"\"", std::string("\"input\":\"bad") +
                                                     static_cast<char>(0xff) + "\"");
    expect_parse_failure(invalid_raw_utf8, "invalid raw UTF-8 is rejected");
    const std::string overlong_utf8 =
        replace_once(json, "\"input\":\"\"", std::string("\"input\":\"") +
                                                     static_cast<char>(0xc0) +
                                                     static_cast<char>(0xaf) + "\"");
    expect_parse_failure(overlong_utf8, "overlong raw UTF-8 is rejected");
    abilens::ElfReport unicode_report = parsed;
    unicode_report.input = "artifact-\xed\x95\x9c\xea\xb8\x80.so";
    const std::string unicode_json = abilens::serialize_report(unicode_report);
    expect(abilens::parse_report_json(unicode_json).input == unicode_report.input,
           "valid UTF-8 survives report JSON round trip");
    abilens::ElfReport byte_report = parsed;
    byte_report.input = std::string("artifact-") + static_cast<char>(0xff) + ".so";
    const std::string byte_json = abilens::serialize_report(byte_report);
    expect(byte_json.find("artifact-\\u00ff.so") != std::string::npos,
           "invalid path bytes are escaped into valid JSON");
    expect(abilens::parse_report_json(byte_json).input == "artifact-\xc3\xbf.so",
           "escaped invalid bytes decode to their Unicode code point");
    std::string too_many_nodes = "[";
    for (unsigned int index = 0; index < 50001U; ++index) {
        if (index != 0U) {
            too_many_nodes.push_back(',');
        }
        too_many_nodes += "[null]";
    }
    too_many_nodes.push_back(']');
    expect_parse_failure(too_many_nodes, "large shallow JSON is rejected by the node bound");
}

void test_diff_contract(const abilens::ElfReport& parsed) {
    abilens::ElfReport changed = parsed;
    changed.needed.push_back("libm.so.6");
    changed.versions.push_back({"GLIBC", "2.35", "libc.so.6"});
    const abilens::DiffReport diff = abilens::diff_reports(parsed, changed);
    expect(diff.changed && !diff.compatible, "ABI diff marks a raised requirement");
    expect(diff.needed.added.size() == 1U && diff.needed.added.front() == "libm.so.6",
           "dependency additions are reported");
    expect(abilens::serialize_diff(diff) == abilens::serialize_diff(diff),
           "diff JSON is deterministic");
    abilens::DiffReport byte_diff = diff;
    byte_diff.left = std::string("left-") + static_cast<char>(0xfe);
    expect(abilens::serialize_diff(byte_diff).find("left-\\u00fe") != std::string::npos,
           "diff JSON escapes invalid path bytes");
}

std::vector<std::filesystem::path> test_input_classification() {
    const std::filesystem::path non_elf = temporary_file("non-elf", "not an ELF");
    const abilens::HeaderCheck non_elf_result = abilens::validate_elf_file(non_elf);
    expect(non_elf_result.status == abilens::InputStatus::NonElf, "non-ELF is classified safely");
    const std::string short_elf{static_cast<char>(0x7f), 'E', 'L', 'F',
                                static_cast<char>(0x02), static_cast<char>(0x01),
                                static_cast<char>(0x01), '\0'};
    const std::filesystem::path corrupt = temporary_file("corrupt", short_elf);
    const abilens::HeaderCheck corrupt_result = abilens::validate_elf_file(corrupt);
    expect(corrupt_result.status == abilens::InputStatus::Corrupt,
           "short ELF is classified as corrupt");
    return {non_elf, corrupt};
}

void remove_path(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
}

}  // namespace

int main() {
    const abilens::ElfReport parsed = test_native_inspector();
    const std::filesystem::path invalid_policy = test_policy(parsed);
    test_json_contract(parsed);
    test_diff_contract(parsed);
    const std::vector<std::filesystem::path> inputs = test_input_classification();

    remove_path(inputs[0]);
    remove_path(inputs[1]);
    remove_path(invalid_policy);
    std::puts("test_parser: PASS");
    return 0;
}
