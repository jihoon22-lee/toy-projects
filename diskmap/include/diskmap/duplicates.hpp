#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "diskmap/scanner.hpp"
#include "diskmap/view.hpp"

namespace diskmap {

// Duplicate analysis is deliberately evidence based.  It never removes or
// modifies a file; callers can use the result to build a separately reviewed
// cleanup plan.
struct DuplicateAnalysisOptions {
    // Zero permits no retained candidates/groups and reports truncation when
    // the scan contains work.  All non-zero limits bound memory and metadata.
    std::size_t max_candidates = 10'000;
    std::size_t max_groups = 1'000;
    std::size_t max_group_members = 256;
    // The first and last portions are read before the full hash.  A zero
    // value is valid and makes the partial key size-only.
    std::size_t partial_bytes = 64 * 1024;
    // Every file read, hash update, and exact comparison is bounded by this
    // buffer size.  The analyzer streams larger files in multiple calls.
    std::size_t read_buffer_bytes = 64 * 1024;
    std::size_t max_errors = 256;
};

// A reader is injectable so retained evidence can be tested without making
// tests depend on races in a live filesystem.  readRange() may return fewer
// bytes than requested (including at EOF), but never more; the analyzer
// decides whether a short range is acceptable from the retained file size.
// Implementations must not follow symlinks.  The production implementation
// uses an OS no-follow open where available.
class DuplicateFileAccess {
public:
    virtual ~DuplicateFileAccess();

    virtual FsMetadata inspectNoFollow(const std::filesystem::path& path) const = 0;
    virtual bool readRange(const std::filesystem::path& path,
                           const FsMetadata& expected,
                           std::uint64_t offset,
                           std::size_t max_bytes,
                           std::string& bytes,
                           std::string& error) const = 0;
};

using DuplicateProgressFn =
    std::function<void(std::size_t files_processed, std::uint64_t bytes_read)>;

enum class DuplicateIssueKind {
    IncompleteScan,
    FilteredScan,
    StaleEvidence,
    CandidateLimit,
    GroupLimit,
    MemberLimit,
    MetadataUnknown,
    UnsupportedType,
    Missing,
    IdentityChanged,
    SizeChanged,
    TypeChanged,
    ModifiedTimeChanged,
    ReadError,
    ChangedDuringRead,
    HashMismatch,
    HardLinkAlias,
    Cancelled,
};

const char* duplicateIssueKindName(DuplicateIssueKind kind);

struct DuplicateIssue {
    std::filesystem::path path;
    NodeKey key;
    DuplicateIssueKind kind = DuplicateIssueKind::ReadError;
    std::string message;
};

struct DuplicateEntry {
    NodeKey key;
    std::filesystem::path path;
    std::uint64_t size = 0;
    FileIdentity identity;
    std::uint64_t hard_link_count = 0;
    bool hard_link_count_known = false;
    std::string partial_fingerprint;
    std::string content_hash;
    bool certain = false;
};

struct DuplicateGroup {
    std::uint64_t size = 0;
    std::string partial_fingerprint;
    std::string content_hash;
    std::vector<DuplicateEntry> entries;
    // Certain means the listed entries were stable and byte-equal.  It does
    // not imply that the scan was exhaustive; inspect complete/uncertain on
    // DuplicateAnalysis for that inventory-level distinction.
    bool certain = false;
    // Hard links are content aliases, not independently reclaimable copies.
    // Such a group remains visible as evidence but is never a cleanup
    // opportunity and has zero reclaimable_bytes.
    bool reclaimable = false;
    bool hard_link_alias = false;
    std::uint64_t reclaimable_bytes = 0;
    std::string reason;
};

struct DuplicateAnalysis {
    std::vector<DuplicateGroup> groups;
    std::vector<DuplicateIssue> issues;
    bool complete = true;
    bool uncertain = false;
    bool truncated = false;
    bool errors_truncated = false;
    bool cancelled = false;
    std::size_t candidates_seen = 0;
    std::size_t candidates_retained = 0;
    std::size_t files_hashed = 0;
    std::uint64_t bytes_read = 0;
};

// Computes a standard lowercase hexadecimal SHA-256 digest.  Exposing this
// tiny deterministic helper also makes known-vector tests independent of the
// filesystem reader.
std::string sha256Hex(std::string_view bytes);

DuplicateAnalysis analyzeDuplicates(
    const ScanResult& scan,
    const DuplicateAnalysisOptions& options = DuplicateAnalysisOptions{},
    const DuplicateFileAccess* access = nullptr,
    const DuplicateProgressFn& progress = nullptr,
    const ScanCancellationToken* cancellation = nullptr);

} // namespace diskmap
