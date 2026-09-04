#pragma once

#include <iosfwd>
#include <string>

#include "diskmap/duplicates.hpp"
#include "diskmap/snapshot.hpp"

namespace diskmap_cli {

// Escapes string content for inclusion between JSON quotes. Valid UTF-8 is
// preserved; malformed input bytes are encoded as \u00XX so filesystem names
// that are legal on POSIX cannot make a report invalid JSON.
std::string escapeJsonStringContent(const std::string& value);

void printSnapshotDiff(const diskmap::SnapshotDiff& diff,
                       bool json,
                       std::ostream& out);

void printDuplicateAnalysis(const diskmap::DuplicateAnalysis& analysis,
                            bool json,
                            std::ostream& out);

} // namespace diskmap_cli
