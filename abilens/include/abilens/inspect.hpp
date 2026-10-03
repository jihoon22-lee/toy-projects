#pragma once

#include <vector>

#include "abilens/model.hpp"

namespace abilens {

// Upper bound on inputs inspected in memory.  Real binaries are far smaller;
// anything larger is rejected as a tool error rather than parsed partially.
constexpr std::uint64_t kInspectMaxFileSize = 256U * 1024U * 1024U;

// Parses ELF metadata from the complete contents of one file.  The bytes must
// already satisfy validate_elf_input: a valid identification, header layout,
// and in-file program/section header tables.  Program headers and dynamic
// tags are the source of truth, so dependency and version evidence is still
// recovered when section headers are stripped; sections are only consulted
// for the .symtab strippedness check.
ElfReport inspect_elf_buffer(const std::vector<unsigned char>& file,
                             const ElfHeader& header);

}  // namespace abilens
