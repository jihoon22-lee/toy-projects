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

constexpr const char* kVersion = "0.1.0";
constexpr std::size_t kReportInputLimit = 8U * 1024U * 1024U;

struct Options {
    bool json = false;
    std::string policy_path;
    std::vector<std::string> positional;
};

void usage(std::ostream& output) {
    output << "Usage:\n"
           << "  abilens inspect [--json|--format text|json] [--policy FILE] ELF\n"
           << "  abilens diff [--json|--format text|json] REPORT_OR_ELF REPORT_OR_ELF\n"
           << "  abilens --version\n";
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
            if (index + 1 >= argc) {
                throw std::runtime_error("--format requires text or json");
            }
            const std::string format(argv[++index]);
            if (format == "json") {
                options.json = true;
            } else if (format == "text") {
                options.json = false;
            } else {
                throw std::runtime_error("--format accepts only text or json");
            }
        } else if (token.rfind("--format=", 0U) == 0U) {
            const std::string format = token.substr(9U);
            if (format == "json") {
                options.json = true;
            } else if (format == "text") {
                options.json = false;
            } else {
                throw std::runtime_error("--format accepts only text or json");
            }
        } else if (token == "--policy") {
            if (index + 1 >= argc) {
                throw std::runtime_error("--policy requires a file");
            }
            options.policy_path = argv[++index];
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

abilens::ElfReport load_report_or_binary(const std::string& value) {
    const std::filesystem::path path(value);
    if (starts_as_json(path)) {
        abilens::ElfReport report = abilens::parse_report_json(read_bounded_text(path));
        if (report.input.empty()) {
            report.input = path.generic_string();
        }
        return report;
    }
    return abilens::inspect_file(path);
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
        abilens::inspect_file(std::filesystem::path(options.positional.front()), policy);
    if (options.json) {
        std::cout << abilens::serialize_report(report) << '\n';
    } else {
        std::cout << abilens::render_report_text(report);
    }
    if (report.status != abilens::InputStatus::Valid) {
        return 3;
    }
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
    const abilens::ElfReport left = load_report_or_binary(options.positional[0]);
    const abilens::ElfReport right = load_report_or_binary(options.positional[1]);
    const abilens::DiffReport diff = abilens::diff_reports(left, right);
    if (options.json) {
        std::cout << abilens::serialize_diff(diff) << '\n';
    } else {
        std::cout << abilens::render_diff_text(diff);
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
