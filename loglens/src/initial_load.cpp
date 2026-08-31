#include "loglens/initial_load.hpp"

#include <deque>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "loglens/log_parser.hpp"

namespace loglens {

namespace {

struct RecordStart {
    std::uint64_t offset = 0;
    std::size_t line_number = 1;
};

SourceError changedSourceError() {
    SourceError error;
    error.kind = SourceErrorKind::ReadFailed;
    error.message = "source changed while selecting the initial tail window";
    error.retryable = true;
    return error;
}

SourceError overflowError() {
    SourceError error;
    error.kind = SourceErrorKind::ReadFailed;
    error.message = "physical line count exceeds the supported range";
    error.retryable = false;
    return error;
}

bool cancellationRequested(const std::function<bool()>& cancelled) {
    return cancelled && cancelled();
}

} // namespace

bool InitialLoadWindow::ok() const {
    return error.kind == SourceErrorKind::None && !cancelled;
}

InitialLoadWindow locateTailWindow(const std::string& path, std::size_t recordCount,
                                   std::size_t sourceChunkBytes,
                                   const std::function<bool()>& cancelled) {
    if (recordCount == 0) {
        throw std::invalid_argument("tail record count must be positive");
    }

    InitialLoadWindow result;
    FileTailer source(path, sourceChunkBytes);
    std::deque<RecordStart> starts;
    std::uint64_t lineStartOffset = 0;
    std::size_t physicalLine = 1;
    bool hasRecord = false;
    bool firstChunk = true;
    std::string prefix;
    prefix.reserve(3);

    while (true) {
        if (cancellationRequested(cancelled)) {
            result.cancelled = true;
            return result;
        }

        const SourceChunk chunk = firstChunk ? source.pollChunk()
                                             : source.pollChunk(result.snapshot_end);
        if (!chunk.ok()) {
            result.error = chunk.error;
            return result;
        }
        if (firstChunk) {
            result.snapshot_end = chunk.snapshot_end;
            result.identity = chunk.identity;
            firstChunk = false;
        } else if (chunk.generation_changed || chunk.identity != result.identity) {
            result.error = changedSourceError();
            return result;
        }

        const std::uint64_t chunkStart =
            chunk.position - static_cast<std::uint64_t>(chunk.bytes.size());
        for (std::size_t index = 0; index < chunk.bytes.size(); ++index) {
            if ((index & 0xfffU) == 0U && cancellationRequested(cancelled)) {
                result.cancelled = true;
                return result;
            }
            const char byte = chunk.bytes[index];
            if (byte != '\n') {
                if (prefix.size() < 3) {
                    prefix.push_back(byte);
                }
                continue;
            }

            if (!hasRecord || !isContinuation(prefix)) {
                starts.push_back(RecordStart{lineStartOffset, physicalLine});
                if (starts.size() > recordCount) {
                    starts.pop_front();
                }
                hasRecord = true;
                if (result.complete_record_count
                    == std::numeric_limits<std::size_t>::max()) {
                    result.error = overflowError();
                    return result;
                }
                ++result.complete_record_count;
            }
            if (physicalLine == std::numeric_limits<std::size_t>::max()) {
                result.error = overflowError();
                return result;
            }
            ++physicalLine;
            lineStartOffset = chunkStart + static_cast<std::uint64_t>(index) + 1;
            prefix.clear();
        }

        if (chunk.position >= result.snapshot_end) {
            break;
        }
        if (!chunk.more_available && chunk.bytes.empty()) {
            result.error = changedSourceError();
            return result;
        }
    }

    if (!starts.empty()) {
        result.offset = starts.front().offset;
        result.first_line_number = starts.front().line_number;
    }
    return result;
}

} // namespace loglens
