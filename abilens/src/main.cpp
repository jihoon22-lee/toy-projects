#include "abilens/diff.hpp"
#include "abilens/report.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kVersion = abilens::kAbiLensVersion;
constexpr std::size_t kReportInputLimit = 8U * 1024U * 1024U;

struct Options {
    bool json = false;
    std::string fail_on = "default";
    abilens::InspectOptions inspection;
    std::string policy_path;
    std::vector<std::string> positional;
};

void usage(std::ostream& output) {
    output << "Usage:\n"
           << "  abilens inspect [--json|--format text|json] [--policy FILE] ELF\n"
           << "  abilens diff [--json|--format text|json] REPORT_OR_ELF REPORT_OR_ELF\n"
           << "  options: --fail-on default|never|incompatible|unknown|changed\n"
           << "           --sysroot DIR --origin /target/dir --library-path /target/lib --dwarf\n"
           << "  abilens --version\n";
}

void set_format(Options& options, const std::string& value) {
    if (value == "json") {
        options.json = true;
        return;
    }
    if (value == "text") {
        options.json = false;
        return;
    }
    throw std::runtime_error("--format accepts only text or json");
}

std::string required_argument(int argc, char** argv, int& index,
                              const char* option, const char* description) {
    if (index + 1 >= argc) {
        throw std::runtime_error(std::string(option) + " requires " + description);
    }
    return argv[++index];
}

Options parse_options(int argc, char** argv, int first) {
    Options options;
    for (int index = first; index < argc; ++index) {
        const std::string token(argv[index]);
        if (token == "--json") {
            options.json = true;
        } else if (token == "--text") {
            options.json = false;
        } else if (token == "--format") {
            set_format(options, required_argument(argc, argv, index, "--format", "text or json"));
        } else if (token.rfind("--format=", 0U) == 0U) {
            set_format(options, token.substr(9U));
        } else if (token == "--dwarf") {
            options.inspection.dwarf = true;
        } else if (token == "--sysroot") {
            options.inspection.sysroot = required_argument(argc, argv, index, "--sysroot", "a directory");
        } else if (token == "--origin") {
            options.inspection.origin = required_argument(argc, argv, index, "--origin", "a target path");
        } else if (token == "--library-path") {
            options.inspection.library_paths.push_back(required_argument(argc, argv, index, "--library-path", "a target path"));
            if (options.inspection.library_paths.size() > 128) throw std::runtime_error("too many library paths");
        } else if (token == "--fail-on") {
            options.fail_on = required_argument(argc, argv, index, "--fail-on", "a policy");
            if (options.fail_on != "default" && options.fail_on != "never" && options.fail_on != "incompatible" && options.fail_on != "unknown" && options.fail_on != "changed") throw std::runtime_error("invalid --fail-on policy");
        } else if (token == "--policy") {
            options.policy_path = required_argument(argc, argv, index, "--policy", "a file");
        } else if (token == "--help" || token == "-h") {
            usage(std::cout);
            std::exit(0);
        } else if (!token.empty() && token.front() == '-') {
            throw std::runtime_error("unknown option: " + token);
        } else {
            options.positional.push_back(token);
        }
    }
    return options;
}

std::string read_bounded_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("could not open report: " + path.generic_string());
    }
    std::string text;
    text.reserve(4096U);
    char buffer[16U * 1024U];
    std::size_t total = 0;
    while (stream) {
        stream.read(buffer, sizeof(buffer));
        const std::streamsize count = stream.gcount();
        if (count <= 0) {
            break;
        }
        const std::size_t bytes = static_cast<std::size_t>(count);
        if (bytes > kReportInputLimit - total) {
            throw std::runtime_error("report input exceeds the 8 MiB bound");
        }
        text.append(buffer, bytes);
        total += bytes;
    }
    return text;
}

bool starts_as_json(const std::filesystem::path& path) {
    if (path.extension() == ".json") {
        return true;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    char character = '\0';
    while (stream.get(character)) {
        if (std::isspace(static_cast<unsigned char>(character)) == 0) {
            return character == '{';
        }
    }
    return false;
}

abilens::ElfReport load_report_or_binary(const std::string& value, const abilens::InspectOptions& options) {
    const std::filesystem::path path(value);
    if (starts_as_json(path)) {
        abilens::ElfReport report = abilens::parse_report_json(read_bounded_text(path));
        if (report.input.empty()) {
            report.input = path.generic_string();
        }
        return report;
    }
    return abilens::inspect_file(path, {}, options);
}

int inspect_command(const Options& options) {
    if (options.positional.size() != 1U) {
        usage(std::cerr);
        return 64;
    }
    abilens::Policy policy;
    if (!options.policy_path.empty()) {
        policy = abilens::load_policy_file(options.policy_path);
    }
    const abilens::ElfReport report =
        abilens::inspect_file(std::filesystem::path(options.positional.front()), policy, options.inspection);
    if (options.json) {
        std::cout << abilens::serialize_report(report) << '\n';
    } else {
        std::cout << abilens::render_report_text(report);
    }
    if (report.status != abilens::InputStatus::Valid) {
        return 3;
    }
    if (options.fail_on == "never") return 0;
    if (options.fail_on == "unknown" && options.inspection.dwarf && report.dwarf_status != "complete") return 2;
    return report.policy.passed ? 0 : 2;
}

int diff_command(const Options& options) {
    if (options.positional.size() != 2U) {
        usage(std::cerr);
        return 64;
    }
    if (!options.policy_path.empty()) {
        throw std::runtime_error("--policy is valid only for inspect");
    }
    const abilens::ElfReport left = load_report_or_binary(options.positional[0], options.inspection);
    const abilens::ElfReport right = load_report_or_binary(options.positional[1], options.inspection);
    const abilens::DiffReport diff = abilens::diff_reports(left, right);
    if (options.json) {
        std::cout << abilens::serialize_diff(diff) << '\n';
    } else {
        std::cout << abilens::render_diff_text(diff);
    }
    if (left.status == abilens::InputStatus::Valid && right.status == abilens::InputStatus::Valid) {
        if (options.fail_on == "changed") return diff.changed ? 2 : 0;
        if (options.fail_on == "unknown") return diff.compatible ? 0 : 2;
        if (options.fail_on == "incompatible") return diff.compatibility == "incompatible" ? 2 : 0;
    }
    return (left.status == abilens::InputStatus::Valid &&
            right.status == abilens::InputStatus::Valid)
               ? 0
               : 3;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            usage(std::cerr);
            return 64;
        }
        const std::string command(argv[1]);
        if (command == "--version" || command == "version") {
            std::cout << "abilens " << kVersion << '\n';
            return 0;
        }
        if (command == "--help" || command == "-h") {
            usage(std::cout);
            return 0;
        }
        const Options options = parse_options(argc, argv, 2);
        if (command == "inspect") {
            return inspect_command(options);
        }
        if (command == "diff") {
            return diff_command(options);
        }
        throw std::runtime_error("unknown command: " + command);
    } catch (const std::exception& error) {
        std::cerr << "abilens: " << error.what() << '\n';
        return 64;
    }
}
