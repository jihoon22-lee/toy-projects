#include "check.hpp"
#include "fake_source.hpp"

#include "loglens/log_source.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

using loglens::FileIdentity;
using loglens::FileTailer;
using loglens::LogSource;
using loglens::SourceChange;
using loglens::SourceChunk;
using loglens::SourceErrorKind;

namespace {

namespace fs = std::filesystem;

// Every test gets its own directory.  The directory is created atomically so
// even parallel test processes cannot accidentally share a target pathname.
class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<std::uint64_t> next_id{0};
        const std::uint64_t id = next_id.fetch_add(1, std::memory_order_relaxed);
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();

        for (unsigned int attempt = 0; attempt < 128; ++attempt) {
            const fs::path candidate = fs::temp_directory_path() /
                                       ("loglens_identity_" + std::to_string(stamp) + "_" +
                                        std::to_string(id) + "_" + std::to_string(attempt));
            std::error_code ec;
            if (fs::create_directory(candidate, ec)) {
                path_ = candidate;
                return;
            }
            if (ec != std::errc::file_exists) {
                throw std::runtime_error("cannot create temporary directory: " + ec.message());
            }
        }
        throw std::runtime_error("cannot create a unique temporary directory");
    }

    ~TempDirectory() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

bool writeFile(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        CHECK(false);
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) {
        CHECK(false);
        return false;
    }
    return true;
}

bool appendFile(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) {
        CHECK(false);
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) {
        CHECK(false);
        return false;
    }
    return true;
}

// The staging file is created in the same directory so POSIX rename is an
// atomic replacement of target, with a distinct device/inode identity.
bool atomicReplace(const fs::path& target, const fs::path& staging,
                   const std::string& replacement) {
    if (!writeFile(staging, replacement)) {
        return false;
    }
    std::error_code ec;
    fs::rename(staging, target, ec);
    CHECK(!ec);
    return !ec;
}

void checkSuccessfulChunk(const SourceChunk& chunk) {
    CHECK(chunk.ok());
    CHECK_EQ(chunk.error.kind, SourceErrorKind::None);
    CHECK(chunk.error.message.empty());
    CHECK(!chunk.error.retryable);
}

void testFileIdentityEqualityAndPublicEnums() {
    const FileIdentity identity{17, 23, true};
    const FileIdentity same{17, 23, true};
    const FileIdentity differentDevice{18, 23, true};
    const FileIdentity differentFile{17, 24, true};
    const FileIdentity invalid{17, 23, false};

    CHECK(identity == same);
    CHECK(!(identity != same));
    CHECK(identity != differentDevice);
    CHECK(identity != differentFile);
    CHECK(identity != invalid);

    // Keep all public enum spellings exercised by this contract in the test
    // translation unit, even where a particular OS error is hard to induce.
    const SourceChange changes[] = {SourceChange::None, SourceChange::Truncated,
                                    SourceChange::Replaced};
    const SourceErrorKind errors[] = {
        SourceErrorKind::None, SourceErrorKind::Missing,
        SourceErrorKind::PermissionDenied, SourceErrorKind::OpenFailed,
        SourceErrorKind::StatFailed, SourceErrorKind::ReadFailed,
        SourceErrorKind::UnsupportedFileType,
    };
    CHECK_EQ(sizeof(changes) / sizeof(changes[0]), static_cast<std::size_t>(3));
    CHECK_EQ(sizeof(errors) / sizeof(errors[0]), static_cast<std::size_t>(7));

    SourceChunk defaultChunk;
    CHECK(defaultChunk.ok());
}

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

void testTailerInitialIdentityAndAppend() {
    TempDirectory directory;
    const fs::path path = directory.path() / "append.log";
    const std::string initial = "first\n";
    const std::string appended = "second\n";
    CHECK(writeFile(path, initial));

    FileTailer tailer(path.string());
    const SourceChunk first = tailer.pollChunk();
    checkSuccessfulChunk(first);
    CHECK_EQ(first.bytes, initial);
    CHECK(first.identity.valid);
    CHECK_EQ(first.position, static_cast<std::uint64_t>(initial.size()));
    CHECK_EQ(first.generation, static_cast<std::uint64_t>(0));
    CHECK_EQ(first.change, SourceChange::None);
    CHECK(!first.generation_changed);
    CHECK_EQ(tailer.offset(), first.position);

    CHECK(appendFile(path, appended));
    const SourceChunk second = tailer.pollChunk();
    checkSuccessfulChunk(second);
    CHECK_EQ(second.bytes, appended);
    CHECK(second.identity.valid);
    CHECK(second.identity == first.identity);
    CHECK_EQ(second.generation, first.generation);
    CHECK_EQ(second.position,
             static_cast<std::uint64_t>(initial.size() + appended.size()));
    CHECK_EQ(tailer.offset(), second.position);
    CHECK_EQ(second.change, SourceChange::None);
    CHECK(!second.generation_changed);

    const SourceChunk idle = tailer.pollChunk();
    checkSuccessfulChunk(idle);
    CHECK(idle.bytes.empty());
    CHECK(idle.identity == first.identity);
    CHECK_EQ(idle.position, second.position);
    CHECK_EQ(idle.generation, second.generation);
    CHECK_EQ(idle.change, SourceChange::None);
    CHECK(!idle.generation_changed);
}

void testTailerLineAdapterStillReadsAndResumes() {
    TempDirectory directory;
    const fs::path path = directory.path() / "line_adapter.log";
    CHECK(writeFile(path, "first\nsecond\n"));

    FileTailer tailer(path.string());
    std::vector<std::string> lines;
    std::string error;
    CHECK(tailer.poll(lines, error));
    CHECK(error.empty());
    CHECK_EQ(lines.size(), static_cast<std::size_t>(2));
    CHECK(tailer.offset() > 0);

    CHECK(tailer.poll(lines, error));
    CHECK(error.empty());
    CHECK_EQ(lines.size(), static_cast<std::size_t>(2));

    CHECK(appendFile(path, "third\n"));
    CHECK(tailer.poll(lines, error));
    CHECK(error.empty());
    CHECK_EQ(lines.size(), static_cast<std::size_t>(3));
    CHECK_EQ(tailer.restarts(), static_cast<std::size_t>(0));
}

void testTailerBoolChunkAdapterPreservesPartialBytes() {
    TempDirectory directory;
    const fs::path path = directory.path() / "partial.log";
    CHECK(writeFile(path, "part"));

    FileTailer tailer(path.string());
    SourceChunk chunk;
    std::string error;
    CHECK(tailer.pollChunk(chunk, error));
    CHECK(error.empty());
    checkSuccessfulChunk(chunk);
    CHECK_EQ(chunk.bytes, std::string("part"));
    CHECK_EQ(chunk.position, static_cast<std::uint64_t>(4));
    CHECK(!chunk.generation_changed);

    // The tailer advances only through bytes it returned. The assembler can
    // therefore join this suffix with the partial bytes from the first poll.
    CHECK(appendFile(path, "ial\n"));
    CHECK(tailer.pollChunk(chunk, error));
    CHECK(error.empty());
    checkSuccessfulChunk(chunk);
    CHECK_EQ(chunk.bytes, std::string("ial\n"));
    CHECK_EQ(chunk.position, static_cast<std::uint64_t>(8));
    CHECK(!chunk.generation_changed);
    CHECK_EQ(tailer.restarts(), static_cast<std::size_t>(0));
}

void testTailerBoundsEachChunkAndReportsBacklog() {
    TempDirectory directory;
    const fs::path path = directory.path() / "bounded.log";
    CHECK(writeFile(path, "first\nsecond\n"));

    FileTailer tailer(path.string(), 5);
    const SourceChunk first = tailer.pollChunk();
    checkSuccessfulChunk(first);
    CHECK_EQ(first.bytes, std::string("first"));
    CHECK(first.more_available);
    CHECK_EQ(first.position, static_cast<std::uint64_t>(5));

    const SourceChunk second = tailer.pollChunk();
    checkSuccessfulChunk(second);
    CHECK_EQ(second.bytes, std::string("\nseco"));
    CHECK(second.more_available);
    CHECK_EQ(second.position, static_cast<std::uint64_t>(10));

    const SourceChunk third = tailer.pollChunk();
    checkSuccessfulChunk(third);
    CHECK_EQ(third.bytes, std::string("nd\n"));
    CHECK(!third.more_available);
    CHECK_EQ(third.position, static_cast<std::uint64_t>(13));
}

void testTailerHonoursAnEarlierSnapshotBoundary() {
    TempDirectory directory;
    const fs::path path = directory.path() / "snapshot.log";
    CHECK(writeFile(path, "first"));

    FileTailer tailer(path.string(), 3);
    const SourceChunk first = tailer.pollChunk();
    checkSuccessfulChunk(first);
    CHECK_EQ(first.bytes, std::string("fir"));
    CHECK_EQ(first.snapshot_end, static_cast<std::uint64_t>(5));
    CHECK(first.more_available);

    CHECK(appendFile(path, "-later"));
    const SourceChunk second = tailer.pollChunk(first.snapshot_end);
    checkSuccessfulChunk(second);
    CHECK_EQ(second.bytes, std::string("st"));
    CHECK_EQ(second.position, first.snapshot_end);
    CHECK_EQ(second.snapshot_end, static_cast<std::uint64_t>(11));
    CHECK(!second.more_available);

    const SourceChunk appended = tailer.pollChunk();
    checkSuccessfulChunk(appended);
    CHECK_EQ(appended.bytes, std::string("-la"));
    CHECK(appended.more_available);
}

void testTailerLineAdapterJoinsChunkAndCrLfBoundaries() {
    TempDirectory directory;
    const fs::path path = directory.path() / "line_boundaries.log";
    CHECK(writeFile(path, "first\r\nsecond-longer-than-a-chunk\n"));

    FileTailer tailer(path.string(), 6);
    std::vector<std::string> lines;
    std::string error;
    CHECK(tailer.poll(lines, error));
    CHECK(error.empty());
    CHECK_EQ(lines.size(), static_cast<std::size_t>(2));
    CHECK_EQ(lines[0], std::string("first"));
    CHECK_EQ(lines[1], std::string("second-longer-than-a-chunk"));
    CHECK_EQ(tailer.offset(), static_cast<std::uint64_t>(fs::file_size(path)));
}

void testTailerRejectsUnsafeChunkSizes() {
    bool rejectedZero = false;
    bool rejectedHuge = false;
    try {
        FileTailer tailer("unused", 0);
    } catch (const std::invalid_argument&) {
        rejectedZero = true;
    }
    try {
        FileTailer tailer("unused", loglens::kMaxSourceChunkBytes + 1);
    } catch (const std::invalid_argument&) {
        rejectedHuge = true;
    }
    CHECK(rejectedZero);
    CHECK(rejectedHuge);
}

void testTailerDetectsInPlaceTruncation() {
    TempDirectory directory;
    const fs::path path = directory.path() / "truncation.log";
    const std::string oldContent = "aaaa\nbbbb\ncccc\n";
    const std::string replacement = "new\n";
    CHECK(writeFile(path, oldContent));

    FileTailer tailer(path.string());
    const SourceChunk before = tailer.pollChunk();
    checkSuccessfulChunk(before);
    CHECK_EQ(before.bytes, oldContent);

    // Truncate through the existing pathname; POSIX preserves the inode.
    CHECK(writeFile(path, replacement));
    const SourceChunk after = tailer.pollChunk();
    checkSuccessfulChunk(after);
    CHECK_EQ(after.bytes, replacement);
    CHECK(after.identity.valid);
    CHECK(after.identity == before.identity);
    CHECK_EQ(after.change, SourceChange::Truncated);
    CHECK(after.generation_changed);
    CHECK_EQ(after.generation, before.generation + 1);
    CHECK_EQ(after.position, static_cast<std::uint64_t>(replacement.size()));
    CHECK_EQ(tailer.offset(), after.position);
    CHECK_EQ(tailer.generation(), after.generation);
    CHECK_EQ(tailer.restarts(), static_cast<std::size_t>(1));
}

void checkAtomicReplacement(const TempDirectory& directory, const std::string& label,
                            const std::string& oldContent, const std::string& newContent) {
    const fs::path target = directory.path() / (label + ".log");
    const fs::path staging = directory.path() / (label + ".staging");
    CHECK(writeFile(target, oldContent));

    FileTailer tailer(target.string());
    const SourceChunk before = tailer.pollChunk();
    checkSuccessfulChunk(before);
    CHECK_EQ(before.bytes, oldContent);
    CHECK(before.identity.valid);

    CHECK(atomicReplace(target, staging, newContent));
    const SourceChunk after = tailer.pollChunk();
    checkSuccessfulChunk(after);
    CHECK_EQ(after.bytes, newContent);
    CHECK(after.identity.valid);
    CHECK(after.identity != before.identity);
    CHECK_EQ(after.change, SourceChange::Replaced);
    CHECK(after.generation_changed);
    CHECK_EQ(after.generation, before.generation + 1);
    CHECK_EQ(after.position, static_cast<std::uint64_t>(newContent.size()));
    CHECK_EQ(tailer.offset(), after.position);
    CHECK_EQ(tailer.generation(), after.generation);
    CHECK_EQ(tailer.restarts(), static_cast<std::size_t>(1));
}

void testTailerDetectsAtomicRenameAtEverySize() {
    TempDirectory directory;
    checkAtomicReplacement(directory, "rename_smaller", "original-content\n", "new\n");
    checkAtomicReplacement(directory, "rename_equal", "old-data\n", "new-data\n");
    checkAtomicReplacement(directory, "rename_larger", "old\n", "new-data-is-larger\n");
}

void testTailerMissingIsRetryableAndRecreateIsReplacement() {
    TempDirectory directory;
    const fs::path target = directory.path() / "missing_then_recreated.log";
    const fs::path staging = directory.path() / "missing_then_recreated.staging";
    const std::string oldContent = "before unlink\n";
    const std::string newContent = "after recreate\n";
    CHECK(writeFile(target, oldContent));

    FileTailer tailer(target.string());
    const SourceChunk before = tailer.pollChunk();
    checkSuccessfulChunk(before);
    CHECK_EQ(before.bytes, oldContent);
    const std::uint64_t oldPosition = tailer.offset();
    const std::uint64_t oldGeneration = tailer.generation();
    const std::size_t oldRestarts = tailer.restarts();

    std::error_code ec;
    fs::remove(target, ec);
    CHECK(!ec);

    const SourceChunk missing = tailer.pollChunk();
    CHECK(!missing.ok());
    CHECK_EQ(missing.error.kind, SourceErrorKind::Missing);
    CHECK(missing.error.retryable);
    CHECK(missing.bytes.empty());
    CHECK_EQ(missing.position, oldPosition);
    CHECK_EQ(missing.generation, oldGeneration);
    CHECK_EQ(missing.change, SourceChange::None);
    CHECK(!missing.generation_changed);
    CHECK_EQ(tailer.offset(), oldPosition);
    CHECK_EQ(tailer.generation(), oldGeneration);
    CHECK_EQ(tailer.restarts(), oldRestarts);

    // Create the replacement only after unlinking. Some filesystems may reuse
    // the just-freed inode immediately; the observed missing interval must
    // still force a new generation independently of identity reuse.
    CHECK(writeFile(staging, newContent));
    fs::rename(staging, target, ec);
    CHECK(!ec);
    const SourceChunk recreated = tailer.pollChunk();
    checkSuccessfulChunk(recreated);
    CHECK_EQ(recreated.bytes, newContent);
    CHECK(recreated.identity.valid);
    CHECK_EQ(recreated.change, SourceChange::Replaced);
    CHECK(recreated.generation_changed);
    CHECK_EQ(recreated.generation, oldGeneration + 1);
    CHECK_EQ(recreated.position, static_cast<std::uint64_t>(newContent.size()));
    CHECK_EQ(tailer.offset(), recreated.position);
    CHECK_EQ(tailer.restarts(), oldRestarts + 1);
}

void testTailerRejectsDirectoryAsUnsupportedFileType() {
    TempDirectory directory;
    const fs::path input = directory.path() / "not-a-regular-file";
    std::error_code ec;
    const bool created = fs::create_directory(input, ec);
    CHECK(created);
    CHECK(!ec);
    if (!created || ec) {
        return;
    }

    FileTailer tailer(input.string());
    const SourceChunk chunk = tailer.pollChunk();
    CHECK(!chunk.ok());
    CHECK_EQ(chunk.error.kind, SourceErrorKind::UnsupportedFileType);
    CHECK(!chunk.error.retryable);
    CHECK(chunk.bytes.empty());
    CHECK(!chunk.identity.valid);
    CHECK_EQ(chunk.position, static_cast<std::uint64_t>(0));
    CHECK_EQ(chunk.generation, static_cast<std::uint64_t>(0));
    CHECK_EQ(chunk.change, SourceChange::None);
    CHECK(!chunk.generation_changed);
    CHECK_EQ(tailer.offset(), static_cast<std::uint64_t>(0));
    CHECK_EQ(tailer.generation(), static_cast<std::uint64_t>(0));
}

void testTailerAdapterErrorStringRemainsCompatible() {
    TempDirectory directory;
    const fs::path missing = directory.path() / "does-not-exist.log";
    FileTailer tailer(missing.string());

    std::vector<std::string> lines;
    std::string error;
    CHECK(!tailer.poll(lines, error));
    CHECK(!error.empty());
    CHECK(error.find("cannot stat") != std::string::npos);

    SourceChunk chunk;
    error.clear();
    CHECK(!tailer.pollChunk(chunk, error));
    CHECK_EQ(chunk.error.kind, SourceErrorKind::Missing);
    CHECK(error.find("cannot stat") != std::string::npos);
    CHECK(!chunk.ok());
}

} // namespace

int main() {
    testFileIdentityEqualityAndPublicEnums();
    testFakeSourceBatches();
    testFakeSourceFailure();
    testPolymorphicDestruction();
    testTailerInitialIdentityAndAppend();
    testTailerLineAdapterStillReadsAndResumes();
    testTailerBoolChunkAdapterPreservesPartialBytes();
    testTailerBoundsEachChunkAndReportsBacklog();
    testTailerHonoursAnEarlierSnapshotBoundary();
    testTailerLineAdapterJoinsChunkAndCrLfBoundaries();
    testTailerRejectsUnsafeChunkSizes();
    testTailerDetectsInPlaceTruncation();
    testTailerDetectsAtomicRenameAtEverySize();
    testTailerMissingIsRetryableAndRecreateIsReplacement();
    testTailerRejectsDirectoryAsUnsupportedFileType();
    testTailerAdapterErrorStringRemainsCompatible();
    return checkSummary();
}
