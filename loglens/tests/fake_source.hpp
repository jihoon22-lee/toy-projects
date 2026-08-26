#pragma once

// Shared test doubles and builders. Keeping them here rather than inline in
// each test avoids the copy-paste that ici's dup engine flags as a clone.

#include <string>
#include <vector>

#include "loglens/log_record.hpp"
#include "loglens/log_source.hpp"

// A LogSource that replays canned batches, optionally failing on a given call.
class FakeLogSource : public loglens::LogSource {
public:
    void pushBatch(std::vector<std::string> lines) { batches_.push_back(std::move(lines)); }
    void failOnCall(std::size_t call, std::string message) {
        fail_call_ = call;
        fail_message_ = std::move(message);
    }
    std::size_t calls() const { return calls_; }

    bool poll(std::vector<std::string>& out, std::string& error) override {
        error.clear();
        const std::size_t call = calls_++;
        if (fail_call_ == call) {
            error = fail_message_;
            return false;
        }
        if (call >= batches_.size()) {
            return true;
        }
        for (const std::string& line : batches_[call]) {
            out.push_back(line);
        }
        return true;
    }

private:
    std::vector<std::vector<std::string>> batches_;
    std::size_t calls_ = 0;
    std::size_t fail_call_ = static_cast<std::size_t>(-1);
    std::string fail_message_;
};

inline loglens::LogRecord makeRecord(loglens::Level level, const std::string& source,
                                     const std::string& message,
                                     std::uint64_t timestamp_ms = 0) {
    loglens::LogRecord record;
    record.level = level;
    record.source = source;
    record.message = message;
    record.timestamp_ms = timestamp_ms;
    record.raw = message;
    return record;
}
