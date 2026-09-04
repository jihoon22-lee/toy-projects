#include "duplicates_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace diskmap {
namespace detail {

namespace {

bool sameEvidence(const FsMetadata& expected,
                  const FsMetadata& actual,
                  DuplicateIssueKind& issue,
                  std::string& message) {
    if (!actual.complete || !actual.identity.valid || !actual.modified_time_known) {
        issue = DuplicateIssueKind::MetadataUnknown;
        message = "revalidation did not return complete identity, size, type, and mtime";
        return false;
    }
    if (actual.kind != FsKind::RegularFile) {
        issue = DuplicateIssueKind::TypeChanged;
        message = "candidate changed from a regular file";
        return false;
    }
    if (actual.identity != expected.identity) {
        issue = DuplicateIssueKind::IdentityChanged;
        message = "candidate identity changed during duplicate analysis";
        return false;
    }
    if (actual.logical_size != expected.logical_size) {
        issue = DuplicateIssueKind::SizeChanged;
        message = "candidate size changed during duplicate analysis";
        return false;
    }
    if (actual.modified_ns != expected.modified_ns) {
        issue = DuplicateIssueKind::ModifiedTimeChanged;
        message = "candidate modification time changed during duplicate analysis";
        return false;
    }
    return true;
}

void noteRead(AnalysisContext& context, std::size_t bytes) {
    context.result.bytes_read += static_cast<std::uint64_t>(bytes);
    if (context.progress) {
        context.progress(context.files_processed, context.result.bytes_read);
    }
}

bool readExact(const Candidate& candidate,
               std::uint64_t offset,
               std::size_t amount,
               AnalysisContext& context,
               std::string& output,
               std::string& error) {
    output.clear();
    output.reserve(amount);
    std::uint64_t position = offset;
    while (output.size() < amount) {
        if (context.isCancelled()) {
            context.result.cancelled = true;
            error = "duplicate analysis cancelled during file read";
            return false;
        }
        const std::size_t remaining = amount - output.size();
        const std::size_t request = std::min(remaining, context.options.read_buffer_bytes);
        std::string chunk;
        if (!context.access.readRange(candidate.path, candidate.evidence, position,
                                      request, chunk, error)) {
            return false;
        }
        if (chunk.empty() || chunk.size() > request) {
            error = "duplicate reader returned a short or oversized range";
            return false;
        }
        output += chunk;
        position += static_cast<std::uint64_t>(chunk.size());
        noteRead(context, chunk.size());
    }
    return true;
}

enum class ByteComparisonResult { Equal, Mismatch, ReadError, Cancelled };

ByteComparisonResult bytesEqual(const Candidate& left,
                                const Candidate& right,
                                AnalysisContext& context,
                                std::string& error) {
    std::uint64_t offset = 0;
    while (offset < left.size) {
        if (context.isCancelled()) {
            context.result.cancelled = true;
            error = "duplicate analysis cancelled during byte comparison";
            return ByteComparisonResult::Cancelled;
        }
        const std::size_t request = static_cast<std::size_t>(std::min<std::uint64_t>(
            left.size - offset, context.options.read_buffer_bytes));
        std::string leftBytes;
        std::string rightBytes;
        if (!readExact(left, offset, request, context, leftBytes, error)) {
            return context.result.cancelled ? ByteComparisonResult::Cancelled
                                             : ByteComparisonResult::ReadError;
        }
        if (!readExact(right, offset, request, context, rightBytes, error)) {
            return context.result.cancelled ? ByteComparisonResult::Cancelled
                                             : ByteComparisonResult::ReadError;
        }
        if (leftBytes != rightBytes) {
            error = "same SHA-256 candidates were not byte-equal";
            return ByteComparisonResult::Mismatch;
        }
        offset += static_cast<std::uint64_t>(leftBytes.size());
    }
    return ByteComparisonResult::Equal;
}

std::uint64_t reclaimableBytes(std::uint64_t size, std::size_t duplicates) {
    const std::uint64_t count = static_cast<std::uint64_t>(duplicates - 1);
    if (count != 0 && size > std::numeric_limits<std::uint64_t>::max() / count) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return size * count;
}

bool prepareGroup(const std::vector<std::size_t>& indices,
                  const PartialKey& key,
                  const std::vector<Candidate>& candidates,
                  DuplicateGroup& group) {
    if (indices.size() < 2) {
        return false;
    }
    group.size = key.size;
    group.partial_fingerprint = key.fingerprint;
    group.content_hash = candidates[indices.front()].hash;
    group.entries.reserve(indices.size());
    for (const std::size_t index : indices) {
        const Candidate& candidate = candidates[index];
        group.entries.push_back(DuplicateEntry{candidate.key,
                                               candidate.path,
                                               candidate.size,
                                               candidate.evidence.identity,
                                               candidate.evidence.hard_link_count,
                                               candidate.evidence.hard_link_count_known,
                                               candidate.partial,
                                               candidate.hash,
                                               candidate.scan_certain});
    }
    std::sort(group.entries.begin(), group.entries.end(), [](const DuplicateEntry& left,
                                                              const DuplicateEntry& right) {
        return left.key.normalized_path < right.key.normalized_path;
    });
    group.certain = std::all_of(group.entries.begin(), group.entries.end(),
                                [](const DuplicateEntry& entry) { return entry.certain; });
    return true;
}

void classifyReclamation(DuplicateGroup& group,
                         AnalysisContext& context,
                         bool membersTruncated) {
    std::set<std::pair<std::uint64_t, std::uint64_t>> identities;
    bool hardLinkEvidenceComplete = true;
    bool alias = false;
    for (const DuplicateEntry& entry : group.entries) {
        if (!entry.hard_link_count_known || entry.hard_link_count != 1) {
            hardLinkEvidenceComplete = false;
        }
        if (entry.identity.valid
            && !identities.insert({entry.identity.device, entry.identity.file}).second) {
            alias = true;
        }
    }
    group.hard_link_alias = alias || !hardLinkEvidenceComplete;
    if (alias) {
        group.reason = "hard-link aliases are not reclaimable copies";
        addIssue(context, group.entries.front().path, group.entries.front().key,
                 DuplicateIssueKind::HardLinkAlias, group.reason, false);
        return;
    }
    if (!hardLinkEvidenceComplete) {
        group.reason = "hard-link count evidence is unavailable; reclamation is uncertain";
        addIssue(context, group.entries.front().path, group.entries.front().key,
                 DuplicateIssueKind::HardLinkAlias, group.reason, false);
        return;
    }
    if (membersTruncated) {
        group.reason = "group member limit reached; reclamation is uncertain";
        return;
    }
    if (!group.certain || !context.result.complete) {
        group.reason = "scan or read evidence is incomplete";
        return;
    }
    if (group.size == 0) {
        group.reason = "empty files have no reclaimable bytes";
        return;
    }
    group.reclaimable = true;
    group.reclaimable_bytes = reclaimableBytes(group.size, group.entries.size());
}

bool hashOneCandidate(std::size_t index,
                      std::vector<Candidate>& candidates,
                      AnalysisContext& context,
                      std::map<std::string, std::vector<std::size_t>>& hashes) {
    if (context.isCancelled()) {
        context.result.cancelled = true;
        addIssue(context, {}, {}, DuplicateIssueKind::Cancelled,
                 "duplicate analysis cancelled while hashing candidates");
        return false;
    }
    if (!hashFull(candidates[index], context)) {
        return true;
    }
    hashes[candidates[index].hash].push_back(index);
    return true;
}

std::map<std::string, std::vector<std::size_t>> hashCandidates(
    PartialBucket& bucket,
    std::vector<Candidate>& candidates,
    AnalysisContext& context) {
    std::map<std::string, std::vector<std::size_t>> hashes;
    for (const std::size_t index : bucket.members) {
        if (!hashOneCandidate(index, candidates, context, hashes)) {
            break;
        }
    }
    return hashes;
}

bool verifyHashCandidates(const std::vector<std::size_t>& indices,
                          const std::vector<Candidate>& candidates,
                          AnalysisContext& context) {
    std::string error;
    ByteComparisonResult comparison = ByteComparisonResult::Equal;
    const Candidate& representative = candidates[indices.front()];
    for (std::size_t cursor = 1; cursor < indices.size(); ++cursor) {
        comparison = bytesEqual(representative, candidates[indices[cursor]], context, error);
        if (comparison != ByteComparisonResult::Equal) {
            break;
        }
    }
    if (comparison == ByteComparisonResult::Mismatch) {
        addIssue(context, representative.path, representative.key,
                 DuplicateIssueKind::HashMismatch, error);
    } else if (comparison == ByteComparisonResult::ReadError) {
        addIssue(context, representative.path, representative.key,
                 DuplicateIssueKind::ReadError, error);
    } else if (comparison == ByteComparisonResult::Cancelled) {
        addIssue(context, representative.path, representative.key,
                 DuplicateIssueKind::Cancelled, error);
    }
    if (comparison == ByteComparisonResult::Cancelled) {
        return false;
    }

    bool stable = comparison == ByteComparisonResult::Equal;
    for (const std::size_t index : indices) {
        DuplicateIssueKind issue = DuplicateIssueKind::ReadError;
        std::string message;
        if (!inspectStable(candidates[index], context, issue, message)) {
            stable = false;
        }
    }
    return stable;
}

bool appendGroup(const PartialKey& key,
                 PartialBucket& bucket,
                 const std::vector<std::size_t>& indices,
                 std::vector<Candidate>& candidates,
                 AnalysisContext& context) {
    DuplicateGroup group;
    if (!prepareGroup(indices, key, candidates, group)) {
        return false;
    }
    if (context.result.groups.size() >= context.options.max_groups) {
        context.result.truncated = true;
        addIssue(context, {}, {}, DuplicateIssueKind::GroupLimit,
                 "group limit reached; duplicate absence is not certain");
        return true;
    }
    classifyReclamation(group, context, bucket.truncated);
    context.result.groups.push_back(std::move(group));
    return false;
}

void reportMemberLimit(AnalysisContext& context) {
    context.result.truncated = true;
    addIssue(context, {}, {}, DuplicateIssueKind::MemberLimit,
             "group member limit reached; duplicate absence is not certain");
}

bool processPartialBucket(const PartialKey& key,
                          PartialBucket& bucket,
                          std::vector<Candidate>& candidates,
                          AnalysisContext& context) {
    if (bucket.truncated) {
        reportMemberLimit(context);
    }
    const std::map<std::string, std::vector<std::size_t>> hashes =
        hashCandidates(bucket, candidates, context);
    for (const auto& hash : hashes) {
        if (hash.second.size() < 2) {
            continue;
        }
        if (!verifyHashCandidates(hash.second, candidates, context)) {
            continue;
        }
        if (appendGroup(key, bucket, hash.second, candidates, context)) {
            return true;
        }
    }
    return false;
}

bool fingerprintOneCandidate(std::size_t index,
                             std::vector<Candidate>& candidates,
                             AnalysisContext& context,
                             std::map<PartialKey, PartialBucket>& partials) {
    if (context.isCancelled()) {
        context.result.cancelled = true;
        addIssue(context, {}, {}, DuplicateIssueKind::Cancelled,
                 "duplicate analysis cancelled while fingerprinting candidates");
        return false;
    }
    if (!readPartial(candidates[index], context)) {
        return true;
    }
    PartialBucket& bucket = partials[PartialKey{candidates[index].size,
                                                 candidates[index].partial}];
    if (bucket.members.size() < context.options.max_group_members) {
        bucket.members.push_back(index);
    } else {
        bucket.truncated = true;
    }
    return true;
}

void fingerprintCandidatesImpl(std::vector<Candidate>& candidates,
                               AnalysisContext& context,
                               std::map<PartialKey, PartialBucket>& partials) {
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        if (!fingerprintOneCandidate(index, candidates, context, partials)) {
            break;
        }
    }
}

bool skipPartialBucket(const PartialBucket& bucket, AnalysisContext& context) {
    if (bucket.members.size() >= 2) {
        return false;
    }
    if (bucket.truncated) {
        reportMemberLimit(context);
    }
    return true;
}

void processPartialGroupsImpl(std::map<PartialKey, PartialBucket>& partials,
                              std::vector<Candidate>& candidates,
                              AnalysisContext& context) {
    for (auto& partial : partials) {
        if (skipPartialBucket(partial.second, context)) {
            continue;
        }
        if (processPartialBucket(partial.first, partial.second, candidates, context)
            || context.result.cancelled) {
            break;
        }
    }
}

} // namespace

void fingerprintCandidates(std::vector<Candidate>& candidates,
                           AnalysisContext& context,
                           std::map<PartialKey, PartialBucket>& partials) {
    fingerprintCandidatesImpl(candidates, context, partials);
}

void processPartialGroups(std::map<PartialKey, PartialBucket>& partials,
                          std::vector<Candidate>& candidates,
                          AnalysisContext& context) {
    processPartialGroupsImpl(partials, candidates, context);
}

void addIssue(AnalysisContext& context,
              const std::filesystem::path& path,
              const NodeKey& key,
              DuplicateIssueKind kind,
              std::string message,
              bool affectsCompleteness) {
    context.result.uncertain = true;
    if (affectsCompleteness) {
        context.result.complete = false;
    }
    if (context.options.max_errors == 0
        || context.result.issues.size() >= context.options.max_errors) {
        context.result.errors_truncated = true;
        return;
    }
    context.result.issues.push_back(DuplicateIssue{path, key, kind, std::move(message)});
}

bool inspectStable(const Candidate& candidate,
                   AnalysisContext& context,
                   DuplicateIssueKind& issue,
                   std::string& message) {
    const FsMetadata actual = context.access.inspectNoFollow(candidate.path);
    if (sameEvidence(candidate.evidence, actual, issue, message)) {
        return true;
    }
    addIssue(context, candidate.path, candidate.key, issue, message);
    return false;
}

bool readPartial(Candidate& candidate, AnalysisContext& context) {
    DuplicateIssueKind issue = DuplicateIssueKind::ReadError;
    std::string message;
    if (!inspectStable(candidate, context, issue, message)) {
        return false;
    }
    const std::size_t portion = std::min<std::uint64_t>(
        candidate.size, static_cast<std::uint64_t>(context.options.partial_bytes));
    std::string first;
    std::string last;
    std::string error;
    if (!readExact(candidate, 0, portion, context, first, error)) {
        addIssue(context, candidate.path, candidate.key, DuplicateIssueKind::ReadError,
                 error);
        return false;
    }
    if (candidate.size > portion
        && !readExact(candidate, candidate.size - portion, portion, context, last, error)) {
        addIssue(context, candidate.path, candidate.key, DuplicateIssueKind::ReadError,
                 error);
        return false;
    }
    if (!inspectStable(candidate, context, issue, message)) {
        return false;
    }
    candidate.partial = partialFingerprint(candidate.size, first, last);
    return true;
}

bool hashFull(Candidate& candidate, AnalysisContext& context) {
    DuplicateIssueKind issue = DuplicateIssueKind::ReadError;
    std::string message;
    if (!inspectStable(candidate, context, issue, message)) {
        return false;
    }
    Sha256 digest;
    std::uint64_t offset = 0;
    while (offset < candidate.size) {
        if (context.isCancelled()) {
            context.result.cancelled = true;
            addIssue(context, candidate.path, candidate.key, DuplicateIssueKind::Cancelled,
                     "duplicate analysis cancelled during full hash");
            return false;
        }
        const std::uint64_t remaining = candidate.size - offset;
        const std::size_t request = static_cast<std::size_t>(std::min<std::uint64_t>(
            remaining, context.options.read_buffer_bytes));
        std::string chunk;
        std::string error;
        if (!context.access.readRange(candidate.path, candidate.evidence, offset,
                                      request, chunk, error)) {
            addIssue(context, candidate.path, candidate.key, DuplicateIssueKind::ReadError,
                     error);
            return false;
        }
        if (chunk.empty() || chunk.size() > request) {
            addIssue(context, candidate.path, candidate.key, DuplicateIssueKind::ReadError,
                     "duplicate reader returned an invalid full-hash range");
            return false;
        }
        digest.update(chunk.data(), chunk.size());
        offset += static_cast<std::uint64_t>(chunk.size());
        noteRead(context, chunk.size());
    }
    candidate.hash = digest.finalHex();
    ++context.result.files_hashed;
    ++context.files_processed;
    if (!inspectStable(candidate, context, issue, message)) {
        return false;
    }
    return true;
}

} // namespace detail
} // namespace diskmap
