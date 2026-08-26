#include "check.hpp"
#include "fake_source.hpp"

#include "loglens/log_source.hpp"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

using loglens::FileTailer;
using loglens::LogSource;

namespace {

// Writes text to path, replacing any existing content.
void writeFile(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::trunc);
    out << text;
}

void appendFile(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::app);
    out << text;
}

std::string tempPath(const char* name) { return std::string("/tmp/loglens_") + name + ".log"; }

void testFakeSourceBatches() {
    FakeLogSource source;
    source.pushBatch({"one", "two"});
    source.pushBatch({"three"});

    std::vector<std::string> lines;
    std::string error;
    CHECK(source.poll(lines, error));
    CHECK_EQ(lines.size(), static_cast<std::size_t>(2));
    CHECK(error.empty());

    CHECK(source.poll(lines, error));
    CHECK_EQ(lines.size(), static_cast<std::size_t>(3));

    // Past the end: succeeds and adds nothing.
    CHECK(source.poll(lines, error));
    CHECK_EQ(lines.size(), static_cast<std::size_t>(3));
    CHECK_EQ(source.calls(), static_cast<std::size_t>(3));
}

void testFakeSourceFailure() {
    FakeLogSource source;
    source.pushBatch({"ok"});
    source.failOnCall(1, "disk went away");

    std::vector<std::string> lines;
    std::string error;
    CHECK(source.poll(lines, error));
    CHECK(!source.poll(lines, error));
    // The failure is reported, not swallowed.
    CHECK_EQ(error, std::string("disk went away"));
}

// Exercises the abstract base through a pointer so its virtual destructor runs.
void testPolymorphicDestruction() {
    std::unique_ptr<LogSource> source(new FakeLogSource());
    std::vector<std::string> lines;
    std::string error;
    CHECK(source->poll(lines, error));
}

void testTailerReadsAndResumes() {
    const std::string path = tempPath("tail");
    writeFile(path, "first\nsecond\n");

    FileTailer tailer(path);
    std::vector<std::string> lines;
    std::string error;
    CHECK(tailer.poll(lines, error));
    CHECK_EQ(lines.size(), static_cast<std::size_t>(2));
    CHECK(tailer.offset() > 0);

    // Nothing new: no additional lines.
    CHECK(tailer.poll(lines, error));
    CHECK_EQ(lines.size(), static_cast<std::size_t>(2));

    appendFile(path, "third\n");
    CHECK(tailer.poll(lines, error));
    CHECK_EQ(lines.size(), static_cast<std::size_t>(3));
    CHECK_EQ(tailer.restarts(), static_cast<std::size_t>(0));
    std::remove(path.c_str());
}

void testTailerDetectsTruncation() {
    const std::string path = tempPath("trunc");
    writeFile(path, "aaaa\nbbbb\ncccc\n");

    FileTailer tailer(path);
    std::vector<std::string> lines;
    std::string error;
    CHECK(tailer.poll(lines, error));
    CHECK_EQ(lines.size(), static_cast<std::size_t>(3));

    // Shrinking the file means the old offset is meaningless.
    writeFile(path, "new\n");
    lines.clear();
    CHECK(tailer.poll(lines, error));
    CHECK_EQ(lines.size(), static_cast<std::size_t>(1));
    CHECK_EQ(tailer.restarts(), static_cast<std::size_t>(1));
    std::remove(path.c_str());
}

void testTailerMissingFile() {
    FileTailer tailer("/tmp/loglens_definitely_absent.log");
    std::vector<std::string> lines;
    std::string error;
    CHECK(!tailer.poll(lines, error));
    CHECK(!error.empty());
    CHECK(error.find("cannot stat") != std::string::npos);
}

} // namespace

int main() {
    testFakeSourceBatches();
    testFakeSourceFailure();
    testPolymorphicDestruction();
    testTailerReadsAndResumes();
    testTailerDetectsTruncation();
    testTailerMissingFile();
    return checkSummary();
}
