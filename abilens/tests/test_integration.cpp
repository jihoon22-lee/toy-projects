#include "abilens/diff.hpp"
#include "abilens/report.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
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

    if (argc >= 3) {
        const abilens::ElfReport executable = abilens::inspect_file(argv[2]);
        expect(executable.status == abilens::InputStatus::Valid, "release executable is valid ELF");
        const abilens::DiffReport diff = abilens::diff_reports(report, executable);
        expect(diff.changed, "fixture and executable have an observable ABI difference");
    }
    std::puts("test_integration: PASS");
    return 0;
}
