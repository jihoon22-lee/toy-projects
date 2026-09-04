#pragma once

#include "diskmap/duplicates.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace diskmap {
namespace detail {

class Sha256 {
public:
    Sha256();

    void update(const char* data, std::size_t size);
    std::string finalHex();

private:
    void transform(const unsigned char* block);

    std::array<std::uint32_t, 8> state_{};
    std::array<unsigned char, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t total_bytes_ = 0;
};

std::string partialFingerprint(std::uint64_t size,
                               const std::string& first,
                               const std::string& last);

std::unique_ptr<DuplicateFileAccess> makeSystemDuplicateFileAccess();

struct Candidate {
    std::filesystem::path path;
    NodeKey key;
    FsMetadata evidence;
    std::uint64_t size = 0;
    bool scan_certain = false;
    std::string partial;
    std::string hash;
};

struct PartialKey {
    std::uint64_t size = 0;
    std::string fingerprint;

    bool operator<(const PartialKey& other) const {
        return size < other.size
               || (size == other.size && fingerprint < other.fingerprint);
    }
};

struct PartialBucket {
    std::vector<std::size_t> members;
    bool truncated = false;
};

struct AnalysisContext {
    const DuplicateAnalysisOptions& options;
    const DuplicateFileAccess& access;
    const DuplicateProgressFn& progress;
    const ScanCancellationToken* cancellation = nullptr;
    DuplicateAnalysis& result;
    std::size_t files_processed = 0;

    bool isCancelled() const {
        return cancellation != nullptr && cancellation->isCancelled();
    }
};

void addIssue(AnalysisContext& context,
              const std::filesystem::path& path,
              const NodeKey& key,
              DuplicateIssueKind kind,
              std::string message,
              bool affectsCompleteness = true);

bool inspectStable(const Candidate& candidate,
                   AnalysisContext& context,
                   DuplicateIssueKind& issue,
                   std::string& message);
bool readPartial(Candidate& candidate, AnalysisContext& context);
bool hashFull(Candidate& candidate, AnalysisContext& context);
void fingerprintCandidates(std::vector<Candidate>& candidates,
                           AnalysisContext& context,
                           std::map<PartialKey, PartialBucket>& partials);
void processPartialGroups(std::map<PartialKey, PartialBucket>& partials,
                          std::vector<Candidate>& candidates,
                          AnalysisContext& context);

} // namespace detail
} // namespace diskmap
