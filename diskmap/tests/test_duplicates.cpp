#include "assert.hpp"

#include "diskmap/duplicates.hpp"
#include "diskmap/fs_source.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using diskmap::DuplicateAnalysis;
using diskmap::DuplicateFileAccess;
using diskmap::DuplicateIssueKind;
using diskmap::FsKind;
using diskmap::FsMetadata;
using diskmap::FsNode;
using diskmap::ScanResult;

struct Record {
    std::string bytes;
    FsMetadata metadata;
};

class FakeAccess final : public DuplicateFileAccess {
public:
    void add(const fs::path& path,
             std::string bytes,
             diskmap::FileIdentity identity,
             std::int64_t modified,
             std::uint64_t links = 1) {
        FsMetadata metadata;
        metadata.kind = FsKind::RegularFile;
        metadata.identity = identity;
        metadata.logical_size = bytes.size();
        metadata.hard_link_count = links;
        metadata.hard_link_count_known = true;
        metadata.modified_ns = modified;
        metadata.modified_time_known = true;
        metadata.complete = true;
        records_[path] = Record{std::move(bytes), metadata};
    }

    void setChunk(std::size_t bytes) { max_chunk_ = bytes; }
    void mutateAfterRead(const fs::path& path, std::size_t call) {
        mutate_path_ = path;
        mutate_after_call_ = call;
    }
    void mutateBytesAfterRead(const fs::path& path,
                              std::size_t call,
                              std::string replacement) {
        mutate_bytes_path_ = path;
        mutate_bytes_after_call_ = call;
        mutate_bytes_ = std::move(replacement);
    }
    void failReadAt(const fs::path& path, std::size_t call, std::string message) {
        fail_read_path_ = path;
        fail_read_at_call_ = call;
        fail_read_message_ = std::move(message);
    }
    void returnEmptyAt(const fs::path& path, std::size_t call) {
        invalid_read_path_ = path;
        invalid_read_at_call_ = call;
        invalid_read_oversized_ = false;
    }
    void returnOversizedAt(const fs::path& path, std::size_t call) {
        invalid_read_path_ = path;
        invalid_read_at_call_ = call;
        invalid_read_oversized_ = true;
    }
    void mutateMetadataAfterRead(const fs::path& path,
                                 std::size_t call,
                                 std::function<void(FsMetadata&)> mutation) {
        mutate_metadata_path_ = path;
        mutate_metadata_after_call_ = call;
        mutate_metadata_ = std::move(mutation);
    }

    FsMetadata inspectNoFollow(const fs::path& path) const override {
        const auto it = records_.find(path);
        return it == records_.end() ? FsMetadata{} : it->second.metadata;
    }

    bool readRange(const fs::path& path,
                   const FsMetadata& expected,
                   std::uint64_t offset,
                   std::size_t max_bytes,
                   std::string& bytes,
                   std::string& error) const override {
        (void)expected;
        ++read_calls_;
        max_request_ = std::max(max_request_, max_bytes);
        const auto it = records_.find(path);
        if (it == records_.end()) {
            error = "fake file is missing";
            return false;
        }
        if (mutate_after_call_ != 0 && read_calls_ == mutate_after_call_
            && path == mutate_path_) {
            it->second.metadata.modified_ns += 1;
        }
        if (mutate_bytes_after_call_ != 0 && read_calls_ == mutate_bytes_after_call_
            && path == mutate_bytes_path_) {
            it->second.bytes = mutate_bytes_;
        }
        if (mutate_metadata_after_call_ != 0 && read_calls_ == mutate_metadata_after_call_
            && path == mutate_metadata_path_ && mutate_metadata_) {
            mutate_metadata_(it->second.metadata);
        }
        if (fail_read_at_call_ != 0 && read_calls_ == fail_read_at_call_
            && path == fail_read_path_) {
            error = fail_read_message_;
            return false;
        }
        if (invalid_read_at_call_ != 0 && read_calls_ == invalid_read_at_call_
            && path == invalid_read_path_) {
            if (invalid_read_oversized_) {
                bytes.assign(max_bytes + 1, 'x');
            } else {
                bytes.clear();
            }
            return true;
        }
        if (offset > it->second.bytes.size()) {
            bytes.clear();
            return true;
        }
        const std::size_t begin = static_cast<std::size_t>(offset);
        const std::size_t available = it->second.bytes.size() - begin;
        const std::size_t limit = max_chunk_ == 0 ? max_bytes : std::min(max_bytes, max_chunk_);
        bytes = it->second.bytes.substr(begin, std::min(available, limit));
        return true;
    }

    std::size_t maxObservedRequest() const { return max_request_; }
    std::size_t readCalls() const { return read_calls_; }

private:
    mutable std::map<fs::path, Record> records_;
    mutable std::size_t read_calls_ = 0;
    mutable std::size_t max_request_ = 0;
    mutable fs::path mutate_path_;
    mutable std::size_t mutate_after_call_ = 0;
    mutable fs::path mutate_bytes_path_;
    mutable std::size_t mutate_bytes_after_call_ = 0;
    mutable std::string mutate_bytes_;
    mutable fs::path fail_read_path_;
    mutable std::size_t fail_read_at_call_ = 0;
    mutable std::string fail_read_message_;
    mutable fs::path invalid_read_path_;
    mutable std::size_t invalid_read_at_call_ = 0;
    mutable bool invalid_read_oversized_ = false;
    mutable fs::path mutate_metadata_path_;
    mutable std::size_t mutate_metadata_after_call_ = 0;
    mutable std::function<void(FsMetadata&)> mutate_metadata_;
    std::size_t max_chunk_ = 0;
};

FsMetadata directoryMetadata() {
    FsMetadata metadata;
    metadata.kind = FsKind::Directory;
    metadata.complete = true;
    return metadata;
}

FsNode fileNode(const fs::path& path,
                std::string bytes,
                diskmap::FileIdentity identity,
                std::int64_t modified,
                std::uint64_t links = 1,
                std::uint64_t generation = 7) {
    FsNode node;
    node.name = path.filename().string();
    node.path = path;
    node.size = bytes.size();
    node.scan_generation = generation;
    node.metadata.kind = FsKind::RegularFile;
    node.metadata.identity = identity;
    node.metadata.logical_size = bytes.size();
    node.metadata.modified_ns = modified;
    node.metadata.modified_time_known = true;
    node.metadata.hard_link_count = links;
    node.metadata.hard_link_count_known = true;
    node.metadata.complete = true;
    return node;
}

ScanResult scanWith(std::vector<FsNode> children,
                    std::uint64_t generation = 7,
                    bool complete = true) {
    ScanResult scan;
    scan.generation = generation;
    scan.root.name = "root";
    scan.root.path = fs::path("/virtual/root");
    scan.root.is_dir = true;
    scan.root.metadata = directoryMetadata();
    scan.root.complete = complete;
    scan.root.children = std::move(children);
    return scan;
}

bool hasIssue(const DuplicateAnalysis& analysis, DuplicateIssueKind kind) {
    return std::any_of(analysis.issues.begin(), analysis.issues.end(),
                       [kind](const diskmap::DuplicateIssue& issue) {
                           return issue.kind == kind;
                       });
}

void testIssueNamesAndOptionValidation() {
    const std::array<DuplicateIssueKind, 18> kinds = {
        DuplicateIssueKind::IncompleteScan,
        DuplicateIssueKind::FilteredScan,
        DuplicateIssueKind::StaleEvidence,
        DuplicateIssueKind::CandidateLimit,
        DuplicateIssueKind::GroupLimit,
        DuplicateIssueKind::MemberLimit,
        DuplicateIssueKind::MetadataUnknown,
        DuplicateIssueKind::UnsupportedType,
        DuplicateIssueKind::Missing,
        DuplicateIssueKind::IdentityChanged,
        DuplicateIssueKind::SizeChanged,
        DuplicateIssueKind::TypeChanged,
        DuplicateIssueKind::ModifiedTimeChanged,
        DuplicateIssueKind::ReadError,
        DuplicateIssueKind::ChangedDuringRead,
        DuplicateIssueKind::HashMismatch,
        DuplicateIssueKind::HardLinkAlias,
        DuplicateIssueKind::Cancelled,
    };
    const std::array<const char*, 18> names = {
        "incomplete-scan", "filtered-scan", "stale-evidence", "candidate-limit",
        "group-limit", "member-limit", "metadata-unknown", "unsupported-type", "missing",
        "identity-changed", "size-changed", "type-changed", "modified-time-changed",
        "read-error", "changed-during-read", "hash-mismatch", "hard-link-alias", "cancelled",
    };
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        CHECK_EQ(std::string(diskmap::duplicateIssueKindName(kinds[index])),
                 std::string(names[index]));
    }
    CHECK_EQ(std::string(diskmap::duplicateIssueKindName(
                 static_cast<DuplicateIssueKind>(999))),
             "unknown");

    const auto rejects = [](diskmap::DuplicateAnalysisOptions options, const char* message) {
        bool threw = false;
        try {
            (void)diskmap::analyzeDuplicates(scanWith({}), options);
        } catch (const std::invalid_argument& error) {
            threw = true;
            CHECK_EQ(std::string(error.what()), std::string(message));
        }
        CHECK(threw);
    };
    diskmap::DuplicateAnalysisOptions zeroBuffer;
    zeroBuffer.read_buffer_bytes = 0;
    rejects(zeroBuffer, "duplicate read_buffer_bytes must be positive");

    diskmap::DuplicateAnalysisOptions zeroMembers;
    zeroMembers.max_group_members = 0;
    rejects(zeroMembers, "duplicate max_group_members must be positive");

    constexpr std::size_t hardReadBound = 64 * 1024 * 1024;
    diskmap::DuplicateAnalysisOptions largeBuffer;
    largeBuffer.read_buffer_bytes = hardReadBound + 1;
    rejects(largeBuffer, "duplicate read buffers exceed the safety bound");

    diskmap::DuplicateAnalysisOptions largePartial;
    largePartial.partial_bytes = hardReadBound + 1;
    rejects(largePartial, "duplicate read buffers exceed the safety bound");
}

void testKnownHashes() {
    CHECK_EQ(diskmap::sha256Hex(""),
             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK_EQ(diskmap::sha256Hex("abc"),
             "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK_EQ(diskmap::sha256Hex("The quick brown fox jumps over the lazy dog"),
             "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

void testFakeDuplicateAndStreaming() {
    const fs::path first = "/virtual/root/a.bin";
    const fs::path second = "/virtual/root/b.bin";
    const fs::path other = "/virtual/root/c.bin";
    const std::string bytes = "same payload, streamed in small chunks";
    FakeAccess access;
    access.add(first, bytes, {1, 11, true}, 10);
    access.add(second, bytes, {1, 12, true}, 10);
    access.add(other, "same payload, streamed in small chink", {1, 13, true}, 10);
    access.setChunk(2);
    std::vector<FsNode> nodes;
    nodes.push_back(fileNode(first, bytes, {1, 11, true}, 10));
    nodes.push_back(fileNode(second, bytes, {1, 12, true}, 10));
    nodes.push_back(fileNode(other, "same payload, streamed in small chink", {1, 13, true}, 10));
    auto scan = scanWith(std::move(nodes));
    diskmap::DuplicateAnalysisOptions options;
    options.partial_bytes = 3;
    options.read_buffer_bytes = 4;
    const DuplicateAnalysis analysis =
        diskmap::analyzeDuplicates(scan, options, &access);
    CHECK_EQ(analysis.groups.size(), std::size_t(1));
    CHECK_EQ(analysis.groups.front().entries.size(), std::size_t(2));
    CHECK(analysis.groups.front().certain);
    CHECK(analysis.groups.front().reclaimable);
    CHECK_EQ(analysis.groups.front().reclaimable_bytes, bytes.size());
    CHECK_EQ(analysis.groups.front().content_hash, diskmap::sha256Hex(bytes));
    CHECK(access.readCalls() > 6);
}

void testHardLinksAreNotReclaimable() {
    const fs::path first = "/virtual/root/a";
    const fs::path second = "/virtual/root/b";
    const std::string bytes = "hard-link content";
    FakeAccess access;
    access.add(first, bytes, {2, 41, true}, 12);
    access.add(second, bytes, {2, 41, true}, 12);
    const DuplicateAnalysis analysis = diskmap::analyzeDuplicates(
        scanWith({fileNode(first, bytes, {2, 41, true}, 12, 2),
                  fileNode(second, bytes, {2, 41, true}, 12, 2)}),
        {}, &access);
    CHECK_EQ(analysis.groups.size(), std::size_t(1));
    CHECK(analysis.groups.front().hard_link_alias);
    CHECK(!analysis.groups.front().reclaimable);
    CHECK_EQ(analysis.groups.front().reclaimable_bytes, std::uint64_t(0));
    CHECK(hasIssue(analysis, DuplicateIssueKind::HardLinkAlias));
}

void testUncertainEvidence() {
    const fs::path first = "/virtual/root/a";
    const fs::path second = "/virtual/root/b";
    const std::string bytes = "uncertain evidence";
    FakeAccess access;
    access.add(first, bytes, {3, 1, true}, 20);
    access.add(second, bytes, {3, 2, true}, 20);
    std::vector<FsNode> nodes{fileNode(first, bytes, {3, 1, true}, 20),
                              fileNode(second, bytes, {3, 2, true}, 20)};
    ScanResult scan = scanWith(std::move(nodes));
    scan.entries_filtered = 1;
    const DuplicateAnalysis filtered = diskmap::analyzeDuplicates(scan, {}, &access);
    CHECK(!filtered.complete);
    CHECK(filtered.uncertain);
    CHECK(hasIssue(filtered, DuplicateIssueKind::FilteredScan));
    CHECK_EQ(filtered.groups.size(), std::size_t(1));
    CHECK(!filtered.groups.front().certain);
    CHECK(!filtered.groups.front().reclaimable);

    ScanResult incompleteRoot = scanWith(
        {fileNode(first, bytes, {3, 1, true}, 20),
         fileNode(second, bytes, {3, 2, true}, 20)});
    incompleteRoot.root.metadata.complete = false;
    const DuplicateAnalysis incompleteRootResult =
        diskmap::analyzeDuplicates(incompleteRoot, {}, &access);
    CHECK(!incompleteRootResult.complete);
    CHECK(hasIssue(incompleteRootResult, DuplicateIssueKind::IncompleteScan));
    CHECK_EQ(incompleteRootResult.groups.size(), std::size_t(1));
    CHECK(!incompleteRootResult.groups.front().certain);
    CHECK(!incompleteRootResult.groups.front().reclaimable);

    ScanResult stale = scanWith(
        {fileNode(first, bytes, {3, 1, true}, 20, 1, 8),
         fileNode(second, bytes, {3, 2, true}, 20, 1, 8)});
    const DuplicateAnalysis staleResult = diskmap::analyzeDuplicates(stale, {}, &access);
    CHECK(staleResult.groups.empty());
    CHECK(hasIssue(staleResult, DuplicateIssueKind::StaleEvidence));
}

void testRevalidationMetadataVariants() {
    const fs::path first = "/virtual/root/revalidate-a";
    const fs::path second = "/virtual/root/revalidate-b";
    const std::string bytes = "revalidation payload";

    const auto expectChange = [&](const std::function<void(FsMetadata&)>& mutation,
                                   DuplicateIssueKind kind) {
        FakeAccess access;
        access.add(first, bytes, {31, 1, true}, 301);
        access.add(second, bytes, {31, 2, true}, 301);
        // The first read belongs to the first candidate's partial fingerprint.
        // Mutating after that read makes the post-read stability check identify
        // the exact kind of evidence that changed.
        access.mutateMetadataAfterRead(first, 1, mutation);
        const DuplicateAnalysis analysis = diskmap::analyzeDuplicates(
            scanWith({fileNode(first, bytes, {31, 1, true}, 301),
                      fileNode(second, bytes, {31, 2, true}, 301)}),
            {}, &access);
        CHECK(analysis.groups.empty());
        CHECK(hasIssue(analysis, kind));
        CHECK(!analysis.complete);
    };

    expectChange([](FsMetadata& metadata) { metadata.complete = false; },
                 DuplicateIssueKind::MetadataUnknown);
    expectChange([](FsMetadata& metadata) { metadata.kind = FsKind::Directory; },
                 DuplicateIssueKind::TypeChanged);
    expectChange([](FsMetadata& metadata) { metadata.identity = {31, 99, true}; },
                 DuplicateIssueKind::IdentityChanged);
    expectChange([](FsMetadata& metadata) { metadata.logical_size += 1; },
                 DuplicateIssueKind::SizeChanged);
    expectChange([](FsMetadata& metadata) { metadata.modified_ns += 1; },
                 DuplicateIssueKind::ModifiedTimeChanged);
}

void testRevalidationAndCancellation() {
    const fs::path first = "/virtual/root/a";
    const fs::path second = "/virtual/root/b";
    const std::string bytes = "changed while being inspected";
    FakeAccess access;
    access.add(first, bytes, {4, 1, true}, 30);
    access.add(second, bytes, {4, 2, true}, 30);
    access.mutateAfterRead(first, 1);
    const DuplicateAnalysis changed = diskmap::analyzeDuplicates(
        scanWith({fileNode(first, bytes, {4, 1, true}, 30),
                  fileNode(second, bytes, {4, 2, true}, 30)}),
        {}, &access);
    CHECK(changed.groups.empty());
    CHECK(hasIssue(changed, DuplicateIssueKind::ModifiedTimeChanged));

    FakeAccess cancellable;
    cancellable.add(first, bytes, {4, 3, true}, 30);
    cancellable.add(second, bytes, {4, 4, true}, 30);
    diskmap::ScanCancellationToken token;
    std::size_t callbacks = 0;
    const DuplicateAnalysis cancelled = diskmap::analyzeDuplicates(
        scanWith({fileNode(first, bytes, {4, 3, true}, 30),
                  fileNode(second, bytes, {4, 4, true}, 30)}),
        {}, &cancellable,
        [&token, &callbacks](std::size_t, std::uint64_t) {
            ++callbacks;
            token.cancel();
        },
        &token);
    CHECK(cancelled.cancelled);
    CHECK(!cancelled.complete);
    CHECK(hasIssue(cancelled, DuplicateIssueKind::Cancelled));
    CHECK(callbacks != 0);
}

void testCancellationPhases() {
    const fs::path first = "/virtual/root/cancel-a";
    const fs::path second = "/virtual/root/cancel-b";
    const std::string bytes = "cancellation payload";

    diskmap::ScanCancellationToken beforeCollectionToken;
    beforeCollectionToken.cancel();
    FakeAccess beforeCollectionAccess;
    const DuplicateAnalysis beforeCollection = diskmap::analyzeDuplicates(
        scanWith({fileNode(first, bytes, {32, 1, true}, 302),
                  fileNode(second, bytes, {32, 2, true}, 302)}),
        {}, &beforeCollectionAccess, {}, &beforeCollectionToken);
    CHECK(beforeCollection.cancelled);
    CHECK_EQ(beforeCollection.candidates_seen, std::size_t(0));
    CHECK(hasIssue(beforeCollection, DuplicateIssueKind::Cancelled));

    FakeAccess partialAccess;
    partialAccess.add(first, bytes, {32, 3, true}, 302);
    partialAccess.add(second, bytes, {32, 4, true}, 302);
    partialAccess.setChunk(2);
    diskmap::DuplicateAnalysisOptions partialOptions;
    partialOptions.partial_bytes = bytes.size();
    partialOptions.read_buffer_bytes = 2;
    diskmap::ScanCancellationToken partialToken;
    std::size_t partialCallbacks = 0;
    const DuplicateAnalysis partial = diskmap::analyzeDuplicates(
        scanWith({fileNode(first, bytes, {32, 3, true}, 302),
                  fileNode(second, bytes, {32, 4, true}, 302)}),
        partialOptions, &partialAccess,
        [&partialToken, &partialCallbacks](std::size_t, std::uint64_t) {
            ++partialCallbacks;
            if (partialCallbacks == 1) {
                partialToken.cancel();
            }
        },
        &partialToken);
    CHECK(partial.cancelled);
    CHECK(hasIssue(partial, DuplicateIssueKind::Cancelled));
    CHECK(partialCallbacks != 0);

    FakeAccess hashAccess;
    hashAccess.add(first, bytes, {32, 5, true}, 302);
    hashAccess.add(second, bytes, {32, 6, true}, 302);
    diskmap::DuplicateAnalysisOptions hashOptions;
    hashOptions.partial_bytes = 0;
    hashOptions.read_buffer_bytes = 2;
    diskmap::ScanCancellationToken hashToken;
    const DuplicateAnalysis hashCancelled = diskmap::analyzeDuplicates(
        scanWith({fileNode(first, bytes, {32, 5, true}, 302),
                  fileNode(second, bytes, {32, 6, true}, 302)}),
        hashOptions, &hashAccess,
        [&hashToken](std::size_t filesProcessed, std::uint64_t bytesRead) {
            if (filesProcessed == 0 && bytesRead >= 2) {
                hashToken.cancel();
            }
        },
        &hashToken);
    CHECK(hashCancelled.cancelled);
    CHECK_EQ(hashCancelled.files_hashed, std::size_t(0));
    CHECK(hasIssue(hashCancelled, DuplicateIssueKind::Cancelled));

    FakeAccess fingerprintAccess;
    const std::string shortBytes = "ab";
    fingerprintAccess.add(first, shortBytes, {32, 7, true}, 302);
    fingerprintAccess.add(second, shortBytes, {32, 8, true}, 302);
    diskmap::DuplicateAnalysisOptions fingerprintOptions;
    fingerprintOptions.partial_bytes = shortBytes.size();
    fingerprintOptions.read_buffer_bytes = shortBytes.size();
    diskmap::ScanCancellationToken fingerprintToken;
    std::size_t fingerprintCallbacks = 0;
    const DuplicateAnalysis fingerprintCancelled = diskmap::analyzeDuplicates(
        scanWith({fileNode(first, shortBytes, {32, 7, true}, 302),
                  fileNode(second, shortBytes, {32, 8, true}, 302)}),
        fingerprintOptions, &fingerprintAccess,
        [&fingerprintToken, &fingerprintCallbacks](std::size_t, std::uint64_t) {
            ++fingerprintCallbacks;
            if (fingerprintCallbacks == 2) {
                fingerprintToken.cancel();
            }
        },
        &fingerprintToken);
    CHECK(fingerprintCancelled.cancelled);
    CHECK_EQ(fingerprintCancelled.files_hashed, std::size_t(0));
    CHECK(hasIssue(fingerprintCancelled, DuplicateIssueKind::Cancelled));

    FakeAccess comparisonAccess;
    comparisonAccess.add(first, bytes, {32, 9, true}, 302);
    comparisonAccess.add(second, bytes, {32, 10, true}, 302);
    diskmap::DuplicateAnalysisOptions comparisonOptions;
    comparisonOptions.partial_bytes = 0;
    comparisonOptions.read_buffer_bytes = 2;
    diskmap::ScanCancellationToken comparisonToken;
    std::size_t comparisonCallbacks = 0;
    const DuplicateAnalysis comparisonCancelled = diskmap::analyzeDuplicates(
        scanWith({fileNode(first, bytes, {32, 9, true}, 302),
                  fileNode(second, bytes, {32, 10, true}, 302)}),
        comparisonOptions, &comparisonAccess,
        [&comparisonToken, &comparisonCallbacks](std::size_t filesProcessed,
                                                  std::uint64_t) {
            if (filesProcessed >= 2) {
                ++comparisonCallbacks;
                if (comparisonCallbacks == 2) {
                    comparisonToken.cancel();
                }
            }
        },
        &comparisonToken);
    CHECK(comparisonCancelled.cancelled);
    CHECK(hasIssue(comparisonCancelled, DuplicateIssueKind::Cancelled));
    CHECK_EQ(comparisonCancelled.groups.size(), std::size_t(0));
}

void testReadFailureModesAndErrorLimit() {
    const fs::path first = "/virtual/root/read-a";
    const fs::path second = "/virtual/root/read-b";
    const std::string bytes = "read failure payload";
    const auto pairNodes = [&]() {
        return std::vector<FsNode>{fileNode(first, bytes, {33, 1, true}, 303),
                                   fileNode(second, bytes, {33, 2, true}, 303)};
    };

    FakeAccess partialFailureAccess;
    partialFailureAccess.add(first, bytes, {33, 1, true}, 303);
    partialFailureAccess.add(second, bytes, {33, 2, true}, 303);
    partialFailureAccess.failReadAt(first, 1, "partial read failed");
    diskmap::DuplicateAnalysisOptions partialOptions;
    partialOptions.partial_bytes = 3;
    partialOptions.read_buffer_bytes = 2;
    const DuplicateAnalysis partialFailure = diskmap::analyzeDuplicates(
        scanWith(pairNodes()), partialOptions, &partialFailureAccess);
    CHECK(partialFailure.groups.empty());
    CHECK(hasIssue(partialFailure, DuplicateIssueKind::ReadError));

    FakeAccess lastPartialFailureAccess;
    lastPartialFailureAccess.add(first, bytes, {33, 1, true}, 303);
    lastPartialFailureAccess.add(second, bytes, {33, 2, true}, 303);
    // Two first-range chunks occur before the last-range read for the first file.
    lastPartialFailureAccess.failReadAt(first, 3, "last partial read failed");
    const DuplicateAnalysis lastPartialFailure = diskmap::analyzeDuplicates(
        scanWith(pairNodes()), partialOptions, &lastPartialFailureAccess);
    CHECK(lastPartialFailure.groups.empty());
    CHECK(hasIssue(lastPartialFailure, DuplicateIssueKind::ReadError));

    FakeAccess emptyRangeAccess;
    emptyRangeAccess.add(first, bytes, {33, 1, true}, 303);
    emptyRangeAccess.add(second, bytes, {33, 2, true}, 303);
    emptyRangeAccess.returnEmptyAt(first, 1);
    const DuplicateAnalysis emptyRange = diskmap::analyzeDuplicates(
        scanWith(pairNodes()), partialOptions, &emptyRangeAccess);
    CHECK(emptyRange.groups.empty());
    CHECK(hasIssue(emptyRange, DuplicateIssueKind::ReadError));

    FakeAccess oversizedRangeAccess;
    oversizedRangeAccess.add(first, bytes, {33, 1, true}, 303);
    oversizedRangeAccess.add(second, bytes, {33, 2, true}, 303);
    oversizedRangeAccess.returnOversizedAt(first, 1);
    const DuplicateAnalysis oversizedRange = diskmap::analyzeDuplicates(
        scanWith(pairNodes()), partialOptions, &oversizedRangeAccess);
    CHECK(oversizedRange.groups.empty());
    CHECK(hasIssue(oversizedRange, DuplicateIssueKind::ReadError));

    diskmap::DuplicateAnalysisOptions fullOptions;
    fullOptions.partial_bytes = 0;
    fullOptions.read_buffer_bytes = 4;
    FakeAccess fullFailureAccess;
    fullFailureAccess.add(first, bytes, {33, 1, true}, 303);
    fullFailureAccess.add(second, bytes, {33, 2, true}, 303);
    fullFailureAccess.failReadAt(first, 1, "full hash read failed");
    const DuplicateAnalysis fullFailure = diskmap::analyzeDuplicates(
        scanWith(pairNodes()), fullOptions, &fullFailureAccess);
    CHECK(fullFailure.groups.empty());
    CHECK(hasIssue(fullFailure, DuplicateIssueKind::ReadError));

    FakeAccess fullInvalidAccess;
    fullInvalidAccess.add(first, bytes, {33, 1, true}, 303);
    fullInvalidAccess.add(second, bytes, {33, 2, true}, 303);
    fullInvalidAccess.returnEmptyAt(first, 1);
    const DuplicateAnalysis fullInvalid = diskmap::analyzeDuplicates(
        scanWith(pairNodes()), fullOptions, &fullInvalidAccess);
    CHECK(fullInvalid.groups.empty());
    CHECK(hasIssue(fullInvalid, DuplicateIssueKind::ReadError));

    FakeAccess leftComparisonFailureAccess;
    leftComparisonFailureAccess.add(first, bytes, {33, 1, true}, 303);
    leftComparisonFailureAccess.add(second, bytes, {33, 2, true}, 303);
    // Each 20-byte file takes five four-byte reads to hash. Calls 11 and 12
    // are the first exact-comparison reads for the representative and peer.
    leftComparisonFailureAccess.failReadAt(first, 11, "left comparison read failed");
    const DuplicateAnalysis leftComparisonFailure = diskmap::analyzeDuplicates(
        scanWith(pairNodes()), fullOptions, &leftComparisonFailureAccess);
    CHECK(leftComparisonFailure.groups.empty());
    CHECK(hasIssue(leftComparisonFailure, DuplicateIssueKind::ReadError));

    FakeAccess rightComparisonFailureAccess;
    rightComparisonFailureAccess.add(first, bytes, {33, 1, true}, 303);
    rightComparisonFailureAccess.add(second, bytes, {33, 2, true}, 303);
    rightComparisonFailureAccess.failReadAt(second, 12, "right comparison read failed");
    const DuplicateAnalysis rightComparisonFailure = diskmap::analyzeDuplicates(
        scanWith(pairNodes()), fullOptions, &rightComparisonFailureAccess);
    CHECK(rightComparisonFailure.groups.empty());
    CHECK(hasIssue(rightComparisonFailure, DuplicateIssueKind::ReadError));

    const auto invalidNodes = [&]() {
        std::vector<FsNode> nodes;
        for (int index = 0; index < 3; ++index) {
            const fs::path path = "/virtual/root/invalid-" + std::to_string(index);
            nodes.push_back(fileNode(path, "invalid", {33, 20 + static_cast<std::uint64_t>(index),
                                                         true},
                                      303));
            nodes.back().metadata.identity.valid = false;
        }
        return nodes;
    };
    diskmap::DuplicateAnalysisOptions boundedErrors;
    boundedErrors.max_errors = 1;
    const DuplicateAnalysis bounded = diskmap::analyzeDuplicates(
        scanWith(invalidNodes()), boundedErrors);
    CHECK_EQ(bounded.issues.size(), std::size_t(1));
    CHECK(bounded.errors_truncated);
    CHECK(!bounded.complete);

    diskmap::DuplicateAnalysisOptions noErrors;
    noErrors.max_errors = 0;
    const DuplicateAnalysis noErrorDetails = diskmap::analyzeDuplicates(
        scanWith(invalidNodes()), noErrors);
    CHECK(noErrorDetails.issues.empty());
    CHECK(noErrorDetails.errors_truncated);
    CHECK(noErrorDetails.uncertain);
}

void testLimitsHardLinksEmptyAndOrdering() {
    const fs::path first = "/virtual/root/limit-a";
    const fs::path second = "/virtual/root/limit-b";
    const fs::path third = "/virtual/root/limit-c";
    const fs::path fourth = "/virtual/root/limit-d";
    const std::string bytes = "bounded duplicate payload";
    FakeAccess access;
    access.add(first, bytes, {34, 1, true}, 304);
    access.add(second, bytes, {34, 2, true}, 304);
    access.add(third, bytes, {34, 3, true}, 304);
    access.add(fourth, bytes, {34, 4, true}, 304);
    diskmap::DuplicateAnalysisOptions candidateOptions;
    candidateOptions.max_candidates = 2;
    const DuplicateAnalysis candidateLimit = diskmap::analyzeDuplicates(
        scanWith({fileNode(first, bytes, {34, 1, true}, 304),
                  fileNode(second, bytes, {34, 2, true}, 304),
                  fileNode(third, bytes, {34, 3, true}, 304),
                  fileNode(fourth, bytes, {34, 4, true}, 304)}),
        candidateOptions, &access);
    CHECK_EQ(candidateLimit.candidates_seen, std::size_t(4));
    CHECK_EQ(candidateLimit.candidates_retained, std::size_t(2));
    CHECK(candidateLimit.truncated);
    CHECK(hasIssue(candidateLimit, DuplicateIssueKind::CandidateLimit));

    const fs::path otherFirst = "/virtual/root/group-a";
    const fs::path otherSecond = "/virtual/root/group-b";
    FakeAccess groupLimitAccess;
    groupLimitAccess.add(otherFirst, "group payload", {34, 5, true}, 304);
    groupLimitAccess.add(otherSecond, "group payload", {34, 6, true}, 304);
    diskmap::DuplicateAnalysisOptions groupOptions;
    groupOptions.max_groups = 0;
    const DuplicateAnalysis groupLimit = diskmap::analyzeDuplicates(
        scanWith({fileNode(otherFirst, "group payload", {34, 5, true}, 304),
                  fileNode(otherSecond, "group payload", {34, 6, true}, 304)}),
        groupOptions, &groupLimitAccess);
    CHECK(groupLimit.groups.empty());
    CHECK(groupLimit.truncated);
    CHECK(hasIssue(groupLimit, DuplicateIssueKind::GroupLimit));

    const fs::path mismatchFirst = "/virtual/root/member-a";
    const fs::path mismatchSecond = "/virtual/root/member-b";
    FakeAccess memberSkipAccess;
    memberSkipAccess.add(mismatchFirst, "abcd", {34, 7, true}, 304);
    memberSkipAccess.add(mismatchSecond, "wxyz", {34, 8, true}, 304);
    diskmap::DuplicateAnalysisOptions memberSkipOptions;
    memberSkipOptions.partial_bytes = 0;
    memberSkipOptions.max_group_members = 1;
    const DuplicateAnalysis memberSkip = diskmap::analyzeDuplicates(
        scanWith({fileNode(mismatchFirst, "abcd", {34, 7, true}, 304),
                  fileNode(mismatchSecond, "wxyz", {34, 8, true}, 304)}),
        memberSkipOptions, &memberSkipAccess);
    CHECK(memberSkip.groups.empty());
    CHECK(memberSkip.truncated);
    CHECK(hasIssue(memberSkip, DuplicateIssueKind::MemberLimit));

    const fs::path uncertainFirst = "/virtual/root/links-a";
    const fs::path uncertainSecond = "/virtual/root/links-b";
    FakeAccess uncertainLinksAccess;
    uncertainLinksAccess.add(uncertainFirst, bytes, {34, 9, true}, 304, 2);
    uncertainLinksAccess.add(uncertainSecond, bytes, {34, 10, true}, 304, 2);
    const DuplicateAnalysis uncertainLinks = diskmap::analyzeDuplicates(
        scanWith({fileNode(uncertainFirst, bytes, {34, 9, true}, 304, 2),
                  fileNode(uncertainSecond, bytes, {34, 10, true}, 304, 2)}),
        {}, &uncertainLinksAccess);
    CHECK_EQ(uncertainLinks.groups.size(), std::size_t(1));
    CHECK(uncertainLinks.groups.front().hard_link_alias);
    CHECK(!uncertainLinks.groups.front().reclaimable);
    CHECK(hasIssue(uncertainLinks, DuplicateIssueKind::HardLinkAlias));
    CHECK(uncertainLinks.groups.front().reason.find("unavailable") != std::string::npos);

    const fs::path emptyFirst = "/virtual/root/empty-a";
    const fs::path emptySecond = "/virtual/root/empty-b";
    FakeAccess emptyAccess;
    emptyAccess.add(emptyFirst, "", {34, 11, true}, 304);
    emptyAccess.add(emptySecond, "", {34, 12, true}, 304);
    const DuplicateAnalysis empty = diskmap::analyzeDuplicates(
        scanWith({fileNode(emptyFirst, "", {34, 11, true}, 304),
                  fileNode(emptySecond, "", {34, 12, true}, 304)}),
        {}, &emptyAccess);
    CHECK_EQ(empty.groups.size(), std::size_t(1));
    CHECK(empty.groups.front().certain);
    CHECK(!empty.groups.front().reclaimable);
    CHECK(empty.groups.front().reason.find("empty") != std::string::npos);

    // A size-only partial key makes two distinct full hashes share one
    // partial bucket, exercising deterministic full-hash ordering as well as
    // the final content-hash tie-breaker in sortAnalysis().
    const fs::path groupA = "/virtual/root/order-a";
    const fs::path groupB = "/virtual/root/order-b";
    const fs::path groupC = "/virtual/root/order-c";
    const fs::path groupD = "/virtual/root/order-d";
    const std::string firstPayload = "aaaa";
    const std::string secondPayload = "zzzz";
    FakeAccess orderingAccess;
    orderingAccess.add(groupA, firstPayload, {34, 13, true}, 304);
    orderingAccess.add(groupB, firstPayload, {34, 14, true}, 304);
    orderingAccess.add(groupC, secondPayload, {34, 15, true}, 304);
    orderingAccess.add(groupD, secondPayload, {34, 16, true}, 304);
    diskmap::DuplicateAnalysisOptions orderingOptions;
    orderingOptions.partial_bytes = 0;
    const DuplicateAnalysis ordering = diskmap::analyzeDuplicates(
        scanWith({fileNode(groupA, firstPayload, {34, 13, true}, 304),
                  fileNode(groupB, firstPayload, {34, 14, true}, 304),
                  fileNode(groupC, secondPayload, {34, 15, true}, 304),
                  fileNode(groupD, secondPayload, {34, 16, true}, 304)}),
        orderingOptions, &orderingAccess);
    CHECK_EQ(ordering.groups.size(), std::size_t(2));
    CHECK_EQ(ordering.groups[0].size, std::uint64_t(4));
    CHECK_EQ(ordering.groups[1].size, std::uint64_t(4));
    CHECK(ordering.groups[0].content_hash != ordering.groups[1].content_hash);
}

void testCandidateMetadataAndTraversal() {
    const fs::path path = "/virtual/root/rejected";
    const std::string bytes = "candidate metadata";
    const auto expectIssue = [&](const std::function<void(FsNode&)>& change,
                                 DuplicateIssueKind kind) {
        FsNode node = fileNode(path, bytes, {35, 1, true}, 305);
        change(node);
        const DuplicateAnalysis analysis = diskmap::analyzeDuplicates(
            scanWith({std::move(node)}));
        CHECK(analysis.groups.empty());
        CHECK(hasIssue(analysis, kind));
    };

    expectIssue([](FsNode& node) { node.followed = true; },
                DuplicateIssueKind::UnsupportedType);
    expectIssue([](FsNode& node) { node.complete = false; },
                DuplicateIssueKind::MetadataUnknown);
    expectIssue([](FsNode& node) { node.error = "listing metadata failed"; },
                DuplicateIssueKind::MetadataUnknown);
    expectIssue([](FsNode& node) { node.logical_size_known = false; },
                DuplicateIssueKind::MetadataUnknown);
    expectIssue([](FsNode& node) { node.metadata.complete = false; },
                DuplicateIssueKind::MetadataUnknown);
    expectIssue([](FsNode& node) { node.metadata.identity.valid = false; },
                DuplicateIssueKind::MetadataUnknown);
    expectIssue([](FsNode& node) { node.metadata.modified_time_known = false; },
                DuplicateIssueKind::MetadataUnknown);
    expectIssue([](FsNode& node) { node.size += 1; }, DuplicateIssueKind::SizeChanged);

    // A non-complete `Other` record is conservatively represented as the
    // entry's structural kind. It is not a regular-file candidate.
    FsNode unknown = fileNode("/virtual/root/unknown", bytes, {35, 2, true}, 305);
    unknown.metadata.kind = FsKind::Other;
    unknown.metadata.complete = false;
    const DuplicateAnalysis representedOther =
        diskmap::analyzeDuplicates(scanWith({std::move(unknown)}));
    CHECK(representedOther.groups.empty());
    CHECK_EQ(representedOther.candidates_seen, std::size_t(0));

    // Empty paths use the retained entry name and still produce a stable key.
    const fs::path relativePath = "relative-a";
    const fs::path absolutePath = "/virtual/root/relative-b";
    FakeAccess relativeAccess;
    relativeAccess.add(relativePath, bytes, {35, 3, true}, 305);
    relativeAccess.add(absolutePath, bytes, {35, 4, true}, 305);
    FsNode relative = fileNode("/not-retained/relative-a", bytes, {35, 3, true}, 305);
    relative.path.clear();
    relative.name = relativePath.string();
    const DuplicateAnalysis relativeResult = diskmap::analyzeDuplicates(
        scanWith({std::move(relative),
                  fileNode(absolutePath, bytes, {35, 4, true}, 305)}),
        {}, &relativeAccess);
    CHECK_EQ(relativeResult.groups.size(), std::size_t(1));
    CHECK(std::any_of(relativeResult.groups.front().entries.begin(),
                      relativeResult.groups.front().entries.end(),
                      [&](const diskmap::DuplicateEntry& entry) {
                          return entry.path == relativePath;
                      }));

    // These directory entries deliberately suppress traversal. Their nested
    // files therefore cannot become duplicate candidates.
    FsNode cycle = FsNode{};
    cycle.name = "cycle";
    cycle.path = "/virtual/root/cycle";
    cycle.is_dir = true;
    cycle.metadata = directoryMetadata();
    cycle.cycle_skipped = true;
    cycle.children.push_back(fileNode("/virtual/root/cycle/a", bytes, {35, 5, true}, 305));
    FsNode mount = FsNode{};
    mount.name = "mount";
    mount.path = "/virtual/root/mount";
    mount.is_dir = true;
    mount.metadata = directoryMetadata();
    mount.mount_boundary_skipped = true;
    mount.children.push_back(fileNode("/virtual/root/mount/b", bytes, {35, 6, true}, 305));
    FsNode followedDirectory = FsNode{};
    followedDirectory.name = "followed-dir";
    followedDirectory.path = "/virtual/root/followed-dir";
    followedDirectory.is_dir = true;
    followedDirectory.followed = true;
    followedDirectory.metadata.kind = FsKind::Symlink;
    followedDirectory.metadata.complete = true;
    followedDirectory.has_target_metadata = true;
    followedDirectory.target_metadata = directoryMetadata();
    followedDirectory.children.push_back(
        fileNode("/virtual/root/followed-dir/c", bytes, {35, 7, true}, 305));
    const DuplicateAnalysis suppressed = diskmap::analyzeDuplicates(
        scanWith({std::move(cycle), std::move(mount), std::move(followedDirectory)}));
    CHECK(suppressed.groups.empty());
    CHECK_EQ(suppressed.candidates_seen, std::size_t(0));

    const fs::path uncertainA = "/virtual/root/uncertain/a";
    const fs::path uncertainB = "/virtual/root/uncertain/b";
    FakeAccess uncertainAccess;
    uncertainAccess.add(uncertainA, bytes, {35, 8, true}, 305);
    uncertainAccess.add(uncertainB, bytes, {35, 9, true}, 305);
    FsNode uncertainDirectory = FsNode{};
    uncertainDirectory.name = "uncertain";
    uncertainDirectory.path = "/virtual/root/uncertain";
    uncertainDirectory.is_dir = true;
    uncertainDirectory.metadata = directoryMetadata();
    uncertainDirectory.metadata.error = "directory metadata warning";
    uncertainDirectory.children.push_back(
        fileNode(uncertainA, bytes, {35, 8, true}, 305));
    uncertainDirectory.children.push_back(
        fileNode(uncertainB, bytes, {35, 9, true}, 305));
    const DuplicateAnalysis uncertain = diskmap::analyzeDuplicates(
        scanWith({std::move(uncertainDirectory)}), {}, &uncertainAccess);
    CHECK_EQ(uncertain.groups.size(), std::size_t(1));
    CHECK(!uncertain.groups.front().certain);
    CHECK(!uncertain.groups.front().reclaimable);
}

void testScanIssueFlags() {
    const fs::path first = "/virtual/root/scan-a";
    const fs::path second = "/virtual/root/scan-b";
    const std::string bytes = "scan issue payload";
    const auto run = [&](const std::function<void(ScanResult&)>& configure) {
        FakeAccess access;
        access.add(first, bytes, {36, 1, true}, 306);
        access.add(second, bytes, {36, 2, true}, 306);
        ScanResult scan = scanWith({fileNode(first, bytes, {36, 1, true}, 306),
                                    fileNode(second, bytes, {36, 2, true}, 306)});
        configure(scan);
        const DuplicateAnalysis analysis = diskmap::analyzeDuplicates(scan, {}, &access);
        CHECK_EQ(analysis.groups.size(), std::size_t(1));
        CHECK(!analysis.complete);
        CHECK(analysis.uncertain);
        return analysis;
    };

    const DuplicateAnalysis cancelled = [](const auto& runFn) {
        return runFn([](ScanResult& scan) { scan.cancelled = true; });
    }(run);
    CHECK(cancelled.cancelled);
    CHECK(hasIssue(cancelled, DuplicateIssueKind::Cancelled));

    const DuplicateAnalysis fatal = run([](ScanResult& scan) {
        scan.fatal_error = "fatal retained scan error";
    });
    CHECK(hasIssue(fatal, DuplicateIssueKind::IncompleteScan));
    CHECK(fatal.issues.front().message == "fatal retained scan error"
          || fatal.issues.back().message == "fatal retained scan error");

    const DuplicateAnalysis listingCount = run([](ScanResult& scan) {
        scan.error_count = 1;
    });
    CHECK(hasIssue(listingCount, DuplicateIssueKind::IncompleteScan));

    const DuplicateAnalysis listingDetails = run([](ScanResult& scan) {
        scan.errors.push_back("one listing failed");
    });
    CHECK(hasIssue(listingDetails, DuplicateIssueKind::IncompleteScan));

    const DuplicateAnalysis listingTruncated = run([](ScanResult& scan) {
        scan.errors_truncated = true;
    });
    CHECK(hasIssue(listingTruncated, DuplicateIssueKind::IncompleteScan));

    const DuplicateAnalysis filtered = run([](ScanResult& scan) {
        scan.entries_filtered = 1;
    });
    CHECK(hasIssue(filtered, DuplicateIssueKind::FilteredScan));

    const DuplicateAnalysis mount = run([](ScanResult& scan) {
        scan.mount_boundaries_skipped = 1;
    });
    CHECK(hasIssue(mount, DuplicateIssueKind::IncompleteScan));

    const DuplicateAnalysis filteredTotals = run([](ScanResult& scan) {
        scan.totals_filtered = true;
    });
    CHECK(hasIssue(filteredTotals, DuplicateIssueKind::FilteredScan));
}

void testComparisonOutcomesAndGroupLimit() {
    const fs::path badFirst = "/virtual/root/a-bad";
    const fs::path badSecond = "/virtual/root/b-bad";
    const fs::path goodFirst = "/virtual/root/c-good";
    const fs::path goodSecond = "/virtual/root/d-good";
    const std::string badBytes = "1234";
    const std::string goodBytes = "wxyz";
    const std::vector<FsNode> nodes{
        fileNode(badFirst, badBytes, {8, 1, true}, 40),
        fileNode(badSecond, badBytes, {8, 2, true}, 40),
        fileNode(goodFirst, goodBytes, {8, 3, true}, 40),
        fileNode(goodSecond, goodBytes, {8, 4, true}, 40),
    };
    diskmap::DuplicateAnalysisOptions options;
    options.max_groups = 1;

    FakeAccess mismatchAccess;
    mismatchAccess.add(badFirst, badBytes, {8, 1, true}, 40);
    mismatchAccess.add(badSecond, badBytes, {8, 2, true}, 40);
    mismatchAccess.add(goodFirst, goodBytes, {8, 3, true}, 40);
    mismatchAccess.add(goodSecond, goodBytes, {8, 4, true}, 40);
    // Four partial reads and two full-hash reads precede byte verification.
    // Change only the second bad candidate while it is being compared so its
    // stored hash agrees while its bytes disagree.
    mismatchAccess.mutateBytesAfterRead(badSecond, 8, "9999");
    const DuplicateAnalysis mismatch =
        diskmap::analyzeDuplicates(scanWith(nodes), options, &mismatchAccess);
    CHECK_EQ(mismatch.groups.size(), std::size_t(1));
    CHECK_EQ(mismatch.groups.front().entries.size(), std::size_t(2));
    CHECK_EQ(mismatch.groups.front().entries.front().path, goodFirst);
    CHECK(hasIssue(mismatch, DuplicateIssueKind::HashMismatch));
    CHECK(!hasIssue(mismatch, DuplicateIssueKind::ReadError));
    CHECK(!hasIssue(mismatch, DuplicateIssueKind::GroupLimit));

    FakeAccess readErrorAccess;
    readErrorAccess.add(badFirst, badBytes, {8, 1, true}, 40);
    readErrorAccess.add(badSecond, badBytes, {8, 2, true}, 40);
    readErrorAccess.add(goodFirst, goodBytes, {8, 3, true}, 40);
    readErrorAccess.add(goodSecond, goodBytes, {8, 4, true}, 40);
    readErrorAccess.failReadAt(badSecond, 8, "simulated comparison read failure");
    const DuplicateAnalysis readError =
        diskmap::analyzeDuplicates(scanWith(nodes), options, &readErrorAccess);
    CHECK_EQ(readError.groups.size(), std::size_t(1));
    CHECK(hasIssue(readError, DuplicateIssueKind::ReadError));
    CHECK(!hasIssue(readError, DuplicateIssueKind::HashMismatch));
    CHECK(!hasIssue(readError, DuplicateIssueKind::GroupLimit));

    // Once max_groups is full, a later partial bucket with different full
    // hashes is not itself a group and must not manufacture GroupLimit.
    const fs::path noGroupFirst = "/virtual/root/e-no-group";
    const fs::path noGroupSecond = "/virtual/root/f-no-group";
    const std::vector<FsNode> laterMismatchNodes{
        fileNode(badFirst, badBytes, {8, 11, true}, 40),
        fileNode(badSecond, badBytes, {8, 12, true}, 40),
        fileNode(noGroupFirst, "w0z1", {8, 13, true}, 40),
        fileNode(noGroupSecond, "w9z1", {8, 14, true}, 40),
    };
    FakeAccess laterMismatchAccess;
    laterMismatchAccess.add(badFirst, badBytes, {8, 11, true}, 40);
    laterMismatchAccess.add(badSecond, badBytes, {8, 12, true}, 40);
    laterMismatchAccess.add(noGroupFirst, "w0z1", {8, 13, true}, 40);
    laterMismatchAccess.add(noGroupSecond, "w9z1", {8, 14, true}, 40);
    options.partial_bytes = 1;
    const DuplicateAnalysis laterMismatch = diskmap::analyzeDuplicates(
        scanWith(laterMismatchNodes), options, &laterMismatchAccess);
    CHECK_EQ(laterMismatch.groups.size(), std::size_t(1));
    CHECK(!laterMismatch.truncated);
    CHECK(!hasIssue(laterMismatch, DuplicateIssueKind::GroupLimit));

    // A single partial bucket can contain multiple full-hash groups. The
    // second verified group must be the event that consumes max_groups.
    FakeAccess samePartialAccess;
    samePartialAccess.add(badFirst, badBytes, {8, 1, true}, 40);
    samePartialAccess.add(badSecond, badBytes, {8, 2, true}, 40);
    samePartialAccess.add(goodFirst, goodBytes, {8, 3, true}, 40);
    samePartialAccess.add(goodSecond, goodBytes, {8, 4, true}, 40);
    options.partial_bytes = 0;
    const DuplicateAnalysis samePartial =
        diskmap::analyzeDuplicates(scanWith(nodes), options, &samePartialAccess);
    CHECK_EQ(samePartial.groups.size(), std::size_t(1));
    CHECK(samePartial.truncated);
    CHECK(hasIssue(samePartial, DuplicateIssueKind::GroupLimit));
}

void testBoundsAndSymlinks() {
    const fs::path first = "/virtual/root/a";
    const fs::path second = "/virtual/root/b";
    const fs::path third = "/virtual/root/c";
    const std::string bytes = "bounded duplicate";
    FakeAccess access;
    access.add(first, bytes, {5, 1, true}, 1);
    access.add(second, bytes, {5, 2, true}, 1);
    access.add(third, bytes, {5, 3, true}, 1);
    std::vector<FsNode> nodes{fileNode(first, bytes, {5, 1, true}, 1),
                              fileNode(second, bytes, {5, 2, true}, 1),
                              fileNode(third, bytes, {5, 3, true}, 1)};
    FsNode link;
    link.name = "link";
    link.path = "/virtual/root/link";
    link.is_dir = false;
    link.followed = true;
    link.metadata.kind = FsKind::Symlink;
    link.metadata.complete = true;
    link.target_metadata.kind = FsKind::RegularFile;
    link.target_metadata.complete = true;
    nodes.push_back(std::move(link));
    diskmap::DuplicateAnalysisOptions options;
    options.max_group_members = 2;
    const DuplicateAnalysis members =
        diskmap::analyzeDuplicates(scanWith(std::move(nodes)), options, &access);
    CHECK_EQ(members.groups.size(), std::size_t(1));
    CHECK_EQ(members.groups.front().entries.size(), std::size_t(2));
    CHECK(!members.groups.front().reclaimable);
    CHECK(hasIssue(members, DuplicateIssueKind::MemberLimit));

    options.max_candidates = 2;
    const DuplicateAnalysis candidates = diskmap::analyzeDuplicates(
        scanWith({fileNode(first, bytes, {5, 1, true}, 1),
                  fileNode(second, bytes, {5, 2, true}, 1),
                  fileNode(third, bytes, {5, 3, true}, 1)}),
        options, &access);
    CHECK_EQ(candidates.candidates_seen, std::size_t(3));
    CHECK(candidates.truncated);
    CHECK(hasIssue(candidates, DuplicateIssueKind::CandidateLimit));
}

class TempDirectory {
public:
    TempDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path parent = fs::temp_directory_path();
        std::error_code error;
        for (unsigned attempt = 0; attempt != 100; ++attempt) {
            const fs::path candidate =
                parent / ("diskmap-duplicates-" + std::to_string(stamp) + "-"
                          + std::to_string(attempt));
            error.clear();
            if (fs::create_directory(candidate, error)) {
                path_ = candidate;
                return;
            }
            if (error && error != std::make_error_code(std::errc::file_exists)) {
                break;
            }
        }
        throw std::runtime_error("cannot create duplicate-analysis test directory");
    }
    ~TempDirectory() { std::error_code ignored; fs::remove_all(path_, ignored); }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void writeFile(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void testRealFiles() {
    TempDirectory temporary;
    const fs::path root = temporary.path() / "root";
    fs::create_directories(root);
    writeFile(root / "first.bin", "real duplicate payload");
    writeFile(root / "second.bin", "real duplicate payload");
    writeFile(root / "different.bin", "different payload here");
#if !defined(_WIN32)
    std::error_code linkError;
    fs::create_symlink(root / "first.bin", root / "alias.bin", linkError);
#endif
    diskmap::RealFsSource source;
    diskmap::ScanOptions scanOptions;
    scanOptions.generation = 99;
    const ScanResult scan = diskmap::scan(source, root, scanOptions);
    diskmap::DuplicateAnalysisOptions options;
    options.partial_bytes = 4;
    options.read_buffer_bytes = 5;
    const DuplicateAnalysis analysis = diskmap::analyzeDuplicates(scan, options);
    CHECK_EQ(analysis.groups.size(), std::size_t(1));
    CHECK(analysis.groups.front().certain);
    CHECK(analysis.groups.front().reclaimable);
    CHECK(std::all_of(analysis.groups.front().entries.begin(),
                      analysis.groups.front().entries.end(), [](const auto& entry) {
                          return entry.path.filename() != "alias.bin";
                      }));
}

void testSystemReadRaceRefusals() {
#if defined(__linux__)
    const auto run = [](bool replaceWithSymlink) {
        TempDirectory temporary;
        const fs::path root = temporary.path() / "root";
        const fs::path raced = root / "a-raced.bin";
        const fs::path stable = root / "b-stable.bin";
        const fs::path replacement = temporary.path() / "replacement.bin";
        const std::string bytes = "0123456789abcdef";
        fs::create_directories(root);
        writeFile(raced, bytes);
        writeFile(stable, bytes);
        writeFile(replacement, "replacement payload");

        diskmap::RealFsSource source;
        diskmap::ScanOptions scanOptions;
        scanOptions.generation = 100;
        const ScanResult scan = diskmap::scan(source, root, scanOptions);

        if (replaceWithSymlink) {
            // Symlink support is a filesystem policy rather than a duplicate
            // analyzer invariant.  Skip only this variant when the fixture
            // filesystem disallows creating symlinks.
            const fs::path probe = temporary.path() / "symlink-probe";
            std::error_code probeError;
            fs::create_symlink(replacement, probe, probeError);
            if (probeError) {
                std::printf("SKIP system duplicate symlink race: %s\n",
                            probeError.message().c_str());
                return;
            }
            probeError.clear();
            fs::remove(probe, probeError);
            CHECK(!probeError);
        }

        diskmap::DuplicateAnalysisOptions options;
        options.partial_bytes = 4;
        options.read_buffer_bytes = 4;
        bool mutated = false;
        std::error_code mutationError;
        const DuplicateAnalysis analysis = diskmap::analyzeDuplicates(
            scan, options, nullptr,
            [&](std::size_t, std::uint64_t bytesRead) {
                if (mutated || bytesRead < options.partial_bytes) {
                    return;
                }
                mutationError.clear();
                fs::remove(raced, mutationError);
                if (!mutationError) {
                    if (replaceWithSymlink) {
                        fs::create_symlink(replacement, raced, mutationError);
                    } else {
                        fs::rename(replacement, raced, mutationError);
                    }
                }
                mutated = true;
            });

        CHECK(mutated);
        CHECK(!mutationError);
        CHECK(analysis.groups.empty());
        CHECK(!analysis.complete);
        CHECK(hasIssue(analysis, DuplicateIssueKind::ReadError));
        CHECK(std::any_of(analysis.issues.begin(), analysis.issues.end(),
                          [&](const diskmap::DuplicateIssue& issue) {
                              return issue.path == raced
                                     && issue.kind == DuplicateIssueKind::ReadError;
                          }));
    };

    // The first range is read successfully, then the reviewed path becomes a
    // symlink before the second range. O_NOFOLLOW must fail at open().
    run(true);

    // The first range is read successfully, then the reviewed path becomes a
    // different regular inode. The opened descriptor must be rejected against
    // the retained identity before any replacement bytes are accepted.
    run(false);
#endif
}

} // namespace

int main() {
    testIssueNamesAndOptionValidation();
    testKnownHashes();
    testFakeDuplicateAndStreaming();
    testHardLinksAreNotReclaimable();
    testUncertainEvidence();
    testRevalidationMetadataVariants();
    testRevalidationAndCancellation();
    testCancellationPhases();
    testReadFailureModesAndErrorLimit();
    testLimitsHardLinksEmptyAndOrdering();
    testCandidateMetadataAndTraversal();
    testScanIssueFlags();
    testComparisonOutcomesAndGroupLimit();
    testBoundsAndSymlinks();
    testRealFiles();
    testSystemReadRaceRefusals();
    return testSummary();
}
