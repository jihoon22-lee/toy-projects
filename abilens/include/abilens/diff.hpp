#pragma once

#include "abilens/model.hpp"

namespace abilens {

DiffReport diff_reports(const ElfReport& left, const ElfReport& right);
std::string serialize_diff(const DiffReport& diff);
std::string render_diff_text(const DiffReport& diff);

}  // namespace abilens
