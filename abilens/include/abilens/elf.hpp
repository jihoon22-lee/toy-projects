#pragma once

#include <filesystem>

#include "abilens/model.hpp"

namespace abilens {

// Read only the ELF identification/header/program-header metadata.  This
// function never maps, loads, or executes the input file.
HeaderCheck validate_elf_file(const std::filesystem::path& path);

}  // namespace abilens
