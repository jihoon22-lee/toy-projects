#pragma once
#include "abilens/model.hpp"
#include "input_internal.hpp"
#include "report_internal.hpp"
namespace abilens::detail {
void inspect_dwarf(const OpenInput& input, ElfReport& report, const InspectOptions& options);
void resolve_loader(ElfReport& report, const InspectOptions& options);
std::string serialize_evidence(const ElfReport& report);
void parse_evidence(const JsonValue& value, ElfReport& report);
}  // namespace abilens::detail
