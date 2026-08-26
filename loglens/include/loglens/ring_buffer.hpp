#pragma once

#include <cstddef>
#include <vector>

#include "loglens/log_record.hpp"

namespace loglens {

// Fixed-capacity FIFO of records. Oldest entries are evicted once full, which
// is what keeps a long tail -f session from growing without bound.
class RingBuffer {
public:
    explicit RingBuffer(std::size_t capacity);

    void push(const LogRecord& record);
    void clear();

    std::size_t size() const;
    std::size_t capacity() const;
    bool empty() const;
    // Total ever pushed, including entries since evicted.
    std::size_t totalPushed() const;

    // Index 0 is the oldest retained record. Out-of-range returns a shared
    // empty record rather than throwing.
    const LogRecord& at(std::size_t index) const;

private:
    std::vector<LogRecord> slots_;
    std::size_t capacity_ = 0;
    std::size_t begin_ = 0;
    std::size_t size_ = 0;
    std::size_t total_ = 0;
};

} // namespace loglens
