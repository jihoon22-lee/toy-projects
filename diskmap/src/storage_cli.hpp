#pragma once

#include <cstdint>
#include <iosfwd>
#include <set>
#include <string>

#include "diskmap/duplicates.hpp"
#include "diskmap/snapshot.hpp"

namespace diskmap_cli {

// Presentation filter for a computed snapshot diff. The comparison itself
// always stays complete; the filter only selects which changes are printed.
struct SnapshotDiffFilter {
    std::set<diskmap::SnapshotChangeKind> kinds;
    std::uint64_t min_delta_bytes = 0;
    bool certain_only = false;
};

// Escapes string content for inclusion between JSON quotes. Valid UTF-8 is
// preserved; malformed input bytes are encoded as \u00XX so filesystem names
// that are legal on POSIX cannot make a report invalid JSON.
std::string escapeJsonStringContent(const std::string& value);

void printSnapshotDiff(const diskmap::SnapshotDiff& diff,
                       bool json,
                       std::ostream& out,
                       const SnapshotDiffFilter& filter = SnapshotDiffFilter{});

void printDuplicateAnalysis(const diskmap::DuplicateAnalysis& analysis,
                            bool json,
                            std::ostream& out);

} // namespace diskmap_cli
