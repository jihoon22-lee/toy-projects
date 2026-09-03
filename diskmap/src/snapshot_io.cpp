#include "snapshot_internal.hpp"

#if defined(__linux__)
#include "trash_linux_internal.hpp"
#endif

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace diskmap {
namespace {

namespace fs = std::filesystem;

#if defined(__linux__)
int openSnapshotInput(int parent, const std::string& name) {
    int flags = O_RDONLY;
#ifdef O_NONBLOCK
    flags |= O_NONBLOCK;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int descriptor = ::openat(parent, name.c_str(), flags);
    if (descriptor < 0) {
        throw SnapshotError("cannot open snapshot file without following symlink: "
                            + std::generic_category().message(errno));
    }
    return descriptor;
}

std::size_t checkedInputSize(int descriptor,
                             const SnapshotLimits& limits,
                             struct stat& initial) {
    if (::fstat(descriptor, &initial) != 0 || !S_ISREG(initial.st_mode)) {
        throw SnapshotError("snapshot input is not a regular file");
    }
    if (initial.st_size < 0
        || static_cast<std::uint64_t>(initial.st_size)
               > limits.max_serialized_bytes) {
        throw SnapshotError("snapshot input exceeds configured bound");
    }
    return static_cast<std::size_t>(initial.st_size);
}

std::string readSnapshotDescriptor(int descriptor, std::size_t expected) {
    std::string output;
    output.reserve(expected);
    char buffer[64 * 1024];
    while (output.size() < expected) {
        const std::size_t request = std::min<std::size_t>(
            sizeof(buffer), expected - output.size());
        const ssize_t count = ::read(descriptor, buffer, request);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            throw SnapshotError("snapshot input changed or ended early");
        }
        output.append(buffer, static_cast<std::size_t>(count));
    }
    return output;
}

bool stableInputDescriptor(int descriptor, const struct stat& initial) {
    struct stat finalState{};
    return ::fstat(descriptor, &finalState) == 0
           && S_ISREG(finalState.st_mode) && finalState.st_dev == initial.st_dev
           && finalState.st_ino == initial.st_ino
           && finalState.st_size == initial.st_size
           && finalState.st_mtim.tv_sec == initial.st_mtim.tv_sec
           && finalState.st_mtim.tv_nsec == initial.st_mtim.tv_nsec;
}

std::string readRegularFile(const fs::path& path,
                            const SnapshotLimits& limits) {
    std::string error;
    detail::FileDescriptor parent =
        detail::openAbsoluteDirectory(path.parent_path(), error);
    if (!parent) {
        throw SnapshotError(error.empty()
                                ? "snapshot input parent is not a directory"
                                : error);
    }
    const int descriptor = openSnapshotInput(parent.get(), path.filename().native());
    bool descriptorOpen = true;
    try {
        struct stat initial{};
        const std::size_t expected = checkedInputSize(descriptor, limits, initial);
        const std::string output = readSnapshotDescriptor(descriptor, expected);
        if (!stableInputDescriptor(descriptor, initial)) {
            throw SnapshotError("snapshot input changed while being read");
        }
        descriptorOpen = false;
        if (::close(descriptor) != 0) {
            throw SnapshotError("cannot close snapshot input");
        }
        return output;
    } catch (...) {
        if (descriptorOpen) {
            ::close(descriptor);
        }
        throw;
    }
}
#else
std::string readRegularFile(const fs::path& path,
                            const SnapshotLimits& limits) {
    std::error_code statusError;
    const fs::file_status status = fs::symlink_status(path, statusError);
    if (statusError || fs::is_symlink(status) || !fs::is_regular_file(status)) {
        throw SnapshotError(
            "snapshot input is not a regular file without symlink following");
    }
    std::error_code sizeError;
    const std::uintmax_t size = fs::file_size(path, sizeError);
    if (sizeError || size > limits.max_serialized_bytes) {
        throw SnapshotError("snapshot input exceeds configured bound");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw SnapshotError("cannot open snapshot input");
    }
    std::string output;
    output.reserve(static_cast<std::size_t>(size));
    char buffer[64 * 1024];
    while (input) {
        input.read(buffer, sizeof(buffer));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            output.append(buffer, static_cast<std::size_t>(count));
        }
    }
    if (input.bad() || output.size() != size) {
        throw SnapshotError("snapshot input changed while being read");
    }
    return output;
}
#endif

} // namespace

Snapshot readSnapshotFile(const std::filesystem::path& input,
                          const SnapshotLimits& inputLimits) {
    const SnapshotLimits limits = detail::checkedSnapshotLimits(inputLimits);
    const fs::path path = detail::absoluteSnapshotFilePath(input);
    return parseSnapshot(readRegularFile(path, limits), limits);
}

} // namespace diskmap
