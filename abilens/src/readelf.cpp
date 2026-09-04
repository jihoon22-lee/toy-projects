#include "abilens/readelf.hpp"

#include <algorithm>
#include <array>
#include <cctype>

#include <sstream>
#include <vector>

namespace abilens {
namespace {

std::string trim(std::string value) {
    auto not_space = [](unsigned char character) { return std::isspace(character) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string bracket_value(const std::string& line) {
    const std::size_t begin = line.find('[');
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = line.find(']', begin + 1U);
    if (end == std::string::npos) {
        return {};
    }
    return line.substr(begin + 1U, end - begin - 1U);
}

void append_unique(std::vector<std::string>& values, const std::string& value) {
    if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0U, prefix.size(), prefix) == 0;
}

bool valid_version(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    bool digit = false;
    for (const char character : value) {
        if (character == '.') {
            if (!digit) {
                return false;
            }
            digit = false;
        } else if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            digit = true;
        } else {
            return false;
        }
    }
    return digit;
}

void append_version(std::vector<VersionRequirement>& values,
                    const std::string& token,
                    const std::string& library) {
    const std::array<std::string, 3U> namespaces{"GLIBCXX_", "GLIBC_", "CXXABI_"};
    for (const std::string& prefix : namespaces) {
        if (!starts_with(token, prefix)) {
            continue;
        }
        const std::string version = token.substr(prefix.size());
        if (!valid_version(version)) {
            return;
        }
        const std::string namespace_name = prefix.substr(0U, prefix.size() - 1U);
        const VersionRequirement candidate{namespace_name, version, library};
        const auto duplicate = std::find_if(
            values.begin(), values.end(), [&](const VersionRequirement& existing) {
                return existing.namespace_name == candidate.namespace_name &&
                       existing.version == candidate.version && existing.library == candidate.library;
            });
        if (duplicate == values.end()) {
            values.push_back(candidate);
        }
        return;
    }
}

std::string first_name_token(const std::string& line) {
    const std::size_t marker = line.find("Name:");
    if (marker == std::string::npos) {
        return {};
    }
    std::istringstream input(line.substr(marker + 5U));
    std::string token;
    input >> token;
    while (!token.empty() && (token.back() == ',' || token.back() == ';')) {
        token.pop_back();
    }
    return token;
}

bool has_symtab_section(const std::string& text) {
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t marker = line.find(']');
        if (marker != std::string::npos && line.find(".symtab", marker) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void append_colon_separated(const std::string& value,
                            std::vector<std::string>& destination) {
    std::istringstream paths(value);
    std::string item;
    while (std::getline(paths, item, ':')) append_unique(destination, item);
}

void update_current_library(const std::string& line, std::string& current_library) {
    const std::size_t marker = line.find("File:");
    if (marker == std::string::npos) return;
    current_library = trim(line.substr(marker + 5U));
    const std::size_t whitespace = current_library.find_first_of(" \t");
    if (whitespace != std::string::npos) current_library.resize(whitespace);
}

void parse_evidence_line(const std::string& line, std::string& current_library,
                         ElfReport& report) {
    update_current_library(line, current_library);
    if (line.find("(NEEDED)") != std::string::npos) {
        append_unique(report.needed, bracket_value(line));
    }
    if (line.find("(RPATH)") != std::string::npos) {
        append_colon_separated(bracket_value(line), report.rpath);
    }
    if (line.find("(RUNPATH)") != std::string::npos) {
        append_colon_separated(bracket_value(line), report.runpath);
    }
    if (line.find("Name:") != std::string::npos) {
        append_version(report.versions, first_name_token(line), current_library);
    }
}

bool version_requirement_less(const VersionRequirement& left,
                              const VersionRequirement& right) {
    if (left.namespace_name != right.namespace_name) {
        return left.namespace_name < right.namespace_name;
    }
    if (left.version != right.version) return left.version < right.version;
    return left.library < right.library;
}

void sort_evidence(ElfReport& report) {
    std::sort(report.needed.begin(), report.needed.end());
    std::sort(report.rpath.begin(), report.rpath.end());
    std::sort(report.runpath.begin(), report.runpath.end());
    std::sort(report.versions.begin(), report.versions.end(), version_requirement_less);
}

void append_stripped_evidence(const std::string& input, ElfReport& report) {
    report.stripped_known = input.find("Section Headers:") != std::string::npos;
    report.stripped = report.stripped_known && !has_symtab_section(input);
    if (!report.stripped_known) {
        report.diagnostics.push_back("stripped: unknown (section headers were unavailable)");
    } else if (report.stripped) {
        report.diagnostics.push_back("stripped: no .symtab section");
    } else {
        report.diagnostics.push_back("not-stripped: .symtab section present");
    }
}

ElfReport tool_error_report(const ElfHeader& header,
                            const ReadelfEvidence& evidence) {
    ElfReport report;
    report.status = InputStatus::ToolError;
    report.header = header;
    report.tool.name = evidence.capability.name;
    report.tool.version = evidence.capability.version;
    if (!evidence.capability.supported) {
        report.message = "system readelf capability is not supported";
    } else if (evidence.timed_out) {
        report.message = "readelf timed out";
    } else if (evidence.truncated) {
        report.message = "readelf output exceeded the safety bound";
    } else {
        report.message = "readelf failed to produce complete evidence";
    }
    if (!evidence.standard_error.empty()) {
        report.diagnostics.push_back(evidence.standard_error.substr(0U, 512U));
    }
    return report;
}

}  // namespace

ElfReport parse_readelf_text(const std::string& input,
                             const ElfHeader& header,
                             const ReadelfEvidence& evidence) {
    if (!evidence.capability.supported || evidence.return_code != 0 ||
        evidence.timed_out || evidence.truncated) {
        return tool_error_report(header, evidence);
    }
    if (input.size() > kReadelfOutputLimit || input.find('\0') != std::string::npos) {
        ElfReport report;
        report.status = InputStatus::ToolError;
        report.header = header;
        report.tool.name = evidence.capability.name;
        report.tool.version = evidence.capability.version;
        report.message = "readelf output is outside the bounded text contract";
        return report;
    }

    ElfReport report;
    report.status = InputStatus::Valid;
    report.header = header;
    report.tool.name = evidence.capability.name;
    report.tool.version = evidence.capability.version;
    std::string current_library;
    std::istringstream lines(input);
    std::string line;
    while (std::getline(lines, line)) {
        parse_evidence_line(line, current_library, report);
    }
    sort_evidence(report);
    if (!header.has_dynamic) {
        report.diagnostics.push_back("static-or-non-dynamic: no PT_DYNAMIC program header");
    }
    append_stripped_evidence(input, report);
    report.message = "ELF header and readelf evidence verified";
    return report;
}

}  // namespace abilens
