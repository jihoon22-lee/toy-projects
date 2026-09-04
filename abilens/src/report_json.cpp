#include "abilens/report.hpp"

#include "report_internal.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace abilens {

using detail::JsonValue;

namespace {

template <std::size_t N>
void require_exact_object(const JsonValue& value,
                          const char* context,
                          const std::array<const char*, N>& allowed) {
    if (value.kind != JsonValue::Kind::Object) {
        throw std::runtime_error(std::string("report JSON ") + context + " is not an object");
    }
    if (value.object.size() != N) {
        throw std::runtime_error(std::string("report JSON ") + context +
                                 " has an unexpected field count");
    }
    for (const auto& item : value.object) {
        const bool known = std::any_of(allowed.begin(), allowed.end(), [&](const char* key) {
            return item.first == key;
        });
        if (!known) {
            throw std::runtime_error(std::string("report JSON ") + context +
                                     " contains unknown field: " + item.first);
        }
    }
}

const JsonValue& required_field(const JsonValue& object, const char* name) {
    if (object.kind != JsonValue::Kind::Object) {
        throw std::runtime_error("report JSON value is not an object");
    }
    const auto iterator = object.object.find(name);
    if (iterator == object.object.end()) {
        throw std::runtime_error(std::string("report JSON is missing field: ") + name);
    }
    return iterator->second;
}

std::string required_string(const JsonValue& object, const char* name) {
    const JsonValue& value = required_field(object, name);
    if (value.kind != JsonValue::Kind::String) {
        throw std::runtime_error(std::string("report JSON field is not a string: ") + name);
    }
    return value.scalar;
}

bool required_boolean(const JsonValue& object, const char* name) {
    const JsonValue& value = required_field(object, name);
    if (value.kind != JsonValue::Kind::Boolean) {
        throw std::runtime_error(std::string("report JSON field is not a boolean: ") + name);
    }
    return value.boolean;
}

std::vector<std::string> required_string_array(const JsonValue& object, const char* name) {
    const JsonValue& value = required_field(object, name);
    if (value.kind != JsonValue::Kind::Array) {
        throw std::runtime_error(std::string("report JSON field is not an array: ") + name);
    }
    std::vector<std::string> result;
    for (const JsonValue& item : value.array) {
        if (item.kind != JsonValue::Kind::String) {
            throw std::runtime_error(std::string("report JSON array contains a non-string: ") + name);
        }
        if (std::find(result.begin(), result.end(), item.scalar) != result.end()) {
            throw std::runtime_error(std::string("report JSON array contains a duplicate: ") + name);
        }
        result.push_back(item.scalar);
    }
    return result;
}

InputStatus status_from_name(const std::string& value) {
    const std::array<std::pair<const char*, InputStatus>, 6U> names{{
        {"valid", InputStatus::Valid},       {"non-elf", InputStatus::NonElf},
        {"corrupt", InputStatus::Corrupt},   {"unsupported", InputStatus::Unsupported},
        {"unreadable", InputStatus::Unreadable}, {"tool-error", InputStatus::ToolError},
    }};
    for (const auto& item : names) {
        if (value == item.first) {
            return item.second;
        }
    }
    throw std::runtime_error("unknown report status: " + value);
}

bool valid_abi_version(const std::string& value) {
    return detail::version_parts(value).size() >= 2U;
}

bool valid_abi_namespace(const std::string& value) {
    return value == "GLIBC" || value == "GLIBCXX" || value == "CXXABI";
}


}  // namespace

ElfReport parse_report_json(const std::string& json) {
    const JsonValue root = detail::parse_json(json);
    require_exact_object(root, "root",
                         std::array<const char*, 10U>{"schema", "input", "status", "message",
                                                      "tool", "elf", "dependencies", "abi",
                                                      "policy", "diagnostics"});
    if (required_string(root, "schema") != ElfReport::schema) {
        throw std::runtime_error("unsupported AbiLens report schema");
    }
    ElfReport report;
    report.input = required_string(root, "input");
    report.status = status_from_name(required_string(root, "status"));
    report.message = required_string(root, "message");
    const JsonValue& tool = required_field(root, "tool");
    require_exact_object(tool, "tool", std::array<const char*, 2U>{"name", "version"});
    report.tool.name = required_string(tool, "name");
    report.tool.version = required_string(tool, "version");
    if (!report.tool.version.empty() && !valid_abi_version(report.tool.version)) {
        throw std::runtime_error("invalid readelf tool version in report JSON");
    }
    if (report.status == InputStatus::Valid && report.tool.name != "GNU readelf") {
        throw std::runtime_error("valid report does not identify GNU readelf");
    }
    if (report.status == InputStatus::Valid && !valid_abi_version(report.tool.version)) {
        throw std::runtime_error("valid report has no numeric GNU readelf version");
    }
    const JsonValue& elf = required_field(root, "elf");
    require_exact_object(elf, "elf",
                         std::array<const char*, 6U>{"class", "endian", "type", "machine",
                                                      "dynamic", "stripped"});
    report.header.elf_class = required_string(elf, "class");
    report.header.endian = required_string(elf, "endian");
    report.header.type = required_string(elf, "type");
    report.header.machine = required_string(elf, "machine");
    report.header.has_dynamic = required_boolean(elf, "dynamic");
    const std::string stripped = required_string(elf, "stripped");
    if (stripped == "yes") {
        report.stripped_known = true;
        report.stripped = true;
    } else if (stripped == "no") {
        report.stripped_known = true;
    } else if (stripped != "unknown") {
        throw std::runtime_error("invalid stripped value in report JSON");
    }
    const JsonValue& dependencies = required_field(root, "dependencies");
    require_exact_object(dependencies, "dependencies",
                         std::array<const char*, 3U>{"needed", "rpath", "runpath"});
    report.needed = required_string_array(dependencies, "needed");
    report.rpath = required_string_array(dependencies, "rpath");
    report.runpath = required_string_array(dependencies, "runpath");
    const JsonValue& abi = required_field(root, "abi");
    require_exact_object(abi, "abi", std::array<const char*, 2U>{"versions", "maximum"});
    const JsonValue& versions = required_field(abi, "versions");
    if (versions.kind != JsonValue::Kind::Array) {
        throw std::runtime_error("report ABI versions is not an array");
    }
    for (const JsonValue& item : versions.array) {
        require_exact_object(item, "ABI version",
                             std::array<const char*, 3U>{"namespace", "version", "library"});
        VersionRequirement requirement{required_string(item, "namespace"),
                                       required_string(item, "version"),
                                       required_string(item, "library")};
        if (!valid_abi_namespace(requirement.namespace_name)) {
            throw std::runtime_error("invalid ABI namespace in report JSON");
        }
        if (!valid_abi_version(requirement.version)) {
            throw std::runtime_error("invalid ABI version in report JSON");
        }
        const auto duplicate = std::find_if(
            report.versions.begin(), report.versions.end(),
            [&](const VersionRequirement& existing) {
                return existing.namespace_name == requirement.namespace_name &&
                       existing.version == requirement.version &&
                       existing.library == requirement.library;
            });
        if (duplicate != report.versions.end()) {
            throw std::runtime_error("duplicate ABI version in report JSON");
        }
        report.versions.push_back(std::move(requirement));
    }
    const JsonValue& maximum = required_field(abi, "maximum");
    require_exact_object(maximum, "ABI maximum",
                         std::array<const char*, 3U>{"GLIBC", "GLIBCXX", "CXXABI"});
    for (const char* namespace_name :
         std::array<const char*, 3U>{"GLIBC", "GLIBCXX", "CXXABI"}) {
        const std::string actual = required_string(maximum, namespace_name);
        if (!actual.empty() && !valid_abi_version(actual)) {
            throw std::runtime_error("invalid ABI maximum in report JSON");
        }
        if (actual != detail::maximum_version(report, namespace_name)) {
            throw std::runtime_error("ABI maximum does not match version requirements");
        }
    }
    const JsonValue& policy = required_field(root, "policy");
    require_exact_object(policy, "policy",
                         std::array<const char*, 3U>{"applied", "passed", "violations"});
    report.policy.applied = required_boolean(policy, "applied");
    report.policy.passed = required_boolean(policy, "passed");
    report.policy.violations = required_string_array(policy, "violations");
    report.diagnostics = required_string_array(root, "diagnostics");
    return report;
}


}  // namespace abilens
