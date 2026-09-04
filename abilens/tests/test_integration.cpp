#include "abilens/diff.hpp"
#include "abilens/report.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "integration test failed: %s\n", message);
        std::abort();
    }
}

bool contains(const std::vector<std::string>& values, const std::string& expected) {
    for (const std::string& value : values) {
        if (value == expected) {
            return true;
        }
    }
    return false;
}

std::filesystem::path temporary_directory() {
    std::string pattern = "/tmp/abilens-identity-XXXXXX";
    std::vector<char> mutable_pattern(pattern.begin(), pattern.end());
    mutable_pattern.push_back('\0');
    expect(::mkdtemp(mutable_pattern.data()) != nullptr,
           "identity test directory is created");
    return std::filesystem::path(mutable_pattern.data());
}

std::filesystem::path system_readelf() {
    for (const std::filesystem::path candidate : {"/usr/bin/readelf", "/bin/readelf"}) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) return candidate;
    }
    return {};
}

void write_mutating_readelf(const std::filesystem::path& script) {
    std::ofstream output(script);
    output << "#!/bin/sh\n"
              "if [ ! -e \"$ABILENS_TEST_MUTATED\" ]; then\n"
              "  if [ \"$ABILENS_TEST_MODE\" = replace ]; then\n"
              "    mv -- \"$ABILENS_TEST_REPLACEMENT\" \"$ABILENS_TEST_TARGET\"\n"
              "  else\n"
              "    printf x >> \"$ABILENS_TEST_TARGET\"\n"
              "  fi\n"
              "  : > \"$ABILENS_TEST_MUTATED\"\n"
              "fi\n"
              "printf '%s\\n' \"$@\" >> \"$ABILENS_TEST_ARGUMENTS\"\n"
              "exec \"$ABILENS_TEST_REAL_READELF\" \"$@\"\n";
    output.close();
    expect(output.good(), "mutating readelf script is written");
    expect(::chmod(script.c_str(), 0700) == 0,
           "mutating readelf script is executable");
}

void test_input_identity(const std::filesystem::path& fixture,
                         const std::string& mode) {
    const std::filesystem::path real_readelf = system_readelf();
    expect(!real_readelf.empty(), "system GNU readelf path is available");
    const std::filesystem::path root = temporary_directory();
    const std::filesystem::path target = root / "target.so";
    const std::filesystem::path replacement = root / "replacement.so";
    const std::filesystem::path marker = root / "mutated";
    const std::filesystem::path arguments = root / "arguments";
    const std::filesystem::path fake_readelf = root / "readelf";
    std::filesystem::copy_file(fixture, target);
    std::filesystem::copy_file(fixture, replacement);
    write_mutating_readelf(fake_readelf);

    const char* old_path_value = std::getenv("PATH");
    const std::string old_path = old_path_value == nullptr ? "" : old_path_value;
    const std::string test_path = root.generic_string() + ":" + old_path;
    expect(::setenv("PATH", test_path.c_str(), 1) == 0, "identity test PATH is installed");
    expect(::setenv("ABILENS_TEST_MODE", mode.c_str(), 1) == 0, "mutation mode is installed");
    expect(::setenv("ABILENS_TEST_TARGET", target.c_str(), 1) == 0, "mutation target is installed");
    expect(::setenv("ABILENS_TEST_REPLACEMENT", replacement.c_str(), 1) == 0,
           "replacement path is installed");
    expect(::setenv("ABILENS_TEST_MUTATED", marker.c_str(), 1) == 0,
           "mutation marker is installed");
    expect(::setenv("ABILENS_TEST_ARGUMENTS", arguments.c_str(), 1) == 0,
           "argument path is installed");
    expect(::setenv("ABILENS_TEST_REAL_READELF", real_readelf.c_str(), 1) == 0,
           "real readelf path is installed");

    const abilens::ElfReport report = abilens::inspect_file(target);
    std::ifstream argument_input(arguments);
    const std::string observed_arguments((std::istreambuf_iterator<char>(argument_input)),
                                         std::istreambuf_iterator<char>());
    if (old_path_value == nullptr) {
        (void)::unsetenv("PATH");
    } else {
        (void)::setenv("PATH", old_path.c_str(), 1);
    }
    for (const char* name : {"ABILENS_TEST_MODE", "ABILENS_TEST_TARGET",
                             "ABILENS_TEST_REPLACEMENT", "ABILENS_TEST_MUTATED",
                             "ABILENS_TEST_ARGUMENTS", "ABILENS_TEST_REAL_READELF"}) {
        (void)::unsetenv(name);
    }

    expect(report.status == abilens::InputStatus::ToolError,
           "input mutation fails the inspection closed");
    expect(contains(report.diagnostics,
                    "input changed while readelf evidence was collected"),
           "input mutation reason is retained");
    expect(observed_arguments.find("/proc/self/fd/") != std::string::npos,
           "readelf receives the already-open descriptor path");
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

}  // namespace

int main(int argc, char** argv) {
    expect(argc >= 2, "fixture path is supplied by make test");
    const std::filesystem::path fixture(argv[1]);
    const abilens::ElfReport report = abilens::inspect_file(fixture);
    expect(report.status == abilens::InputStatus::Valid, "real shared fixture is valid ELF");
    expect(report.tool.name == "GNU readelf" && !report.tool.version.empty(),
           "system GNU readelf capability is recorded");
    expect(report.header.elf_class == "ELF64", "fixture class is detected directly");
    expect(report.header.has_dynamic, "fixture has dynamic metadata");
    expect(contains(report.needed, "libstdc++.so.6"), "fixture records libstdc++ dependency");
    expect(!report.versions.empty(), "fixture exposes version requirements");
    expect(!abilens::serialize_report(report).empty(), "real report serializes");
    test_input_identity(fixture, "replace");
    test_input_identity(fixture, "modify");

    if (argc >= 3) {
        const abilens::ElfReport executable = abilens::inspect_file(argv[2]);
        expect(executable.status == abilens::InputStatus::Valid, "release executable is valid ELF");
        const abilens::DiffReport diff = abilens::diff_reports(report, executable);
        expect(diff.changed, "fixture and executable have an observable ABI difference");
    }
    std::puts("test_integration: PASS");
    return 0;
}
