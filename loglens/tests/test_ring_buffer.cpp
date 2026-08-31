#include "check.hpp"
#include "fake_source.hpp"

#include <stdexcept>

#include "loglens/ring_buffer.hpp"

using loglens::Level;
using loglens::LogRecord;
using loglens::RingBuffer;

namespace {

void testBasics() {
    RingBuffer buffer(3);
    CHECK_EQ(buffer.capacity(), static_cast<std::size_t>(3));
    CHECK_EQ(buffer.size(), static_cast<std::size_t>(0));
    CHECK(buffer.empty());
    CHECK_EQ(buffer.totalPushed(), static_cast<std::size_t>(0));
    CHECK_EQ(buffer.firstIndex(), static_cast<std::size_t>(0));
    CHECK_EQ(buffer.droppedCount(), static_cast<std::size_t>(0));
    CHECK(!buffer.contains(0));
    CHECK(buffer.find(0) == nullptr);
    // Out-of-range reads return an empty record rather than misbehaving.
    CHECK(buffer.at(0).message.empty());

    const RingBuffer::PushResult first = buffer.push(makeRecord(Level::Info, "a", "one"));
    CHECK_EQ(first.index, static_cast<std::size_t>(0));
    CHECK(first.retained);
    CHECK(!first.evicted);
    const RingBuffer::PushResult second = buffer.push(makeRecord(Level::Warn, "b", "two"));
    CHECK_EQ(second.index, static_cast<std::size_t>(1));
    CHECK(second.retained);
    CHECK(!second.evicted);
    CHECK_EQ(buffer.size(), static_cast<std::size_t>(2));
    CHECK(!buffer.empty());
    CHECK_EQ(buffer.firstIndex(), static_cast<std::size_t>(0));
    CHECK_EQ(buffer.droppedCount(), static_cast<std::size_t>(0));
    CHECK(buffer.contains(0));
    CHECK(buffer.contains(1));
    CHECK(!buffer.contains(2));
    CHECK_EQ(buffer.at(0).message, std::string("one"));
    CHECK_EQ(buffer.at(1).message, std::string("two"));
    CHECK_EQ(buffer.find(0)->message, std::string("one"));
    CHECK_EQ(buffer.find(1)->message, std::string("two"));
    CHECK(buffer.at(2).message.empty());
}

void testWrapAround() {
    RingBuffer buffer(3);
    const char* messages[] = {"one", "two", "three", "four", "five"};
    for (const char* message : messages) {
        buffer.push(makeRecord(Level::Info, "s", message));
    }
    // Capacity holds; the oldest two were evicted.
    CHECK_EQ(buffer.size(), static_cast<std::size_t>(3));
    CHECK_EQ(buffer.totalPushed(), static_cast<std::size_t>(5));
    CHECK_EQ(buffer.firstIndex(), static_cast<std::size_t>(2));
    CHECK_EQ(buffer.droppedCount(), static_cast<std::size_t>(2));
    CHECK_EQ(buffer.at(0).message, std::string("three"));
    CHECK_EQ(buffer.at(1).message, std::string("four"));
    CHECK_EQ(buffer.at(2).message, std::string("five"));
    CHECK(!buffer.contains(0));
    CHECK(!buffer.contains(1));
    CHECK(buffer.contains(2));
    CHECK(buffer.contains(4));
    CHECK(buffer.find(1) == nullptr);
    CHECK_EQ(buffer.find(2)->message, std::string("three"));
    CHECK_EQ(buffer.find(4)->message, std::string("five"));

    // Wrapping repeatedly keeps the invariant.
    for (int i = 0; i < 10; ++i) {
        buffer.push(makeRecord(Level::Info, "s", "x"));
    }
    CHECK_EQ(buffer.size(), static_cast<std::size_t>(3));
    CHECK_EQ(buffer.totalPushed(), static_cast<std::size_t>(15));
    CHECK_EQ(buffer.firstIndex(), static_cast<std::size_t>(12));
    CHECK_EQ(buffer.droppedCount(), static_cast<std::size_t>(12));
    CHECK(!buffer.contains(11));
    CHECK(buffer.contains(12));
    CHECK(buffer.contains(14));
}

void testExactlyFullThenOneMore() {
    RingBuffer buffer(2);
    const RingBuffer::PushResult first = buffer.push(makeRecord(Level::Info, "s", "a"));
    const RingBuffer::PushResult second = buffer.push(makeRecord(Level::Info, "s", "b"));
    CHECK(first.retained);
    CHECK(second.retained);
    CHECK(!first.evicted);
    CHECK(!second.evicted);
    CHECK_EQ(buffer.size(), static_cast<std::size_t>(2));
    CHECK_EQ(buffer.at(0).message, std::string("a"));
    const RingBuffer::PushResult third = buffer.push(makeRecord(Level::Info, "s", "c"));
    CHECK_EQ(third.index, static_cast<std::size_t>(2));
    CHECK(third.retained);
    CHECK(third.evicted);
    CHECK_EQ(third.evicted_index, static_cast<std::size_t>(0));
    CHECK_EQ(buffer.at(0).message, std::string("b"));
    CHECK_EQ(buffer.at(1).message, std::string("c"));
    CHECK_EQ(buffer.firstIndex(), static_cast<std::size_t>(1));
    CHECK_EQ(buffer.droppedCount(), static_cast<std::size_t>(1));
    CHECK(!buffer.contains(0));
    CHECK(buffer.contains(1));
    CHECK(buffer.contains(2));

    const RingBuffer::PushResult fourth = buffer.push(makeRecord(Level::Info, "s", "d"));
    CHECK_EQ(fourth.index, static_cast<std::size_t>(3));
    CHECK(fourth.retained);
    CHECK(fourth.evicted);
    CHECK_EQ(fourth.evicted_index, static_cast<std::size_t>(1));
    CHECK_EQ(buffer.firstIndex(), static_cast<std::size_t>(2));
    CHECK_EQ(buffer.at(0).message, std::string("c"));
    CHECK_EQ(buffer.at(1).message, std::string("d"));
}

void testZeroCapacity() {
    RingBuffer buffer(0);
    const RingBuffer::PushResult first = buffer.push(makeRecord(Level::Info, "s", "dropped"));
    const RingBuffer::PushResult second =
        buffer.push(makeRecord(Level::Info, "s", "also dropped"));
    CHECK_EQ(first.index, static_cast<std::size_t>(0));
    CHECK_EQ(second.index, static_cast<std::size_t>(1));
    CHECK(!first.retained);
    CHECK(!second.retained);
    CHECK(!first.evicted);
    CHECK(!second.evicted);
    CHECK_EQ(buffer.size(), static_cast<std::size_t>(0));
    CHECK(buffer.empty());
    // Everything is still counted even though nothing is retained.
    CHECK_EQ(buffer.totalPushed(), static_cast<std::size_t>(2));
    CHECK_EQ(buffer.firstIndex(), static_cast<std::size_t>(2));
    CHECK_EQ(buffer.droppedCount(), static_cast<std::size_t>(2));
    CHECK(!buffer.contains(0));
    CHECK(buffer.find(0) == nullptr);
    CHECK(buffer.at(0).message.empty());
}

void testCapacityLimit() {
    bool rejected = false;
    try {
        RingBuffer buffer(loglens::kMaxRecordCapacity + 1);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    CHECK(rejected);
}

void testFindAndReplace() {
    RingBuffer buffer(2);
    buffer.push(makeRecord(Level::Info, "s", "first"));
    buffer.push(makeRecord(Level::Info, "s", "second"));

    CHECK(buffer.replace(1, makeRecord(Level::Error, "updated", "replacement")));
    const LogRecord* replaced = buffer.find(1);
    CHECK(replaced != nullptr);
    CHECK(replaced->level == Level::Error);
    CHECK_EQ(replaced->source, std::string("updated"));
    CHECK_EQ(replaced->message, std::string("replacement"));
    CHECK_EQ(buffer.totalPushed(), static_cast<std::size_t>(2));
    CHECK_EQ(buffer.droppedCount(), static_cast<std::size_t>(0));

    buffer.push(makeRecord(Level::Warn, "s", "third"));
    CHECK(!buffer.replace(0, makeRecord(Level::Fatal, "s", "stale")));
    CHECK(buffer.find(0) == nullptr);
    CHECK(buffer.find(1) != nullptr);
    CHECK_EQ(buffer.find(1)->message, std::string("replacement"));
    LogRecord* mutableRecord = buffer.find(1);
    CHECK(mutableRecord != nullptr);
    if (mutableRecord != nullptr) {
        mutableRecord->message = "mutated";
    }
    CHECK_EQ(buffer.at(0).message, std::string("mutated"));
    CHECK_EQ(buffer.at(1).message, std::string("third"));
    CHECK(buffer.replace(2, makeRecord(Level::Fatal, "s", "latest")));
    CHECK_EQ(buffer.find(2)->message, std::string("latest"));
}

void testClear() {
    RingBuffer buffer(2);
    buffer.push(makeRecord(Level::Info, "s", "a"));
    buffer.push(makeRecord(Level::Info, "s", "b"));
    buffer.push(makeRecord(Level::Info, "s", "c"));
    buffer.clear();
    CHECK(buffer.empty());
    CHECK_EQ(buffer.totalPushed(), static_cast<std::size_t>(0));
    CHECK_EQ(buffer.firstIndex(), static_cast<std::size_t>(0));
    CHECK_EQ(buffer.droppedCount(), static_cast<std::size_t>(0));
    CHECK(buffer.find(0) == nullptr);
    buffer.push(makeRecord(Level::Info, "s", "fresh"));
    CHECK_EQ(buffer.at(0).message, std::string("fresh"));
}

} // namespace

int main() {
    testBasics();
    testWrapAround();
    testExactlyFullThenOneMore();
    testZeroCapacity();
    testCapacityLimit();
    testFindAndReplace();
    testClear();
    return checkSummary();
}
