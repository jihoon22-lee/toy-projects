#include "loglens/ring_buffer.hpp"

#include <stdexcept>

namespace loglens {

namespace {
const LogRecord& emptyRecord() {
    static const LogRecord record;
    return record;
}
} // namespace

RingBuffer::RingBuffer(std::size_t capacity) : capacity_(capacity) {
    if (capacity > kMaxRecordCapacity) {
        throw std::invalid_argument("record capacity exceeds maximum");
    }
    slots_.resize(capacity);
}

RingBuffer::PushResult RingBuffer::push(const LogRecord& record) {
    PushResult result;
    result.index = total_;

    if (capacity_ == 0) {
        ++total_;
        return result;
    }

    if (size_ == capacity_) {
        result.evicted = true;
        result.evicted_index = firstIndex();
    }

    ++total_;
    const std::size_t slot = (begin_ + size_) % capacity_;
    slots_[slot] = record;
    if (size_ < capacity_) {
        ++size_;
        result.retained = true;
        return result;
    }
    begin_ = (begin_ + 1) % capacity_;
    result.retained = true;
    return result;
}

void RingBuffer::clear() {
    begin_ = 0;
    size_ = 0;
    total_ = 0;
}

std::size_t RingBuffer::size() const { return size_; }
std::size_t RingBuffer::capacity() const { return capacity_; }
bool RingBuffer::empty() const { return size_ == 0; }
std::size_t RingBuffer::totalPushed() const { return total_; }

std::size_t RingBuffer::firstIndex() const { return total_ - size_; }

std::size_t RingBuffer::droppedCount() const { return total_ - size_; }

bool RingBuffer::contains(std::size_t absolute) const {
    return absolute >= firstIndex() && absolute < total_;
}

const LogRecord* RingBuffer::find(std::size_t absolute) const {
    if (!contains(absolute)) {
        return nullptr;
    }
    return &slots_[(begin_ + (absolute - firstIndex())) % capacity_];
}

LogRecord* RingBuffer::find(std::size_t absolute) {
    if (!contains(absolute)) {
        return nullptr;
    }
    return &slots_[(begin_ + (absolute - firstIndex())) % capacity_];
}

bool RingBuffer::replace(std::size_t absolute, const LogRecord& record) {
    LogRecord* current = find(absolute);
    if (current == nullptr) {
        return false;
    }
    *current = record;
    return true;
}

const LogRecord& RingBuffer::at(std::size_t index) const {
    if (index >= size_) {
        return emptyRecord();
    }
    return slots_[(begin_ + index) % capacity_];
}

} // namespace loglens
