#include "abilens/report.hpp"

#include "abilens/elf.hpp"
#include "abilens/readelf.hpp"
#include "report_internal.hpp"

#include <algorithm>
#include <array>
#include <sstream>

namespace abilens {

using detail::json_bool;
using detail::json_escape;
using detail::json_string_array;
using detail::maximum_version;
using detail::sorted_strings;
using detail::version_less;

ElfReport inspect_file(const std::filesystem::path& path, const Policy& policy) {
    ElfReport report;
    report.input = path.generic_string();
    const HeaderCheck check = validate_elf_file(path);
    report.status = check.status;
    report.header = check.header;
    report.message = check.message;
    if (check.status != InputStatus::Valid) {
        if (!check.message.empty()) {
            report.diagnostics.push_back(check.message);
        }
        return report;
    }
    const ReadelfEvidence evidence = run_readelf(path);
    report = parse_readelf_text(evidence.standard_output, check.header, evidence);
    report.input = path.generic_string();
    if (report.status == InputStatus::Valid) {
        report.policy = evaluate_policy(report, policy);
        if (!report.policy.passed) {
            report.message = "ELF evidence verified; policy violations were found";
        }
    }
    return report;
}


std::string serialize_report(const ElfReport& report) {
    const std::vector<std::string> needed = sorted_strings(report.needed);
    const std::vector<std::string> rpath = sorted_strings(report.rpath);
    const std::vector<std::string> runpath = sorted_strings(report.runpath);
    std::vector<VersionRequirement> versions = report.versions;
    std::sort(versions.begin(), versions.end(), [](const VersionRequirement& left,
                                                   const VersionRequirement& right) {
        if (left.namespace_name != right.namespace_name) {
            return left.namespace_name < right.namespace_name;
        }
        if (left.version != right.version) {
            return version_less(left.version, right.version);
        }
        return left.library < right.library;
    });
    versions.erase(std::unique(versions.begin(), versions.end(),
                               [](const VersionRequirement& left,
                                  const VersionRequirement& right) {
                                   return left.namespace_name == right.namespace_name &&
                                          left.version == right.version &&
                                          left.library == right.library;
                               }),
                   versions.end());
    const std::vector<std::string> diagnostics = sorted_strings(report.diagnostics);
    const std::vector<std::string> policy_violations = sorted_strings(report.policy.violations);
    std::ostringstream output;
    output << "{\"schema\":" << json_escape(ElfReport::schema)
           << ",\"input\":" << json_escape(report.input)
           << ",\"status\":" << json_escape(input_status_name(report.status))
           << ",\"message\":" << json_escape(report.message)
           << ",\"tool\":{\"name\":" << json_escape(report.tool.name)
           << ",\"version\":" << json_escape(report.tool.version)
           << "},\"elf\":{\"class\":" << json_escape(report.header.elf_class)
           << ",\"endian\":" << json_escape(report.header.endian)
           << ",\"type\":" << json_escape(report.header.type)
           << ",\"machine\":" << json_escape(report.header.machine)
           << ",\"dynamic\":" << json_bool(report.header.has_dynamic)
           << ",\"stripped\":"
           << json_escape(report.stripped_known ? (report.stripped ? "yes" : "no") : "unknown")
           << "},\"dependencies\":{\"needed\":" << json_string_array(needed)
           << ",\"rpath\":" << json_string_array(rpath)
           << ",\"runpath\":" << json_string_array(runpath) << "},\"abi\":{\"versions\":[";
    for (std::size_t index = 0; index < versions.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        output << "{\"namespace\":" << json_escape(versions[index].namespace_name)
               << ",\"version\":" << json_escape(versions[index].version)
               << ",\"library\":" << json_escape(versions[index].library) << '}';
    }
    output << "],\"maximum\":{\"GLIBC\":"
           << json_escape(maximum_version(report, "GLIBC"))
           << ",\"GLIBCXX\":" << json_escape(maximum_version(report, "GLIBCXX"))
           << ",\"CXXABI\":" << json_escape(maximum_version(report, "CXXABI"))
           << "}},\"policy\":{\"applied\":" << json_bool(report.policy.applied)
           << ",\"passed\":" << json_bool(report.policy.passed)
           << ",\"violations\":" << json_string_array(policy_violations)
           << "},\"diagnostics\":" << json_string_array(diagnostics) << '}';
    return output.str();
}


std::string render_report_text(const ElfReport& report) {
    std::ostringstream output;
    output << "AbiLens report\n"
           << "  input: " << report.input << "\n"
           << "  status: " << input_status_name(report.status) << "\n"
           << "  tool: " << (report.tool.name.empty() ? "(unavailable)" : report.tool.name)
           << (report.tool.version.empty() ? "" : " " + report.tool.version) << "\n";
    if (!report.message.empty()) {
        output << "  message: " << report.message << "\n";
    }
    if (report.status == InputStatus::Valid) {
        output << "  ELF: " << report.header.elf_class << ", " << report.header.endian << ", "
               << report.header.type << ", " << report.header.machine << "\n"
               << "  linkage: " << (report.header.has_dynamic ? "dynamic" : "static/non-dynamic")
               << ", stripped="
               << (report.stripped_known ? (report.stripped ? "yes" : "no") : "unknown") << "\n"
               << "  NEEDED: " << (report.needed.empty() ? "(none)" : "") << "\n";
        for (const std::string& value : report.needed) {
            output << "    - " << value << "\n";
        }
        output << "  RPATH: " << (report.rpath.empty() ? "(none)" : "") << "\n";
        for (const std::string& value : report.rpath) {
            output << "    - " << value << "\n";
        }
        output << "  RUNPATH: " << (report.runpath.empty() ? "(none)" : "") << "\n";
        for (const std::string& value : report.runpath) {
            output << "    - " << value << "\n";
        }
        output << "  ABI maximums: GLIBC=" << maximum_version(report, "GLIBC")
               << " GLIBCXX=" << maximum_version(report, "GLIBCXX")
               << " CXXABI=" << maximum_version(report, "CXXABI") << "\n";
    }
    if (report.policy.applied) {
        output << "  policy: " << (report.policy.passed ? "PASS" : "FAIL") << "\n";
        for (const std::string& violation : report.policy.violations) {
            output << "    ! " << violation << "\n";
        }
    }
    for (const std::string& diagnostic : report.diagnostics) {
        output << "  note: " << diagnostic << "\n";
    }
    return output.str();
}
}  // namespace abilens
