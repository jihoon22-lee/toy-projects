#include "abilens/report.hpp"

#include "input_internal.hpp"

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
        std::fprintf(stderr, "integration test failed: %s\n", message);
        std::abort();
    }
}

bool contains(const std::vector<std::string>& values, const std::string& needle) {
    for (const std::string& value : values) {
        if (value.find(needle) != std::string::npos) {
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

// The inspection reads one already-open descriptor end to end and then
// re-checks the identity captured at open time, so a swapped or appended
// target can never silently produce evidence for the wrong bytes.
void test_input_identity(const std::filesystem::path& fixture) {
    const std::filesystem::path root = temporary_directory();
    const std::filesystem::path target = root / "target.so";
    std::filesystem::copy_file(fixture, target);

    {
        const abilens::detail::OpenInput input(target);
        expect(input.opened(), "target opens for inspection");
        expect(input.unchanged(), "freshly opened input is unchanged");

        std::ofstream append(target, std::ios::app);
        append << 'x';
        append.close();
        expect(!input.unchanged(), "appended input is detected as changed");
    }
    {
        const std::filesystem::path replacement = root / "replacement.so";
        std::filesystem::copy_file(fixture, replacement);
        const abilens::detail::OpenInput input(target);
        expect(input.opened(), "replaced target opens for inspection");
        std::filesystem::rename(replacement, target);
        expect(!input.unchanged(), "replaced input is detected as changed");
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
}

}  // namespace

int main(int argc, char** argv) {
    expect(argc >= 2, "fixture path is supplied by make test");
    const std::filesystem::path fixture(argv[1]);
    const abilens::ElfReport report = abilens::inspect_file(fixture);
    expect(report.status == abilens::InputStatus::Valid, "real shared fixture is valid ELF");
    expect(report.tool.name == "abilens" && !report.tool.version.empty(),
           "the internal analyzer is recorded");
    expect(report.header.elf_class == "ELF64", "fixture class is detected directly");
    expect(report.header.has_dynamic, "fixture has dynamic metadata");
    expect(contains(report.needed, "libstdc++.so.6"), "fixture records libstdc++ dependency");
    expect(!report.versions.empty(), "fixture exposes version requirements");
    expect(contains(report.symbols, "abilens_fixture_value@ABILENS_1.0"),
           "defined dynamic symbols carry verdef-qualified names");
    expect(!abilens::serialize_report(report).empty(), "real report serializes");
    test_input_identity(fixture);

    if (argc >= 3) {
        const abilens::ElfReport executable = abilens::inspect_file(argv[2]);
        expect(executable.status == abilens::InputStatus::Valid,
               "release executable is valid ELF");
        expect(executable.tool.name == "abilens",
               "release executable records the internal analyzer");
    }

    std::puts("test_integration: PASS");
    return 0;
}
