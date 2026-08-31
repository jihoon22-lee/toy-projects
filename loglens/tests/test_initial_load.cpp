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
    testLeadingContinuationIsARecordAndLargeWindowStartsAtZero();
    testCancellationStopsBeforeOpeningTheSource();
    testRejectsZeroTailSize();
    return checkSummary();
}
