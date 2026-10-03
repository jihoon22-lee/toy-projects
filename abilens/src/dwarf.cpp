#include <algorithm>
#include <chrono>
#include <set>
#include <sstream>
#include <tuple>

#include "evidence_internal.hpp"
#ifdef ABILENS_HAVE_LIBDW
#include <dwarf.h>
#include <elfutils/libdw.h>
#include <gelf.h>
#include <libelf.h>
#endif
namespace abilens::detail {
#ifdef ABILENS_HAVE_LIBDW
namespace {
std::string attribute(Dwarf_Die& die, unsigned code, bool& complete) {
    Dwarf_Attribute storage;
    auto* attr = dwarf_attr_integrate(&die, code, &storage);
    if (!attr) return "-";
    Dwarf_Word value = 0;
    if (dwarf_formudata(attr, &value) != 0) {
        complete = false;
        return "unknown";
    }
    return std::to_string(value);
}
std::string type_name(Dwarf_Die& die, unsigned depth = 0) {
    if (depth >= 16) return "<recursive>";
    const char* name = dwarf_diename(&die);
    if (name) return std::string(name).substr(0, 1024);
    Dwarf_Attribute attr;
    Dwarf_Die referred;
    auto* value = dwarf_attr_integrate(&die, DW_AT_type, &attr);
    return std::to_string(dwarf_tag(&die)) + ":" +
           (value && dwarf_formref_die(value, &referred) ? type_name(referred, depth + 1) : "void");
}
}  // namespace
#endif
void inspect_dwarf(const OpenInput& input, ElfReport& report, const InspectOptions& options) {
    if (!options.dwarf) return;
#ifndef ABILENS_HAVE_LIBDW
    (void)input;
    report.dwarf_status = "unavailable";
    report.diagnostics.push_back("DWARF requested but this build has no libdw support");
#else
    if (input.size() > 128U * 1024U * 1024U) {
        report.dwarf_status = "limited";
        return;
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(input.size()));
    if (!input.read_exact(0, bytes.data(), bytes.size()) || !input.unchanged()) {
        report.dwarf_status = "input-changed";
        return;
    }
    elf_version(EV_CURRENT);
    Elf* elf = elf_memory(reinterpret_cast<char*>(bytes.data()), bytes.size());
    if (elf) {
        std::size_t strings = 0;
        if (elf_getshdrstrndx(elf, &strings) != 0) {
            report.dwarf_status = "partial";
            elf_end(elf);
            return;
        }
        Elf_Scn* section = nullptr;
        while ((section = elf_nextscn(elf, section))) {
            GElf_Shdr header;
            const bool valid_header = gelf_getshdr(section, &header) != nullptr;
            const char* section_name =
                valid_header ? elf_strptr(elf, strings, header.sh_name) : nullptr;
            if (!valid_header || (header.sh_flags & SHF_COMPRESSED) ||
                (section_name && std::string(section_name).rfind(".zdebug", 0) == 0)) {
                report.dwarf_status = "limited";
                report.diagnostics.push_back(
                    "compressed or unreadable sections are outside bounded DWARF analysis");
                elf_end(elf);
                return;
            }
        }
    }
    Dwarf* dwarf = elf ? dwarf_begin_elf(elf, DWARF_C_READ, nullptr) : nullptr;
    if (!dwarf) {
        report.dwarf_status = "absent";
        if (elf) elf_end(elf);
        return;
    }
    bool complete = true;
    bool limited = false;
    std::size_t visited = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::set<std::string> layouts;
    std::size_t layout_bytes = 0;
    Dwarf_CU* unit = nullptr;
    Dwarf_CU* next_unit = nullptr;
    Dwarf_Die cu;
    std::uint8_t unit_type = 0;
    int cu_result = 0;
    while ((cu_result =
                dwarf_get_units(dwarf, unit, &next_unit, nullptr, &unit_type, &cu, nullptr)) == 0) {
        if (next_unit == unit || dwarf_tag(&cu) <= 0) {
            complete = false;
            break;
        }
        if (unit_type != DW_UT_compile && unit_type != DW_UT_type) complete = false;
        std::vector<std::tuple<Dwarf_Die, unsigned, std::string> > stack{{cu, 0, ""}};
        while (!stack.empty()) {
            auto [die, depth, scope] = stack.back();
            stack.pop_back();
            if (++visited > options.dwarf_die_budget || depth > 128 || scope.size() > 4096 ||
                std::chrono::steady_clock::now() > deadline) {
                limited = true;
                break;
            }
            const int tag = dwarf_tag(&die);
            if (tag == DW_TAG_structure_type || tag == DW_TAG_class_type ||
                tag == DW_TAG_union_type) {
                Dwarf_Word bytesize = 0;
                const char* name = dwarf_diename(&die);
                if (name && dwarf_aggregate_size(&die, &bytesize) == 0) {
                    std::ostringstream layout;
                    layout << scope << std::string(name).substr(0, 1024) << "|size=" << bytesize;
                    Dwarf_Die member;
                    int child_result = dwarf_child(&die, &member);
                    if (child_result < 0) complete = false;
                    if (child_result == 0) do {
                            if (++visited > options.dwarf_die_budget) {
                                limited = true;
                                break;
                            }
                            const int member_tag = dwarf_tag(&member);
                            if (member_tag != DW_TAG_member && member_tag != DW_TAG_inheritance)
                                continue;
                            const char* member_name = dwarf_diename(&member);
                            Dwarf_Attribute attr;
                            Dwarf_Die type;
                            auto* reference = dwarf_attr_integrate(&member, DW_AT_type, &attr);
                            layout << '|'
                                   << (member_name ? std::string(member_name).substr(0, 1024)
                                                   : "<base>")
                                   << ':'
                                   << (reference && dwarf_formref_die(reference, &type)
                                           ? type_name(type)
                                           : "unknown")
                                   << "@" << attribute(member, DW_AT_data_member_location, complete)
                                   << ":bits=" << attribute(member, DW_AT_bit_size, complete)
                                   << ":bit-offset="
                                   << attribute(member, DW_AT_data_bit_offset, complete)
                                   << ":legacy-bit-offset="
                                   << attribute(member, DW_AT_bit_offset, complete);
                            if (layout.tellp() > 65536) {
                                limited = true;
                                break;
                            }
                        } while (dwarf_siblingof(&member, &member) == 0);
                    const auto serialized = layout.str();
                    if (serialized.size() > 65536 ||
                        layout_bytes + serialized.size() > 4U * 1024U * 1024U) {
                        limited = true;
                        break;
                    }
                    if (layouts.insert(serialized).second) layout_bytes += serialized.size();
                    if (layouts.size() >= 10000) limited = true;
                }
            }
            if (limited) break;
            Dwarf_Die sibling, child;
            const int sibling_result = dwarf_siblingof(&die, &sibling);
            const int child_result = dwarf_child(&die, &child);
            if (sibling_result == 0) stack.emplace_back(sibling, depth, scope);
            if (child_result == 0) {
                const char* scope_name = dwarf_diename(&die);
                if (scope_name && (tag == DW_TAG_namespace || tag == DW_TAG_structure_type ||
                                   tag == DW_TAG_class_type || tag == DW_TAG_union_type)) {
                    scope += std::string(scope_name).substr(0, 1024) + "::";
                }
                stack.emplace_back(child, depth + 1, std::move(scope));
            }
            if (sibling_result < 0 || child_result < 0) complete = false;
        }
        if (limited) break;
        unit = next_unit;
    }
    if (cu_result < 0) complete = false;
    report.type_layouts.assign(layouts.begin(), layouts.end());
    report.dwarf_status = limited ? "limited" : complete ? "complete" : "partial";
    if (!input.unchanged()) report.dwarf_status = "input-changed";
    report.diagnostics.push_back(
        "DWARF layouts cover observed named aggregate types; public API reachability is not "
        "inferred");
    dwarf_end(dwarf);
    elf_end(elf);
#endif
}
}  // namespace abilens::detail
