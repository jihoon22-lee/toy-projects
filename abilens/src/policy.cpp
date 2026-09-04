#include "abilens/report.hpp"

#include "report_internal.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace abilens {

PolicyEvaluation evaluate_policy(const ElfReport& report, const Policy& policy) {
    PolicyEvaluation result;
    result.applied = !policy.expected_class.empty() || !policy.expected_machine.empty() ||
                     !policy.max_glibc.empty() || !policy.max_glibcxx.empty() ||
                     !policy.max_cxxabi.empty() || policy.forbid_absolute_rpath ||
                     !policy.forbidden_needed.empty();
    if (report.status != InputStatus::Valid || !result.applied) {
        return result;
    }
    if (!policy.expected_class.empty() && report.header.elf_class != policy.expected_class) {
        result.violations.push_back("ELF class " + report.header.elf_class + " is not " +
                                    policy.expected_class);
    }
    if (!policy.expected_machine.empty() && report.header.machine != policy.expected_machine) {
        result.violations.push_back("ELF machine " + report.header.machine + " is not " +
                                    policy.expected_machine);
    }
    const std::array<std::pair<const char*, std::string>, 3U> limits{{
        {"GLIBC", policy.max_glibc}, {"GLIBCXX", policy.max_glibcxx}, {"CXXABI", policy.max_cxxabi},
    }};
    for (const auto& item : limits) {
        if (item.second.empty()) {
            continue;
        }
        const std::string actual = detail::maximum_version(report, item.first);
        if (!actual.empty() && detail::version_less(item.second, actual)) {
            result.violations.push_back(std::string(item.first) + " requirement " + actual +
                                        " exceeds policy maximum " + item.second);
        }
    }
    if (policy.forbid_absolute_rpath) {
        for (const std::string& value : report.rpath) {
            if (detail::is_absolute_path(value)) {
                result.violations.push_back("absolute RPATH is forbidden: " + value);
            }
        }
        for (const std::string& value : report.runpath) {
            if (detail::is_absolute_path(value)) {
                result.violations.push_back("absolute RUNPATH is forbidden: " + value);
            }
        }
    }
    for (const std::string& dependency : report.needed) {
        if (std::find(policy.forbidden_needed.begin(), policy.forbidden_needed.end(), dependency) !=
            policy.forbidden_needed.end()) {
            result.violations.push_back("forbidden NEEDED dependency: " + dependency);
        }
    }
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
    std::size_t bytes = 0;
    while (std::getline(stream, line)) {
        bytes += line.size() + 1U;
        if (bytes > 64U * 1024U) {
            throw std::runtime_error("policy file exceeds the 64 KiB bound");
        }
        for (std::size_t offset = 0U; offset < line.size();) {
            const std::size_t length = detail::valid_utf8_sequence_length(line, offset);
            if (length == 0U) {
                throw std::runtime_error("policy file contains invalid UTF-8");
            }
            offset += length;
        }
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.resize(comment);
        }
        line = detail::trim(line);
        if (line.empty()) {
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            throw std::runtime_error("policy line is missing '=': " + line);
        }
        const std::string key = detail::trim(line.substr(0U, equals));
        const std::string value = detail::trim(line.substr(equals + 1U));
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
            if (value == "true" || value == "1" || value == "yes") {
                policy.forbid_absolute_rpath = true;
            } else if (value == "false" || value == "0" || value == "no") {
                policy.forbid_absolute_rpath = false;
            } else {
                throw std::runtime_error("policy boolean is invalid: " + value);
            }
        } else if (key == "forbidden_needed") {
            std::istringstream values(value);
            std::string dependency;
            while (std::getline(values, dependency, ',')) {
                dependency = detail::trim(dependency);
                if (!dependency.empty()) {
                    policy.forbidden_needed.push_back(dependency);
                }
            }
            policy.forbidden_needed = detail::sorted_strings(policy.forbidden_needed);
        } else {
            throw std::runtime_error("unknown policy key: " + key);
        }
    }
    for (const auto& item : std::array<std::pair<const char*, const std::string*>, 3U>{{
             {"max_glibc", &policy.max_glibc},
             {"max_glibcxx", &policy.max_glibcxx},
             {"max_cxxabi", &policy.max_cxxabi},
         }}) {
        if (!item.second->empty() && detail::version_parts(*item.second).empty()) {
            throw std::runtime_error(std::string(item.first) + " is not a numeric ABI version");
        }
    }
    return policy;
}


}  // namespace abilens
