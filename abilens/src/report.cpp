#include "abilens/report.hpp"

#include "abilens/elf.hpp"
#include "abilens/inspect.hpp"
#include "input_internal.hpp"
#include "evidence_internal.hpp"
#include "report_internal.hpp"

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>

namespace abilens {

using detail::json_bool;
using detail::json_escape;
using detail::json_string_array;
using detail::maximum_version;
using detail::sorted_strings;
using detail::version_less;

ElfReport inspect_file(const std::filesystem::path& path, const Policy& policy, const InspectOptions& options) {
    ElfReport report;
    report.input = path.generic_string();
    const detail::OpenInput input(path);
    const HeaderCheck check = detail::validate_elf_input(input);
    report.status = check.status;
    report.header = check.header;
    report.message = check.message;
    if (check.status != InputStatus::Valid) {
        if (!check.message.empty()) {
            report.diagnostics.push_back(check.message);
        }
        return report;
    }
    report = detail::inspect_elf_input(input, check.header);
    report.input = path.generic_string();
    if (report.status == InputStatus::Valid) {
        detail::inspect_dwarf(input, report, options);
        detail::resolve_loader(report, options);
        report.policy = evaluate_policy(report, policy);
        if (!report.policy.passed) {
            report.message = "ELF evidence verified; policy violations were found";
        }
    }
    return report;
}


std::string serialize_report(const ElfReport& report) {
    const std::vector<std::string> needed = sorted_strings(report.needed);
    const std::vector<std::string>& rpath = report.rpath;
    const std::vector<std::string>& runpath = report.runpath;
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
           << ",\"runpath\":" << json_string_array(runpath)
           << '}';
    if (report.symbols_known) output << ",\"symbols\":" << json_string_array(sorted_strings(report.symbols));
    if (report.vtables_known) output << ",\"vtables\":" << json_string_array(sorted_strings(report.vtables));
    output << ",\"abi\":{\"versions\":[";
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
           << "},\"evidence\":" << detail::serialize_evidence(report)
           << ",\"diagnostics\":" << json_string_array(diagnostics) << '}';
    const auto serialized = output.str();
    if (serialized.size() > detail::kMaxReportBytes) throw std::runtime_error("report exceeds 8 MiB output budget");
    // Enforce the same structural budget on output as on input. Never emit a
    // report that a later offline diff rejects solely because of its shape.
    (void)detail::parse_json(serialized);
    return serialized;
}

namespace {

void append_text_values(std::ostringstream& output, const char* label,
                        const std::vector<std::string>& values) {
    output << "  " << label << ": " << (values.empty() ? "(none)" : "") << "\n";
    for (const std::string& value : values) {
        output << "    - " << value << "\n";
    }
}

void append_elf_text(std::ostringstream& output, const ElfReport& report) {
    output << "  ELF: " << report.header.elf_class << ", " << report.header.endian << ", "
           << report.header.type << ", " << report.header.machine << "\n"
           << "  linkage: " << (report.header.has_dynamic ? "dynamic" : "static/non-dynamic")
           << ", stripped="
           << (report.stripped_known ? (report.stripped ? "yes" : "no") : "unknown") << "\n";
    append_text_values(output, "NEEDED", report.needed);
    append_text_values(output, "RPATH", report.rpath);
    append_text_values(output, "RUNPATH", report.runpath);
    output << "  SONAME: " << report.soname << "\n  interpreter: " << report.interpreter
           << "\n  build ID: " << report.build_id << "\n  DWARF: " << report.dwarf_status << "\n";
    output << "  dynamic symbols: " << report.symbols.size() << "\n";
    output << "  vtable symbols: " << report.vtables.size() << "\n";
    output << "  ABI maximums: GLIBC=" << maximum_version(report, "GLIBC")
           << " GLIBCXX=" << maximum_version(report, "GLIBCXX")
           << " CXXABI=" << maximum_version(report, "CXXABI") << "\n";
}

void append_policy_text(std::ostringstream& output, const PolicyEvaluation& policy) {
    if (!policy.applied) return;
    output << "  policy: " << (policy.passed ? "PASS" : "FAIL") << "\n";
    for (const std::string& violation : policy.violations) {
        output << "    ! " << violation << "\n";
    }
}

void append_diagnostic_text(std::ostringstream& output,
                            const std::vector<std::string>& diagnostics) {
    for (const std::string& diagnostic : diagnostics) {
        output << "  note: " << diagnostic << "\n";
    }
}

}  // namespace


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
        append_elf_text(output, report);
    }
    append_policy_text(output, report.policy);
    append_diagnostic_text(output, report.diagnostics);
    return output.str();
}
}  // namespace abilens
