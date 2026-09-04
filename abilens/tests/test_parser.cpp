#include "abilens/diff.hpp"
#include "abilens/elf.hpp"
#include "abilens/report.hpp"
#include "abilens/readelf.hpp"

#include <cassert>
#include <chrono>
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

std::filesystem::path temporary_directory(const std::string& name) {
    const std::string pattern =
        (std::filesystem::temp_directory_path() / ("abilens-" + name + "-XXXXXX")).string();
    std::vector<char> mutable_pattern(pattern.begin(), pattern.end());
    mutable_pattern.push_back('\0');
    expect(::mkdtemp(mutable_pattern.data()) != nullptr,
           "mkdtemp creates a private temporary directory");
    return std::filesystem::path(mutable_pattern.data());
}

}  // namespace

int main() {
    const std::string transcript = R"(ELF Header:
  Class:                             ELF64
  Type:                              DYN (Shared object file)
  Machine:                           Advanced Micro Devices X86-64
Section Headers:
  [ 1] .dynsym           DYNSYM
  [ 2] .symtab           SYMTAB
Dynamic section at offset 0x200 contains 5 entries:
 0x0000000000000001 (NEEDED)             Shared library: [libstdc++.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x000000000000000f (RPATH)              Library rpath: [/opt/abi:$ORIGIN/lib]
 0x000000000000001d (RUNPATH)            Library runpath: [$ORIGIN/plugins]

Version needs section '.gnu.version_r' contains 2 entries:
  0x0010: Version: 1  File: libc.so.6  Cnt: 1
  0x0020:   Name: GLIBC_2.34  Flags: none  Version: 1
  0x0030: Version: 1  File: libstdc++.so.6  Cnt: 2
  0x0040:   Name: GLIBCXX_3.4.30  Flags: none  Version: 2
  0x0050:   Name: CXXABI_1.3.13  Flags: none  Version: 3
)";
    abilens::ReadelfEvidence synthetic_evidence;
    synthetic_evidence.return_code = 0;
    synthetic_evidence.standard_output = transcript;
    synthetic_evidence.capability = {true, "GNU readelf", "2.40"};
    const abilens::ElfReport parsed =
        abilens::parse_readelf_text(transcript, synthetic_header(), synthetic_evidence);
    expect(parsed.status == abilens::InputStatus::Valid, "synthetic readelf is valid");
    expect(parsed.needed.size() == 2U && parsed.needed.front() == "libc.so.6",
           "NEEDED entries are parsed and sorted");
    expect(parsed.rpath.size() == 2U && parsed.rpath.front() == "$ORIGIN/lib",
           "RPATH entries are split and sorted");
    expect(parsed.runpath.size() == 1U && parsed.runpath.front() == "$ORIGIN/plugins",
           "RUNPATH is parsed");
    expect(parsed.versions.size() == 3U, "typed ABI requirements are parsed");
    expect(parsed.stripped_known && !parsed.stripped, "symtab marks a binary as unstripped");
    abilens::ReadelfEvidence unsupported_evidence = synthetic_evidence;
    unsupported_evidence.capability = {false, "llvm-readelf", "18.0"};
    const abilens::ElfReport unsupported =
        abilens::parse_readelf_text(transcript, synthetic_header(), unsupported_evidence);
    expect(unsupported.status == abilens::InputStatus::ToolError,
           "non-GNU readelf capability fails closed");

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

    const std::string json = abilens::serialize_report(parsed);
    const abilens::ElfReport round_trip = abilens::parse_report_json(json);
    expect(abilens::serialize_report(round_trip) == json, "report JSON is stable under round trip");
    expect(round_trip.tool.name == "GNU readelf" && round_trip.tool.version == "2.40",
           "report preserves the GNU readelf capability");
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
    expect_parse_failure(replace_once(json, "\"name\":\"GNU readelf\"",
                                      "\"name\":\"llvm-readelf\""),
                         "valid reports must identify GNU readelf");
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

    const std::filesystem::path fake_dir = temporary_directory("fake-readelf");
    const std::filesystem::path fake_readelf = fake_dir / "readelf";
    {
        std::ofstream script(fake_readelf);
        script << "#!/bin/sh\n"
                  "if [ \"$1\" = \"--version\" ]; then\n"
                  "  printf 'GNU readelf (GNU Binutils) 2.40\\n'\n"
                  "  exit 0\n"
                  "fi\n"
                  "while :; do printf '%1048576s' x; done\n";
    }
    expect(::chmod(fake_readelf.c_str(), 0700) == 0, "fake readelf is executable");
    const char* old_path_value = std::getenv("PATH");
    const std::string old_path = old_path_value == nullptr ? "" : old_path_value;
    const std::string test_path =
        fake_dir.generic_string() + (old_path.empty() ? "" : ":" + old_path);
    expect(::setenv("PATH", test_path.c_str(), 1) == 0, "test PATH is installed");
    const auto runner_start = std::chrono::steady_clock::now();
    const abilens::ReadelfEvidence bounded = abilens::run_readelf(non_elf);
    const auto runner_elapsed = std::chrono::steady_clock::now() - runner_start;
    if (old_path_value == nullptr) {
        (void)::unsetenv("PATH");
    } else {
        (void)::setenv("PATH", old_path.c_str(), 1);
    }
    expect(bounded.capability.supported, "GNU readelf capability is accepted");
    expect(bounded.truncated && !bounded.timed_out,
           "readelf output bound terminates the child");
    expect(std::chrono::duration_cast<std::chrono::seconds>(runner_elapsed).count() < 5,
           "readelf output bound does not wait for the timeout");

    std::error_code error;
    std::filesystem::remove(fake_readelf, error);
    std::filesystem::remove(fake_dir, error);
    std::filesystem::remove(non_elf, error);
    std::filesystem::remove(corrupt, error);
    std::filesystem::remove(invalid_policy, error);
    std::puts("test_parser: PASS");
    return 0;
}
