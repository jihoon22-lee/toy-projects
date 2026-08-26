#include "check.hpp"
#include "fake_source.hpp"

#include "loglens/ring_buffer.hpp"

using loglens::Level;
using loglens::RingBuffer;

namespace {

void testBasics() {
    RingBuffer buffer(3);
    CHECK_EQ(buffer.capacity(), static_cast<std::size_t>(3));
    CHECK_EQ(buffer.size(), static_cast<std::size_t>(0));
    CHECK(buffer.empty());
    CHECK_EQ(buffer.totalPushed(), static_cast<std::size_t>(0));
    // Out-of-range reads return an empty record rather than misbehaving.
    CHECK(buffer.at(0).message.empty());

    buffer.push(makeRecord(Level::Info, "a", "one"));
    buffer.push(makeRecord(Level::Warn, "b", "two"));
    CHECK_EQ(buffer.size(), static_cast<std::size_t>(2));
    CHECK(!buffer.empty());
    CHECK_EQ(buffer.at(0).message, std::string("one"));
    CHECK_EQ(buffer.at(1).message, std::string("two"));
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
    CHECK_EQ(buffer.at(0).message, std::string("three"));
    CHECK_EQ(buffer.at(1).message, std::string("four"));
    CHECK_EQ(buffer.at(2).message, std::string("five"));

    // Wrapping repeatedly keeps the invariant.
    for (int i = 0; i < 10; ++i) {
        buffer.push(makeRecord(Level::Info, "s", "x"));
    }
    CHECK_EQ(buffer.size(), static_cast<std::size_t>(3));
    CHECK_EQ(buffer.totalPushed(), static_cast<std::size_t>(15));
}

void testExactlyFullThenOneMore() {
    RingBuffer buffer(2);
    buffer.push(makeRecord(Level::Info, "s", "a"));
    buffer.push(makeRecord(Level::Info, "s", "b"));
    CHECK_EQ(buffer.size(), static_cast<std::size_t>(2));
    CHECK_EQ(buffer.at(0).message, std::string("a"));
    buffer.push(makeRecord(Level::Info, "s", "c"));
    CHECK_EQ(buffer.at(0).message, std::string("b"));
    CHECK_EQ(buffer.at(1).message, std::string("c"));
}

void testZeroCapacity() {
    RingBuffer buffer(0);
    buffer.push(makeRecord(Level::Info, "s", "dropped"));
    buffer.push(makeRecord(Level::Info, "s", "also dropped"));
    CHECK_EQ(buffer.size(), static_cast<std::size_t>(0));
    CHECK(buffer.empty());
    // Everything is still counted even though nothing is retained.
    CHECK_EQ(buffer.totalPushed(), static_cast<std::size_t>(2));
    CHECK(buffer.at(0).message.empty());
}

void testClear() {
    RingBuffer buffer(2);
    buffer.push(makeRecord(Level::Info, "s", "a"));
    buffer.push(makeRecord(Level::Info, "s", "b"));
    buffer.push(makeRecord(Level::Info, "s", "c"));
    buffer.clear();
    CHECK(buffer.empty());
    CHECK_EQ(buffer.totalPushed(), static_cast<std::size_t>(0));
    buffer.push(makeRecord(Level::Info, "s", "fresh"));
    CHECK_EQ(buffer.at(0).message, std::string("fresh"));
}

} // namespace

int main() {
    testBasics();
    testWrapAround();
    testExactlyFullThenOneMore();
    testZeroCapacity();
    testClear();
    return checkSummary();
}
