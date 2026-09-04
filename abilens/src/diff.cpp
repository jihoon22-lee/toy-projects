#include "abilens/diff.hpp"

#include "report_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>

namespace abilens {
namespace {

std::vector<std::string> sorted_unique(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

SetDiff make_set_diff(const std::vector<std::string>& left,
                      const std::vector<std::string>& right) {
    const std::vector<std::string> left_values = sorted_unique(left);
    const std::vector<std::string> right_values = sorted_unique(right);
    SetDiff result;
    std::set_difference(right_values.begin(), right_values.end(), left_values.begin(),
                        left_values.end(), std::back_inserter(result.added));
    std::set_difference(left_values.begin(), left_values.end(), right_values.begin(),
                        right_values.end(), std::back_inserter(result.removed));
    return result;
}

bool version_less(const std::string& left, const std::string& right) {
    std::istringstream left_stream(left);
    std::istringstream right_stream(right);
    std::string left_part;
    std::string right_part;
    for (;;) {
        const bool has_left = static_cast<bool>(std::getline(left_stream, left_part, '.'));
        const bool has_right = static_cast<bool>(std::getline(right_stream, right_part, '.'));
        if (!has_left && !has_right) {
            return false;
        }
        const unsigned long long left_value = has_left ? std::stoull(left_part) : 0U;
        const unsigned long long right_value = has_right ? std::stoull(right_part) : 0U;
        if (left_value != right_value) {
            return left_value < right_value;
        }
    }
}

std::string max_version(const ElfReport& report, const std::string& namespace_name) {
    std::string result;
    for (const VersionRequirement& item : report.versions) {
        if (item.namespace_name != namespace_name) {
            continue;
        }
        if (result.empty() || version_less(result, item.version)) {
            result = item.version;
        }
    }
    return result;
}

std::vector<std::string> abi_keys(const ElfReport& report) {
    std::vector<std::string> result;
    for (const VersionRequirement& item : report.versions) {
        result.push_back(item.namespace_name + "_" + item.version + " [" + item.library + "]");
    }
    return result;
}

void append_set_json(std::ostringstream& output, const SetDiff& diff) {
    auto write_array = [&](const std::vector<std::string>& values) {
        output << '[';
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index != 0U) {
                output << ',';
            }
            output << detail::json_escape(values[index]);
        }
        output << ']';
    };
    output << "{\"added\":";
    write_array(diff.added);
    output << ",\"removed\":";
    write_array(diff.removed);
    output << '}';
}

bool set_changed(const SetDiff& diff) {
    return !diff.added.empty() || !diff.removed.empty();
}

}  // namespace

DiffReport diff_reports(const ElfReport& left, const ElfReport& right) {
    DiffReport result;
    result.left = left.input;
    result.right = right.input;
    result.left_status = input_status_name(left.status);
    result.right_status = input_status_name(right.status);
    result.needed = make_set_diff(left.needed, right.needed);
    result.rpath = make_set_diff(left.rpath, right.rpath);
    result.runpath = make_set_diff(left.runpath, right.runpath);
    result.abi = make_set_diff(abi_keys(left), abi_keys(right));
    if (left.status != right.status) {
        result.header_changes.push_back("input status changed");
    }
    if (left.status == InputStatus::Valid && right.status == InputStatus::Valid) {
        if (left.header.elf_class != right.header.elf_class) {
            result.header_changes.push_back("ELF class: " + left.header.elf_class + " -> " +
                                            right.header.elf_class);
        }
        if (left.header.endian != right.header.endian) {
            result.header_changes.push_back("endianness: " + left.header.endian + " -> " +
                                            right.header.endian);
        }
        if (left.header.machine != right.header.machine) {
            result.header_changes.push_back("machine: " + left.header.machine + " -> " +
                                            right.header.machine);
        }
        if (left.header.type != right.header.type) {
            result.header_changes.push_back("type: " + left.header.type + " -> " + right.header.type);
        }
        if (left.header.has_dynamic != right.header.has_dynamic) {
            result.header_changes.push_back("linkage: dynamic/static changed");
        }
        const std::array<std::string, 3U> namespaces{"GLIBC", "GLIBCXX", "CXXABI"};
        for (const std::string& namespace_name : namespaces) {
            const std::string left_max = max_version(left, namespace_name);
            const std::string right_max = max_version(right, namespace_name);
            if (left_max != right_max) {
                result.header_changes.push_back(namespace_name + " maximum: " +
                                                (left_max.empty() ? "(none)" : left_max) + " -> " +
                                                (right_max.empty() ? "(none)" : right_max));
                if (left_max.empty() || (!right_max.empty() && version_less(left_max, right_max))) {
                    result.compatible = false;
                }
            }
        }
        if (left.header.elf_class != right.header.elf_class ||
            left.header.endian != right.header.endian || left.header.machine != right.header.machine ||
            left.header.type != right.header.type ||
            left.header.has_dynamic != right.header.has_dynamic) {
            result.compatible = false;
        }
    } else {
        result.compatible = false;
    }
    result.changed = !result.header_changes.empty() || set_changed(result.needed) ||
                     set_changed(result.rpath) || set_changed(result.runpath) ||
                     set_changed(result.abi);
    if (left.policy.passed != right.policy.passed) {
        result.diagnostics.push_back("policy result changed");
        result.changed = true;
    }
    return result;
}

std::string serialize_diff(const DiffReport& diff) {
    std::ostringstream output;
    output << "{\"schema\":" << detail::json_escape(DiffReport::schema)
           << ",\"left\":" << detail::json_escape(diff.left)
           << ",\"right\":" << detail::json_escape(diff.right)
           << ",\"changed\":" << (diff.changed ? "true" : "false")
           << ",\"compatible\":" << (diff.compatible ? "true" : "false")
           << ",\"left_status\":" << detail::json_escape(diff.left_status)
           << ",\"right_status\":" << detail::json_escape(diff.right_status)
           << ",\"header_changes\":[";
    for (std::size_t index = 0; index < diff.header_changes.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        output << detail::json_escape(diff.header_changes[index]);
    }
    output << "],\"dependencies\":{\"needed\":";
    append_set_json(output, diff.needed);
    output << ",\"rpath\":";
    append_set_json(output, diff.rpath);
    output << ",\"runpath\":";
    append_set_json(output, diff.runpath);
    output << "},\"abi\":";
    append_set_json(output, diff.abi);
    output << ",\"diagnostics\":[";
    for (std::size_t index = 0; index < diff.diagnostics.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        output << detail::json_escape(diff.diagnostics[index]);
    }
    output << "]}";
    return output.str();
}

std::string render_diff_text(const DiffReport& diff) {
    std::ostringstream output;
    output << "AbiLens diff\n"
           << "  left: " << diff.left << " (" << diff.left_status << ")\n"
           << "  right: " << diff.right << " (" << diff.right_status << ")\n"
           << "  result: " << (diff.changed ? "CHANGED" : "IDENTICAL")
           << ", compatibility: " << (diff.compatible ? "compatible" : "incompatible") << "\n";
    for (const std::string& change : diff.header_changes) {
        output << "  header: " << change << "\n";
    }
    const std::array<std::pair<const char*, const SetDiff*>, 4U> sets{{
        {"NEEDED", &diff.needed}, {"RPATH", &diff.rpath}, {"RUNPATH", &diff.runpath},
        {"ABI", &diff.abi},
    }};
    for (const auto& item : sets) {
        for (const std::string& value : item.second->added) {
            output << "  + " << item.first << ": " << value << "\n";
        }
        for (const std::string& value : item.second->removed) {
            output << "  - " << item.first << ": " << value << "\n";
        }
    }
    for (const std::string& diagnostic : diff.diagnostics) {
        output << "  note: " << diagnostic << "\n";
    }
    return output.str();
}

}  // namespace abilens
