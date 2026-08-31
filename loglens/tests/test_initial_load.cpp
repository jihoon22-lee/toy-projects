#include "check.hpp"

#include "loglens/initial_load.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> nextId{0};
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        for (unsigned int attempt = 0; attempt < 128; ++attempt) {
            const fs::path candidate =
                fs::temp_directory_path()
                / ("loglens_window_" + std::to_string(stamp) + "_"
                   + std::to_string(nextId.fetch_add(1, std::memory_order_relaxed)) + "_"
                   + std::to_string(attempt));
            std::error_code error;
            if (fs::create_directory(candidate, error)) {
                path_ = candidate;
                return;
            }
        }
        throw std::runtime_error("cannot create temporary directory");
    }

    ~TempDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void writeFile(const fs::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.flush();
    CHECK(output.good());
}

void testSelectsCompleteLogicalRecordsAcrossTinyChunks() {
    TempDirectory directory;
    const fs::path path = directory.path() / "tail.log";
    const std::string first = "root one\n  continuation\n";
    const std::string second = "root two\r\n";
    const std::string third = "root three\n\tmore\n";
    writeFile(path, first + second + third + "unfinished");

    const loglens::InitialLoadWindow window = loglens::locateTailWindow(path.string(), 2, 5);
    CHECK(window.ok());
    CHECK(window.identity.valid);
    CHECK_EQ(window.snapshot_end,
             static_cast<std::uint64_t>((first + second + third + "unfinished").size()));
    CHECK_EQ(window.complete_record_count, static_cast<std::size_t>(3));
    CHECK_EQ(window.offset, static_cast<std::uint64_t>(first.size()));
    CHECK_EQ(window.first_line_number, static_cast<std::size_t>(3));
}

void testEmptyFileProducesAnEmptySuccessfulWindow() {
    TempDirectory directory;
    const fs::path path = directory.path() / "empty.log";
    writeFile(path, "");

    const loglens::InitialLoadWindow window = loglens::locateTailWindow(path.string(), 1, 1);
    CHECK(window.ok());
    CHECK(!window.cancelled);
    CHECK(window.identity.valid);
    CHECK_EQ(window.snapshot_end, static_cast<std::uint64_t>(0));
    CHECK_EQ(window.offset, static_cast<std::uint64_t>(0));
    CHECK_EQ(window.first_line_number, static_cast<std::size_t>(1));
    CHECK_EQ(window.complete_record_count, static_cast<std::size_t>(0));
    CHECK_EQ(window.error.kind, loglens::SourceErrorKind::None);
    CHECK(window.error.message.empty());
}

void testPartialOnlyFileProducesNoCompleteRecords() {
    TempDirectory directory;
    const fs::path path = directory.path() / "partial_only.log";
    const std::string content = "  continuation-looking fragment\r";
    writeFile(path, content);

    const loglens::InitialLoadWindow window = loglens::locateTailWindow(path.string(), 3, 2);
    CHECK(window.ok());
    CHECK(window.identity.valid);
    CHECK_EQ(window.snapshot_end, static_cast<std::uint64_t>(content.size()));
    CHECK_EQ(window.offset, static_cast<std::uint64_t>(0));
    CHECK_EQ(window.first_line_number, static_cast<std::size_t>(1));
    CHECK_EQ(window.complete_record_count, static_cast<std::size_t>(0));
}

void testTailCountIsExactAcrossContinuationCrLfAndEveryChunkBoundary() {
    TempDirectory directory;
    const fs::path path = directory.path() / "exact_tail.log";
    const std::string content = "root-one\r\n"
                                "  one-detail\r\n"
                                "at one-frame\r\n"
                                "root-two\r\n"
                                "\t two-detail\r\n"
                                "root-three\r\n"
                                "unfinished-root";
    writeFile(path, content);

    const loglens::InitialLoadWindow tailOne =
        loglens::locateTailWindow(path.string(), 1, 1);
    CHECK(tailOne.ok());
    CHECK_EQ(tailOne.complete_record_count, static_cast<std::size_t>(3));
    CHECK_EQ(tailOne.offset,
             static_cast<std::uint64_t>(content.find("root-three")));
    CHECK_EQ(tailOne.first_line_number, static_cast<std::size_t>(6));

    const loglens::InitialLoadWindow tailTwo =
        loglens::locateTailWindow(path.string(), 2, 1);
    CHECK(tailTwo.ok());
    CHECK_EQ(tailTwo.offset, static_cast<std::uint64_t>(content.find("root-two")));
    CHECK_EQ(tailTwo.first_line_number, static_cast<std::size_t>(4));
    CHECK_EQ(tailTwo.complete_record_count, static_cast<std::size_t>(3));

    const loglens::InitialLoadWindow tailThree =
        loglens::locateTailWindow(path.string(), 3, 1);
    CHECK(tailThree.ok());
    CHECK_EQ(tailThree.offset, static_cast<std::uint64_t>(0));
    CHECK_EQ(tailThree.first_line_number, static_cast<std::size_t>(1));
    CHECK_EQ(tailThree.complete_record_count, static_cast<std::size_t>(3));
    CHECK_EQ(tailTwo.snapshot_end, static_cast<std::uint64_t>(content.size()));
    CHECK(tailTwo.identity == tailThree.identity);
}

void testLeadingContinuationIsARecordAndLargeWindowStartsAtZero() {
    TempDirectory directory;
    const fs::path path = directory.path() / "leading.log";
    writeFile(path, "  orphan\nroot\n  detail\n");

    const loglens::InitialLoadWindow window = loglens::locateTailWindow(path.string(), 10, 4);
    CHECK(window.ok());
    CHECK_EQ(window.complete_record_count, static_cast<std::size_t>(2));
    CHECK_EQ(window.offset, static_cast<std::uint64_t>(0));
    CHECK_EQ(window.first_line_number, static_cast<std::size_t>(1));
}

void testCancellationStopsBeforeOpeningTheSource() {
    const loglens::InitialLoadWindow window =
        loglens::locateTailWindow("does-not-need-to-exist.log", 1, 8, [] { return true; });
    CHECK(window.cancelled);
    CHECK(!window.ok());
    CHECK(window.error.kind == loglens::SourceErrorKind::None);
}

void testCancellationDuringScanIsReportedWithoutSourceError() {
    TempDirectory directory;
    const fs::path path = directory.path() / "cancel.log";
    std::string content;
    for (int index = 0; index < 32; ++index) {
        content += "root-" + std::to_string(index) + "\n";
    }
    writeFile(path, content);

    std::size_t callbackCalls = 0;
    const loglens::InitialLoadWindow window = loglens::locateTailWindow(
        path.string(), 1, 4, [&callbackCalls] { return ++callbackCalls >= 4; });
    CHECK(window.cancelled);
    CHECK(!window.ok());
    CHECK(callbackCalls >= static_cast<std::size_t>(4));
    CHECK_EQ(window.error.kind, loglens::SourceErrorKind::None);
}

void testMissingAndUnsupportedSourcesExposeDeterministicErrors() {
    TempDirectory directory;
    const fs::path missing = directory.path() / "missing.log";

    const loglens::InitialLoadWindow missingWindow =
        loglens::locateTailWindow(missing.string(), 1, 8);
    CHECK(!missingWindow.ok());
    CHECK(!missingWindow.cancelled);
    CHECK_EQ(missingWindow.error.kind, loglens::SourceErrorKind::Missing);
    CHECK(missingWindow.error.retryable);
    CHECK(!missingWindow.error.message.empty());

    const loglens::InitialLoadWindow directoryWindow =
        loglens::locateTailWindow(directory.path().string(), 1, 8);
    CHECK(!directoryWindow.ok());
    CHECK(!directoryWindow.cancelled);
    CHECK_EQ(directoryWindow.error.kind, loglens::SourceErrorKind::UnsupportedFileType);
    CHECK(!directoryWindow.error.retryable);
    CHECK(!directoryWindow.error.message.empty());
}

void testReplacementDuringScanIsRejected() {
    TempDirectory directory;
    const fs::path path = directory.path() / "changing.log";
    const fs::path replacement = directory.path() / "replacement.log";
    writeFile(path, "root-one\nroot-two\nroot-three\n");
    writeFile(replacement, "replacement\n");

    std::size_t callbackCalls = 0;
    const loglens::InitialLoadWindow window = loglens::locateTailWindow(
        path.string(), 2, 4, [&] {
            ++callbackCalls;
            if (callbackCalls == 3) {
                std::error_code error;
                fs::rename(replacement, path, error);
                CHECK(!error);
            }
            return false;
        });

    CHECK(!window.ok());
    CHECK(!window.cancelled);
    CHECK_EQ(window.error.kind, loglens::SourceErrorKind::ReadFailed);
    CHECK(window.error.retryable);
    CHECK(window.error.message.find("source changed") != std::string::npos);
}

void testRejectsZeroTailSize() {
    bool threw = false;
    try {
        static_cast<void>(loglens::locateTailWindow("unused.log", 0));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

} // namespace

int main() {
    testSelectsCompleteLogicalRecordsAcrossTinyChunks();
    testEmptyFileProducesAnEmptySuccessfulWindow();
    testPartialOnlyFileProducesNoCompleteRecords();
    testTailCountIsExactAcrossContinuationCrLfAndEveryChunkBoundary();
    testLeadingContinuationIsARecordAndLargeWindowStartsAtZero();
    testCancellationStopsBeforeOpeningTheSource();
    testCancellationDuringScanIsReportedWithoutSourceError();
    testMissingAndUnsupportedSourcesExposeDeterministicErrors();
    testReplacementDuringScanIsRejected();
    testRejectsZeroTailSize();
    return checkSummary();
}
