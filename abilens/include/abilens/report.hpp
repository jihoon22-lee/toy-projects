#pragma once

#include <filesystem>
#include <string>

#include "abilens/model.hpp"

namespace abilens {

ElfReport inspect_file(const std::filesystem::path& path,
                       const Policy& policy = {});

PolicyEvaluation evaluate_policy(const ElfReport& report, const Policy& policy);

std::string serialize_report(const ElfReport& report);
ElfReport parse_report_json(const std::string& json);

Policy load_policy_file(const std::filesystem::path& path);

std::string render_report_text(const ElfReport& report);

}  // namespace abilens
