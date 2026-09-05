#include "duplicates_internal.hpp"

#include "diskmap/fs_source.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace diskmap {
namespace {

namespace fs = std::filesystem;

std::string systemError(const std::string& action, int value = errno) {
    return action + ": " + std::generic_category().message(value);
}

class SystemDuplicateFileAccess final : public DuplicateFileAccess {
public:
    FsMetadata inspectNoFollow(const fs::path& path) const override {
        return source_.inspect(path, false);
    }

    bool readRange(const fs::path& path,
                   const FsMetadata& expected,
                   std::uint64_t offset,
                   std::size_t max_bytes,
                   std::string& bytes,
                   std::string& error) const override {
        bytes.clear();
        error.clear();
        if (max_bytes == 0) {
            return true;
        }
#if defined(_WIN32)
        return readRangePortable(path, expected, offset, max_bytes, bytes, error);
#else
        ReadRangeOutcome outcome = readRangeNoFollow(path, expected, offset, max_bytes);
        bytes = std::move(outcome.bytes);
        error = std::move(outcome.error);
        return error.empty();
#endif
    }

private:
#if defined(_WIN32)
    bool readRangePortable(const fs::path& path,
                           const FsMetadata& expected,
                           std::uint64_t offset,
                           std::size_t max_bytes,
                           std::string& bytes,
                           std::string& error) const {
        (void)expected;
        std::error_code statusError;
        const fs::file_status status = fs::symlink_status(path, statusError);
        if (statusError || fs::is_symlink(status) || !fs::is_regular_file(status)) {
            error = statusError ? "cannot inspect file without following a symlink: "
                                  + statusError.message()
                                : "duplicate candidate is not a regular file";
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            error = "cannot open duplicate candidate";
            return false;
        }
        if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
            error = "duplicate candidate offset exceeds platform limit";
            return false;
        }
        input.seekg(static_cast<std::streamoff>(offset));
        if (!input) {
            error = "cannot seek duplicate candidate";
            return false;
        }
        bytes.resize(max_bytes);
        input.read(&bytes[0], static_cast<std::streamsize>(max_bytes));
        const std::streamsize count = input.gcount();
        if (count < 0) {
            error = "cannot read duplicate candidate";
            bytes.clear();
            return false;
        }
        bytes.resize(static_cast<std::size_t>(count));
        if (input.bad()) {
            error = "cannot read duplicate candidate";
            bytes.clear();
            return false;
        }
        return true;
    }
#else
    // Bundles the read outcome in one typed value instead of a pair of
    // same-typed output references, which clang-tidy's
    // bugprone-easily-swappable-parameters flagged as swappable at the call
    // site. Success is signaled by an empty error, matching ScanResult's
    // fatal_error convention elsewhere in DiskMap.
    struct ReadRangeOutcome {
        std::string bytes;
        std::string error;
    };

    static std::int64_t modifiedNanoseconds(const struct stat& status) {
#if defined(__APPLE__)
        const std::int64_t seconds =
            static_cast<std::int64_t>(status.st_mtimespec.tv_sec);
        const std::int64_t nanoseconds =
            static_cast<std::int64_t>(status.st_mtimespec.tv_nsec);
#else
        const std::int64_t seconds =
            static_cast<std::int64_t>(status.st_mtim.tv_sec);
        const std::int64_t nanoseconds =
            static_cast<std::int64_t>(status.st_mtim.tv_nsec);
#endif
        constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000;
        if (seconds > std::numeric_limits<std::int64_t>::max()
                          / kNanosecondsPerSecond) {
            return std::numeric_limits<std::int64_t>::max();
        }
        if (seconds < std::numeric_limits<std::int64_t>::min()
                          / kNanosecondsPerSecond) {
            return std::numeric_limits<std::int64_t>::min();
        }
        const std::int64_t base = seconds * kNanosecondsPerSecond;
        if (nanoseconds > 0
            && base > std::numeric_limits<std::int64_t>::max() - nanoseconds) {
            return std::numeric_limits<std::int64_t>::max();
        }
        return base + nanoseconds;
    }

    static bool matchesExpected(const struct stat& opened,
                                const FsMetadata& expected) {
        return expected.complete && expected.identity.valid
               && expected.modified_time_known && S_ISREG(opened.st_mode)
               && static_cast<std::uint64_t>(opened.st_dev)
                      == expected.identity.device
               && static_cast<std::uint64_t>(opened.st_ino)
                      == expected.identity.file
               && opened.st_size >= 0
               && static_cast<std::uint64_t>(opened.st_size)
                      == expected.logical_size
               && modifiedNanoseconds(opened) == expected.modified_ns
               && (!expected.hard_link_count_known
                   || static_cast<std::uint64_t>(opened.st_nlink)
                          == expected.hard_link_count);
    }

    ReadRangeOutcome readRangeNoFollow(const fs::path& path,
                                       const FsMetadata& expected,
                                       std::uint64_t offset,
                                       std::size_t max_bytes) const {
        ReadRangeOutcome outcome;
        if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())
            || max_bytes > static_cast<std::size_t>(std::numeric_limits<ssize_t>::max())) {
            outcome.error = "duplicate candidate range exceeds platform limit";
            return outcome;
        }
        int flags = O_RDONLY;
#ifdef O_NONBLOCK
        // A reviewed regular file can be replaced by a FIFO before open().
        // Non-blocking open plus the descriptor type check below makes that
        // race fail promptly instead of hanging cancellation indefinitely.
        flags |= O_NONBLOCK;
#endif
#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif
        const int descriptor = ::open(path.c_str(), flags);
        if (descriptor < 0) {
            outcome.error = systemError("cannot open duplicate candidate without following symlink");
            return outcome;
        }
        struct stat opened{};
        if (::fstat(descriptor, &opened) != 0) {
            const int savedErrno = errno;
            ::close(descriptor);
            outcome.error = systemError("cannot validate opened duplicate candidate", savedErrno);
            return outcome;
        }
        if (!matchesExpected(opened, expected)) {
            ::close(descriptor);
            outcome.error = "opened duplicate candidate no longer matches retained evidence";
            return outcome;
        }
        std::vector<char> buffer(max_bytes);
        ssize_t count = 0;
        do {
            count = ::pread(descriptor, buffer.data(), max_bytes,
                            static_cast<off_t>(offset));
        } while (count < 0 && errno == EINTR);
        const int savedErrno = errno;
        struct stat finalState{};
        const bool stable = count >= 0 && ::fstat(descriptor, &finalState) == 0
                            && matchesExpected(finalState, expected);
        ::close(descriptor);
        if (count < 0) {
            outcome.error = systemError("cannot read duplicate candidate", savedErrno);
            return outcome;
        }
        if (!stable) {
            outcome.error = "duplicate candidate changed while its range was read";
            return outcome;
        }
        outcome.bytes.assign(buffer.data(), static_cast<std::size_t>(count));
        return outcome;
    }
#endif

    RealFsSource source_;
};

} // namespace

DuplicateFileAccess::~DuplicateFileAccess() = default;

namespace detail {

std::unique_ptr<DuplicateFileAccess> makeSystemDuplicateFileAccess() {
    return std::unique_ptr<DuplicateFileAccess>(new SystemDuplicateFileAccess());
}

} // namespace detail
} // namespace diskmap
