#include "duplicates_internal.hpp"

#include "diskmap/fs_node.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <vector>

namespace diskmap {
namespace {

namespace fs = std::filesystem;

using detail::AnalysisContext;
using detail::Candidate;
using detail::PartialBucket;
using detail::PartialKey;
using detail::addIssue;

fs::path nodePath(const FsNode& node) {
    return (node.path.empty() ? fs::path(node.name) : node.path).lexically_normal();
}

std::string normalized(const fs::path& input) {
    const fs::path path = input.lexically_normal();
    std::string value = path.generic_string();
    const std::string root = path.root_path().generic_string();
    while (value.size() > root.size() && !value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

FsKind representedKind(const FsNode& node) {
    if (node.metadata.kind != FsKind::Other || node.metadata.complete) {
        return node.metadata.kind;
    }
    return node.is_dir ? FsKind::Directory : FsKind::Other;
}

NodeKey keyFor(const FsNode& node, const fs::path& path) {
    NodeKey key;
    key.normalized_path = normalized(path);
    key.kind = representedKind(node);
    key.followed = node.followed;
    if (node.metadata.identity.valid) {
        key.identity = node.metadata.identity;
    }
    return key;
}

bool nodeEvidenceUncertain(const FsNode& node) {
    return !node.complete || !node.error.empty() || !node.metadata.complete
           || !node.metadata.error.empty() || node.cycle_skipped
           || node.mount_boundary_skipped
           || (node.followed
               && (!node.has_target_metadata || !node.target_metadata.complete
                   || !node.target_metadata.error.empty()));
}

bool evidenceCertain(const FsNode& node, bool inheritedUncertainty) {
    return !inheritedUncertainty && !nodeEvidenceUncertain(node);
}

bool scanHasUncertainty(const ScanResult& scan) {
    return scan.cancelled || !scan.fatal_error.empty() || nodeEvidenceUncertain(scan.root)
           || scan.error_count != 0 || !scan.errors.empty() || scan.errors_truncated
           || scan.entries_filtered != 0 || scan.mount_boundaries_skipped != 0
           || scan.totals_filtered;
}

void addScanIssues(const ScanResult& scan, AnalysisContext& context) {
    const fs::path rootPath = nodePath(scan.root);
    const NodeKey rootKey = keyFor(scan.root, rootPath);
    if (scan.cancelled) {
        context.result.cancelled = true;
        addIssue(context, rootPath, rootKey, DuplicateIssueKind::Cancelled,
                 "retained scan was cancelled");
    }
    if (!scan.fatal_error.empty() || nodeEvidenceUncertain(scan.root)) {
        const std::string message = scan.fatal_error.empty()
                                        ? "retained scan is incomplete"
                                        : scan.fatal_error;
        addIssue(context, rootPath, rootKey, DuplicateIssueKind::IncompleteScan,
                 message);
    }
    if (scan.error_count != 0 || !scan.errors.empty() || scan.errors_truncated) {
        addIssue(context, rootPath, rootKey, DuplicateIssueKind::IncompleteScan,
                 "retained scan contains listing or metadata errors");
    }
    if (scan.entries_filtered != 0 || scan.totals_filtered) {
        addIssue(context, rootPath, rootKey, DuplicateIssueKind::FilteredScan,
                 "retained scan filtered entries; duplicate absence is not certain");
    }
    if (scan.mount_boundaries_skipped != 0) {
        addIssue(context, rootPath, rootKey, DuplicateIssueKind::IncompleteScan,
                 "retained scan skipped mount boundaries");
    }
}

struct WalkFrame {
    const FsNode* node = nullptr;
    bool inherited_uncertainty = false;
};

void pushChildren(const FsNode& node,
                  bool inheritedUncertainty,
                  std::vector<WalkFrame>& stack) {
    std::vector<const FsNode*> children;
    children.reserve(node.children.size());
    for (const FsNode& child : node.children) {
        children.push_back(&child);
    }
    std::sort(children.begin(), children.end(), [](const FsNode* left, const FsNode* right) {
        const std::string leftPath = normalized(nodePath(*left));
        const std::string rightPath = normalized(nodePath(*right));
        return leftPath < rightPath;
    });
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        const FsNode& child = **it;
        const bool uncertain = inheritedUncertainty || nodeEvidenceUncertain(child);
        stack.push_back(WalkFrame{*it, uncertain});
    }
}

void collectFileNode(const WalkFrame& frame,
                     const ScanResult& scan,
                     AnalysisContext& context,
                     bool globalUncertainty,
                     bool& limitReported,
                     std::vector<Candidate>& candidates);

bool retainableFile(const FsNode& node,
                    const ScanResult& scan,
                    const fs::path& path,
                    const NodeKey& key,
                    AnalysisContext& context) {
    if (node.scan_generation != scan.generation) {
        addIssue(context, path, key, DuplicateIssueKind::StaleEvidence,
                 "file evidence belongs to a different scan generation");
        return false;
    }
    if (representedKind(node) != FsKind::RegularFile || node.followed) {
        addIssue(context, path, key, DuplicateIssueKind::UnsupportedType,
                 "only the file entry itself is eligible; symlink targets are excluded");
        return false;
    }
    if (!node.complete || !node.error.empty() || !node.logical_size_known
        || !node.metadata.complete || !node.metadata.identity.valid
        || !node.metadata.modified_time_known) {
        addIssue(context, path, key, DuplicateIssueKind::MetadataUnknown,
                 "regular-file evidence lacks a complete stable identity, size, or mtime");
        return false;
    }
    if (node.size != node.metadata.logical_size) {
        addIssue(context, path, key, DuplicateIssueKind::SizeChanged,
                 "retained node size disagrees with its metadata");
        return false;
    }
    return true;
}

void collectNode(const WalkFrame& frame,
                 const ScanResult& scan,
                 AnalysisContext& context,
                 bool globalUncertainty,
                 bool& limitReported,
                 std::vector<Candidate>& candidates,
                 std::vector<WalkFrame>& stack) {
    const FsNode& node = *frame.node;
    const fs::path path = nodePath(node);
    const NodeKey key = keyFor(node, path);
    const FsKind kind = representedKind(node);
    if (kind == FsKind::RegularFile) {
        collectFileNode(frame, scan, context, globalUncertainty, limitReported, candidates);
    }
    // A followed symlink is still a symlink entry. Do not traverse its target
    // descendants, because they are not independent observations.
    if (node.is_dir && kind != FsKind::Symlink && !node.cycle_skipped
        && !node.mount_boundary_skipped) {
        pushChildren(node, frame.inherited_uncertainty || nodeEvidenceUncertain(node),
                     stack);
    }
}

void collectFileNode(const WalkFrame& frame,
                     const ScanResult& scan,
                     AnalysisContext& context,
                     bool globalUncertainty,
                     bool& limitReported,
                     std::vector<Candidate>& candidates) {
    const FsNode& node = *frame.node;
    const fs::path path = nodePath(node);
    const NodeKey key = keyFor(node, path);
    ++context.result.candidates_seen;
    if (candidates.size() >= context.options.max_candidates) {
        if (limitReported) {
            return;
        }
        addIssue(context, path, key, DuplicateIssueKind::CandidateLimit,
                 "candidate limit reached; duplicate absence is not certain");
        context.result.truncated = true;
        limitReported = true;
        return;
    }
    if (!retainableFile(node, scan, path, key, context)) {
        return;
    }
    candidates.push_back(Candidate{path,
                                   key,
                                   node.metadata,
                                   node.metadata.logical_size,
                                   evidenceCertain(node,
                                                   frame.inherited_uncertainty
                                                       || globalUncertainty),
                                   {},
                                   {}});
}

bool collectionCancelled(AnalysisContext& context) {
    if (!context.isCancelled()) {
        return false;
    }
    context.result.cancelled = true;
    addIssue(context, {}, {}, DuplicateIssueKind::Cancelled,
             "duplicate analysis cancelled while collecting candidates");
    return true;
}

void collectCandidates(const ScanResult& scan,
                       AnalysisContext& context,
                       std::vector<Candidate>& candidates,
                       bool globalUncertainty) {
    std::vector<WalkFrame> stack{{&scan.root, false}};
    bool limitReported = false;
    while (!stack.empty()) {
        if (collectionCancelled(context)) {
            break;
        }
        const WalkFrame frame = stack.back();
        stack.pop_back();
        collectNode(frame, scan, context, globalUncertainty, limitReported,
                    candidates, stack);
    }
    context.result.candidates_retained = candidates.size();
}

void sortIssues(DuplicateAnalysis& result) {
    std::sort(result.issues.begin(), result.issues.end(), [](const DuplicateIssue& left,
                                                             const DuplicateIssue& right) {
        const std::string leftPath = left.path.generic_string();
        const std::string rightPath = right.path.generic_string();
        if (leftPath != rightPath) {
            return leftPath < rightPath;
        }
        if (left.kind != right.kind) {
            return static_cast<int>(left.kind) < static_cast<int>(right.kind);
        }
        return left.message < right.message;
    });
}

DuplicateAnalysisOptions checkedOptions(const DuplicateAnalysisOptions& input) {
    if (input.read_buffer_bytes == 0) {
        throw std::invalid_argument("duplicate read_buffer_bytes must be positive");
    }
    if (input.max_group_members == 0) {
        throw std::invalid_argument("duplicate max_group_members must be positive");
    }
    constexpr std::size_t kHardReadBound = 64 * 1024 * 1024;
    if (input.read_buffer_bytes > kHardReadBound || input.partial_bytes > kHardReadBound) {
        throw std::invalid_argument("duplicate read buffers exceed the safety bound");
    }
    return input;
}

void sortAnalysis(DuplicateAnalysis& result) {
    sortIssues(result);
    std::sort(result.groups.begin(), result.groups.end(), [](const DuplicateGroup& left,
                                                             const DuplicateGroup& right) {
        if (left.size != right.size) {
            return left.size < right.size;
        }
        if (left.partial_fingerprint != right.partial_fingerprint) {
            return left.partial_fingerprint < right.partial_fingerprint;
        }
        return left.content_hash < right.content_hash;
    });
}

} // namespace

const char* duplicateIssueKindName(DuplicateIssueKind kind) {
    constexpr std::array<const char*, 18> names = {
        "incomplete-scan",
        "filtered-scan",
        "stale-evidence",
        "candidate-limit",
        "group-limit",
        "member-limit",
        "metadata-unknown",
        "unsupported-type",
        "missing",
        "identity-changed",
        "size-changed",
        "type-changed",
        "modified-time-changed",
        "read-error",
        "changed-during-read",
        "hash-mismatch",
        "hard-link-alias",
        "cancelled",
    };
    const auto index = static_cast<std::size_t>(kind);
    if (index < names.size()) {
        return names[index];
    }
    return "unknown";
}

DuplicateAnalysis analyzeDuplicates(const ScanResult& scan,
                                     const DuplicateAnalysisOptions& inputOptions,
                                     const DuplicateFileAccess* providedAccess,
                                     const DuplicateProgressFn& progress,
                                     const ScanCancellationToken* cancellation) {
    const DuplicateAnalysisOptions options = checkedOptions(inputOptions);
    std::unique_ptr<DuplicateFileAccess> ownedAccess;
    if (providedAccess == nullptr) {
        ownedAccess = detail::makeSystemDuplicateFileAccess();
    }
    const DuplicateFileAccess& access = providedAccess != nullptr ? *providedAccess
                                                                    : *ownedAccess;
    DuplicateAnalysis result;
    AnalysisContext context{options, access, progress, cancellation, result};
    addScanIssues(scan, context);
    if (context.isCancelled()) {
        result.cancelled = true;
        addIssue(context, {}, {}, DuplicateIssueKind::Cancelled,
                 "duplicate analysis cancelled before candidate collection");
        sortIssues(result);
        return result;
    }

    std::vector<Candidate> candidates;
    collectCandidates(scan, context, candidates, scanHasUncertainty(scan));
    result.candidates_retained = candidates.size();
    std::map<PartialKey, PartialBucket> partials;
    detail::fingerprintCandidates(candidates, context, partials);
    detail::processPartialGroups(partials, candidates, context);
    sortAnalysis(result);
    return result;
}

} // namespace diskmap
