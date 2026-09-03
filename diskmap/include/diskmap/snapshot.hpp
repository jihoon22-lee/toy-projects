#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "diskmap/scanner.hpp"
#include "diskmap/view.hpp"

namespace diskmap {

// Snapshot files are intentionally a separate contract from ScanResult.  A
// scan generation identifies an in-memory worker result and must never become
// part of a file that is compared with a later scan.
inline constexpr const char* kSnapshotSchemaV1 = "diskmap.snapshot/v1";
inline constexpr const char* kSnapshotSchema = kSnapshotSchemaV1;

struct SnapshotLimits {
    // A root is always retained, so this must be at least one.
    std::size_t max_nodes = 100'000;
    // The value is clamped to kMaxTreeDepth by the implementation.  Zero
    // retains the root but not its descendants.
    std::size_t max_depth = kMaxTreeDepth;
    // Bounds both serializer output and parser input.  This is deliberately
    // independent from the scanner's node bound.
    std::size_t max_serialized_bytes = 64U * 1024U * 1024U;
    // Each user-controlled path, name, and diagnostic is bounded separately.
    std::size_t max_string_bytes = 1U * 1024U * 1024U;
};

class SnapshotError final : public std::runtime_error {
public:
    explicit SnapshotError(const std::string& message) : std::runtime_error(message) {}
};

struct Snapshot {
    std::string schema_version = kSnapshotSchemaV1;
    FsNode root;
    bool complete = true;
    bool truncated = false;
    // Populated by snapshotFromNode() and parseSnapshot().  Serializers
    // recompute it so hand-built values cannot write a stale count.
    std::size_t nodes_retained = 0;
};

// Copies a bounded, value-owned snapshot from a scan tree.  If a node or
// depth bound is reached, the visible directory remains in the result but is
// marked incomplete and the snapshot is marked truncated.  This makes a
// partial snapshot useful for UI review without allowing a caller to mistake
// it for a complete inventory.
Snapshot snapshotFromNode(const FsNode& root,
                          const SnapshotLimits& limits = SnapshotLimits{});

// Canonical compact JSON. Object keys and children are emitted in a stable
// order; parsing and serializing a valid document produces identical bytes.
// scan_generation is deliberately absent from this format.
std::string serializeSnapshot(const Snapshot& snapshot,
                              const SnapshotLimits& limits = SnapshotLimits{});

// Strict v1 parser. Unknown or duplicate keys, malformed JSON, schema
// mismatches, and bound violations are rejected with SnapshotError.
Snapshot parseSnapshot(std::string_view json,
                       const SnapshotLimits& limits = SnapshotLimits{});

// Reads one bounded regular file without following a symlink.  The parser is
// applied only after the complete file has been read, so malformed or
// oversized on-disk documents never become a partial Snapshot value.
Snapshot readSnapshotFile(const std::filesystem::path& path,
                          const SnapshotLimits& limits = SnapshotLimits{});

// Converts persisted evidence into a read-only ScanResult-shaped document for
// shared consumers such as duplicate analysis. Snapshot-level incomplete or
// truncated state is propagated to the root so it cannot be mistaken for a
// complete live inventory. No filesystem scan is performed.
ScanResult scanEvidenceFromSnapshot(Snapshot snapshot,
                                    std::uint64_t generation = 0);

// Serializes to a same-directory temporary file, fsyncs it, then atomically
// renames it into place.  Existing destinations must already be regular files
// and unchanged during the write; symlinks, directories, devices, FIFOs, and
// races that replace the reviewed destination are rejected.  The temporary
// file is private (0600) and is removed on every failure path.
void writeSnapshotAtomically(const Snapshot& snapshot,
                             const std::filesystem::path& path,
                             const SnapshotLimits& limits = SnapshotLimits{});

enum class SnapshotChangeKind { Added, Removed, Grown, Shrunk, Moved, Uncertain };

struct SnapshotChange {
    SnapshotChangeKind kind = SnapshotChangeKind::Uncertain;
    // False means the observation is a candidate only: an incomplete scan,
    // unknown metric, or ambiguous identity prevents a definitive claim.
    bool certain = false;
    bool has_before = false;
    bool has_after = false;
    NodeKey before_key;
    NodeKey after_key;
    MetricValue before_metric;
    MetricValue after_metric;
    std::string reason;
};

struct SnapshotDiffOptions {
    SizeMetric metric = SizeMetric::Logical;
};

struct SnapshotDiff {
    std::vector<SnapshotChange> changes;
    // `complete` is false whenever any change or absence conclusion is
    // uncertain. `uncertain` is retained as a readable summary for callers
    // that do not need to inspect every candidate.
    bool complete = true;
    bool uncertain = false;
    std::size_t compared_nodes = 0;
};

// Compares visible entries by path first, then uses a unique valid physical
// identity to detect moves. Missing/unknown evidence is never upgraded to a
// definite change; the candidate remains present with certain=false.
SnapshotDiff diffSnapshots(const Snapshot& before,
                           const Snapshot& after,
                           const SnapshotDiffOptions& options = SnapshotDiffOptions{});

} // namespace diskmap
