#include "abilens/report.hpp"

#include "report_internal.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace abilens {
namespace {

bool policy_applied(const Policy& policy) {
    return !policy.expected_class.empty() || !policy.expected_machine.empty() ||
           !policy.max_glibc.empty() || !policy.max_glibcxx.empty() ||
           !policy.max_cxxabi.empty() || policy.forbid_absolute_rpath ||
           !policy.forbidden_needed.empty();
}

void append_identity_violations(const ElfReport& report, const Policy& policy,
                                std::vector<std::string>& violations) {
    if (!policy.expected_class.empty() && report.header.elf_class != policy.expected_class) {
        violations.push_back("ELF class " + report.header.elf_class + " is not " +
                             policy.expected_class);
    }
    if (!policy.expected_machine.empty() && report.header.machine != policy.expected_machine) {
        violations.push_back("ELF machine " + report.header.machine + " is not " +
                             policy.expected_machine);
    }
}

void append_abi_violations(const ElfReport& report, const Policy& policy,
                           std::vector<std::string>& violations) {
    const std::array<std::pair<const char*, std::string>, 3U> limits{{
        {"GLIBC", policy.max_glibc},
        {"GLIBCXX", policy.max_glibcxx},
        {"CXXABI", policy.max_cxxabi},
    }};
    for (const auto& item : limits) {
        const std::string actual = detail::maximum_version(report, item.first);
        if (!item.second.empty() && !actual.empty() &&
            detail::version_less(item.second, actual)) {
            violations.push_back(std::string(item.first) + " requirement " + actual +
                                 " exceeds policy maximum " + item.second);
        }
    }
}

void append_absolute_path_violations(const std::vector<std::string>& paths,
                                     const char* label,
                                     std::vector<std::string>& violations) {
    for (const std::string& value : paths) {
        if (detail::is_absolute_path(value)) {
            violations.push_back(std::string("absolute ") + label + " is forbidden: " + value);
        }
    }
}

void append_dependency_violations(const ElfReport& report, const Policy& policy,
                                  std::vector<std::string>& violations) {
    for (const std::string& dependency : report.needed) {
        const bool forbidden = std::find(policy.forbidden_needed.begin(),
                                         policy.forbidden_needed.end(), dependency) !=
                               policy.forbidden_needed.end();
        if (forbidden) {
            violations.push_back("forbidden NEEDED dependency: " + dependency);
        }
    }
}

void require_valid_utf8(const std::string& line) {
    for (std::size_t offset = 0U; offset < line.size();) {
        const std::size_t length = detail::valid_utf8_sequence_length(line, offset);
        if (length == 0U) {
            throw std::runtime_error("policy file contains invalid UTF-8");
        }
        offset += length;
    }
}

bool parse_boolean(const std::string& value) {
    if (value == "true" || value == "1" || value == "yes") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no") {
        return false;
    }
    throw std::runtime_error("policy boolean is invalid: " + value);
}

void assign_forbidden_dependencies(Policy& policy, const std::string& value) {
    std::istringstream values(value);
    std::string dependency;
    while (std::getline(values, dependency, ',')) {
        dependency = detail::trim(dependency);
        if (!dependency.empty()) {
            policy.forbidden_needed.push_back(dependency);
        }
    }
    policy.forbidden_needed = detail::sorted_strings(policy.forbidden_needed);
}

void assign_policy_value(Policy& policy, const std::string& key,
                         const std::string& value) {
    if (key == "expected_class" || key == "class") {
        policy.expected_class = value;
    } else if (key == "expected_machine" || key == "machine") {
        policy.expected_machine = value;
    } else if (key == "max_glibc" || key == "glibc") {
        policy.max_glibc = value;
    } else if (key == "max_glibcxx" || key == "glibcxx") {
        policy.max_glibcxx = value;
    } else if (key == "max_cxxabi" || key == "cxxabi") {
        policy.max_cxxabi = value;
    } else if (key == "forbid_absolute_rpath") {
        policy.forbid_absolute_rpath = parse_boolean(value);
    } else if (key == "forbidden_needed") {
        assign_forbidden_dependencies(policy, value);
    } else {
        throw std::runtime_error("unknown policy key: " + key);
    }
}

void parse_policy_line(Policy& policy, std::string line) {
    require_valid_utf8(line);
    const std::size_t comment = line.find('#');
    if (comment != std::string::npos) {
        line.resize(comment);
    }
    line = detail::trim(line);
    if (line.empty()) {
        return;
    }
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) {
        throw std::runtime_error("policy line is missing '=': " + line);
    }
    assign_policy_value(policy, detail::trim(line.substr(0U, equals)),
                        detail::trim(line.substr(equals + 1U)));
}

void validate_policy_versions(const Policy& policy) {
    const std::array<std::pair<const char*, const std::string*>, 3U> versions{{
        {"max_glibc", &policy.max_glibc},
        {"max_glibcxx", &policy.max_glibcxx},
        {"max_cxxabi", &policy.max_cxxabi},
    }};
    for (const auto& item : versions) {
        if (!item.second->empty() && detail::version_parts(*item.second).empty()) {
            throw std::runtime_error(std::string(item.first) +
                                     " is not a numeric ABI version");
        }
    }
}

}  // namespace

PolicyEvaluation evaluate_policy(const ElfReport& report, const Policy& policy) {
    PolicyEvaluation result;
    result.applied = policy_applied(policy);
    if (report.status != InputStatus::Valid || !result.applied) {
        return result;
    }
    append_identity_violations(report, policy, result.violations);
    append_abi_violations(report, policy, result.violations);
    if (policy.forbid_absolute_rpath) {
        append_absolute_path_violations(report.rpath, "RPATH", result.violations);
        append_absolute_path_violations(report.runpath, "RUNPATH", result.violations);
    }
    append_dependency_violations(report, policy, result.violations);
    result.passed = result.violations.empty();
    return result;
}

Policy load_policy_file(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("could not open policy file: " + path.generic_string());
    }
    Policy policy;
    std::string line;
    std::size_t bytes = 0U;
    while (std::getline(stream, line)) {
        bytes += line.size() + 1U;
        if (bytes > 64U * 1024U) {
            throw std::runtime_error("policy file exceeds the 64 KiB bound");
        }
        parse_policy_line(policy, line);
    }
    validate_policy_versions(policy);
    return policy;
}

}  // namespace abilens
