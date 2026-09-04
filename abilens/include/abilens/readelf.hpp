#pragma once

#include <filesystem>
#include <string>

#include "abilens/model.hpp"

namespace abilens {

constexpr std::size_t kReadelfOutputLimit = 8U * 1024U * 1024U;
constexpr double kReadelfTimeoutSeconds = 30.0;

// Runs the fixed readelf command through fork/exec.  No shell is involved and
// the target path is passed as one argv element.  The child receives the C
// locale so parser output is stable across hosts.
ReadelfEvidence run_readelf(const std::filesystem::path& path);

// Parse bounded GNU readelf text after direct ELF header validation.
ElfReport parse_readelf_text(const std::string& text,
                             const ElfHeader& header,
                             const ReadelfEvidence& evidence);

}  // namespace abilens
