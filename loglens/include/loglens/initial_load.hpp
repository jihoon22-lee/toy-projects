#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "loglens/log_parser.hpp"
#include "loglens/log_source.hpp"

namespace loglens {

enum class InitialLoadMode { TailRecords, FromStart };

// An immutable source snapshot selection. The worker must still compare the
// identity returned by its first FileTailer poll: replacement between locating
// and parsing is a retry, never permission to combine two files.
struct InitialLoadWindow {
    std::uint64_t offset = 0;
    std::uint64_t snapshot_end = 0;
    std::size_t first_line_number = 1;
    std::size_t complete_record_count = 0;
    FileIdentity identity;
    SourceError error;
    bool cancelled = false;

    bool ok() const;
};

// Finds the first byte of the last recordCount complete logical records while
// retaining only O(recordCount) offsets. The scan is byte-only and bounded by
// sourceChunkBytes; parsing and model mutation remain separate stages. The
// multiline policy must match the RecordAssembler that will consume the
// selected suffix, otherwise Tail N could select a different logical window.
InitialLoadWindow locateTailWindow(
    const std::string& path, std::size_t recordCount,
    std::size_t sourceChunkBytes = kDefaultSourceChunkBytes,
    const std::function<bool()>& cancelled = std::function<bool()>(),
    MultilinePolicy multilinePolicy = MultilinePolicy::FoldContinuations);

} // namespace loglens
