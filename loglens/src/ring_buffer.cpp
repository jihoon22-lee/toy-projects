#include "loglens/ring_buffer.hpp"

namespace loglens {

namespace {
const LogRecord& emptyRecord() {
    static const LogRecord record;
    return record;
}
} // namespace

RingBuffer::RingBuffer(std::size_t capacity) : capacity_(capacity) {
    slots_.resize(capacity);
}

void RingBuffer::push(const LogRecord& record) {
    ++total_;
    if (capacity_ == 0) {
        return;
    }
    const std::size_t slot = (begin_ + size_) % capacity_;
    slots_[slot] = record;
    if (size_ < capacity_) {
        ++size_;
        return;
    }
    begin_ = (begin_ + 1) % capacity_;
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

const LogRecord& RingBuffer::at(std::size_t index) const {
    if (index >= size_) {
        return emptyRecord();
    }
    return slots_[(begin_ + index) % capacity_];
}

} // namespace loglens
