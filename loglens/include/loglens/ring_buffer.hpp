#pragma once

#include <cstddef>
#include <vector>

#include "loglens/log_record.hpp"

namespace loglens {

constexpr std::size_t kDefaultRecordCapacity = 32768;
constexpr std::size_t kMaxRecordCapacity = 1'000'000;

// Fixed-capacity FIFO of records. Oldest entries are evicted once full, which
// is what keeps a long tail -f session from growing without bound.
class RingBuffer {
public:
    struct PushResult {
        // Absolute logical ID assigned to the pushed record. IDs start at zero
        // and continue monotonically even after the ring starts evicting.
        std::size_t index = 0;
        // False only when the buffer has zero capacity; the record was then
        // counted but could not be retained.
        bool retained = false;
        // True when a full ring discarded its oldest retained record.
        bool evicted = false;
        // Valid only when evicted is true.
        std::size_t evicted_index = 0;
    };

    explicit RingBuffer(std::size_t capacity);

    // Throws std::overflow_error before the absolute ID space would wrap.
    PushResult push(const LogRecord& record);
    void clear();

    std::size_t size() const;
    std::size_t capacity() const;
    bool empty() const;
    // Total ever pushed, including entries since evicted.
    std::size_t totalPushed() const;
    // Absolute logical ID of the oldest retained record. For an empty ring it
    // is the next ID that would be assigned (totalPushed()).
    std::size_t firstIndex() const;
    // Number of records no longer retained by the bounded ring.
    std::size_t droppedCount() const;

    // Look up a record by its absolute logical ID. A null pointer means the ID
    // is either not retained (including an ID evicted by wraparound) or the
    // ring has zero capacity.
    bool contains(std::size_t absolute) const;
    const LogRecord* find(std::size_t absolute) const;
    LogRecord* find(std::size_t absolute);
    // Replaces a retained record without changing its logical ID or counters.
    // Returns false when absolute is not currently retained.
    bool replace(std::size_t absolute, const LogRecord& record);

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
