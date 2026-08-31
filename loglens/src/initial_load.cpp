#include "loglens/initial_load.hpp"

#include <deque>
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

bool cancellationRequested(const std::function<bool()>& cancelled) {
    return cancelled && cancelled();
}

class TailWindowScanner {
public:
    TailWindowScanner(std::size_t recordCount, const std::function<bool()>& cancelled)
        : record_count_(recordCount), cancelled_(cancelled) {
        prefix_.reserve(3);
    }

    bool accept(const SourceChunk& chunk, bool firstChunk) {
        if (!chunk.ok()) {
            result_.error = chunk.error;
            return false;
        }
        if (!acceptSnapshot(chunk, firstChunk)) {
            return false;
        }
        return scanBytes(chunk);
    }

    bool cancellationRequestedNow() {
        if (!cancellationRequested(cancelled_)) {
            return false;
        }
        result_.cancelled = true;
        return true;
    }

    const InitialLoadWindow& result() const { return result_; }

    InitialLoadWindow finish() {
        if (!starts_.empty()) {
            result_.offset = starts_.front().offset;
            result_.first_line_number = starts_.front().line_number;
        }
        return result_;
    }

private:
    bool acceptSnapshot(const SourceChunk& chunk, bool firstChunk) {
        if (firstChunk) {
            result_.snapshot_end = chunk.snapshot_end;
            result_.identity = chunk.identity;
            return true;
        }
        if (!chunk.generation_changed && chunk.identity == result_.identity) {
            return true;
        }
        result_.error = changedSourceError();
        return false;
    }

    bool scanBytes(const SourceChunk& chunk) {
        const std::uint64_t chunkStart =
            chunk.position - static_cast<std::uint64_t>(chunk.bytes.size());
        for (std::size_t index = 0; index < chunk.bytes.size(); ++index) {
            if ((index & 0xfffU) == 0U && cancellationRequestedNow()) {
                return false;
            }
            const char byte = chunk.bytes[index];
            if (byte == '\n') {
                completeLine(chunkStart + static_cast<std::uint64_t>(index) + 1);
            } else if (prefix_.size() < 3) {
                prefix_.push_back(byte);
            }
        }
        return true;
    }

    void completeLine(std::uint64_t nextLineOffset) {
        if (!has_record_ || !isContinuation(prefix_)) {
            starts_.push_back(RecordStart{line_start_offset_, physical_line_});
            if (starts_.size() > record_count_) {
                starts_.pop_front();
            }
            has_record_ = true;
            ++result_.complete_record_count;
        }
        ++physical_line_;
        line_start_offset_ = nextLineOffset;
        prefix_.clear();
    }

    std::size_t record_count_;
    const std::function<bool()>& cancelled_;
    InitialLoadWindow result_;
    std::deque<RecordStart> starts_;
    std::uint64_t line_start_offset_ = 0;
    std::size_t physical_line_ = 1;
    bool has_record_ = false;
    std::string prefix_;
};

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

    FileTailer source(path, sourceChunkBytes);
    TailWindowScanner scanner(recordCount, cancelled);
    bool firstChunk = true;

    while (true) {
        if (scanner.cancellationRequestedNow()) {
            return scanner.finish();
        }

        const SourceChunk chunk = firstChunk ? source.pollChunk()
                                             : source.pollChunk(scanner.result().snapshot_end);
        if (!scanner.accept(chunk, firstChunk)) {
            return scanner.finish();
        }
        firstChunk = false;

        if (chunk.position >= scanner.result().snapshot_end) {
            break;
        }
        if (!chunk.more_available && chunk.bytes.empty()) {
            InitialLoadWindow result = scanner.finish();
            result.error = changedSourceError();
            return result;
        }
    }
    return scanner.finish();
}

} // namespace loglens
