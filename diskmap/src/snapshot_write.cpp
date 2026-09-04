#include "snapshot_internal.hpp"

#if defined(__linux__)
#include "trash_linux_internal.hpp"
#endif

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace diskmap {
namespace {

namespace fs = std::filesystem;

struct FileState {
    bool exists = false;
    FsKind kind = FsKind::Other;
    FileIdentity identity;
    std::uint64_t size = 0;
    std::int64_t modified_ns = 0;
    bool modified_known = false;
    std::int64_t changed_ns = 0;
    bool changed_known = false;
};

#if defined(__linux__)
std::int64_t timestampNanoseconds(std::int64_t seconds, std::int64_t nanos) {
    constexpr std::int64_t kNanosPerSecond = 1'000'000'000;
    if (seconds > std::numeric_limits<std::int64_t>::max() / kNanosPerSecond
        || seconds < std::numeric_limits<std::int64_t>::min() / kNanosPerSecond) {
        return seconds > 0 ? std::numeric_limits<std::int64_t>::max()
                           : std::numeric_limits<std::int64_t>::min();
    }
    const std::int64_t base = seconds * kNanosPerSecond;
    if (nanos > 0 && base > std::numeric_limits<std::int64_t>::max() - nanos) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return base + nanos;
}

std::int64_t modifiedNanoseconds(const struct stat& status) {
    return timestampNanoseconds(static_cast<std::int64_t>(status.st_mtim.tv_sec),
                                static_cast<std::int64_t>(status.st_mtim.tv_nsec));
}

bool inspectFileAt(int parent,
                   const std::string& name,
                   FileState& state,
                   std::string& error) {
    struct stat status{};
    if (::fstatat(parent, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) {
            state = FileState{};
            return true;
        }
        error = "cannot inspect snapshot destination: "
                + std::generic_category().message(errno);
        return false;
    }
    state.exists = true;
    state.kind = detail::kindFromMode(status.st_mode);
    state.identity = FileIdentity{static_cast<std::uint64_t>(status.st_dev),
                                  static_cast<std::uint64_t>(status.st_ino), true};
    state.size = status.st_size < 0 ? 0 : static_cast<std::uint64_t>(status.st_size);
    state.modified_ns = modifiedNanoseconds(status);
    state.modified_known = true;
    state.changed_ns = timestampNanoseconds(
        static_cast<std::int64_t>(status.st_ctim.tv_sec),
        static_cast<std::int64_t>(status.st_ctim.tv_nsec));
    state.changed_known = true;
    return true;
}
#else
bool inspectFile(const fs::path& path, FileState& state, std::string& error) {
    std::error_code statusError;
    const fs::file_status status = fs::symlink_status(path, statusError);
    if (statusError) {
        if (statusError == std::make_error_code(std::errc::no_such_file_or_directory)) {
            state = FileState{};
            return true;
        }
        error = "cannot inspect snapshot destination: " + statusError.message();
        return false;
    }
    state.exists = true;
    state.kind = fs::is_regular_file(status)
                     ? FsKind::RegularFile
                     : (fs::is_directory(status) ? FsKind::Directory
                                                  : (fs::is_symlink(status) ? FsKind::Symlink
                                                                            : FsKind::Other));
    std::error_code sizeError;
    if (state.kind == FsKind::RegularFile) {
        const std::uintmax_t size = fs::file_size(path, sizeError);
        if (sizeError) {
            error = "cannot inspect snapshot size: " + sizeError.message();
            return false;
        }
        state.size = static_cast<std::uint64_t>(size);
    }
    std::error_code timeError;
    const fs::file_time_type modified = fs::last_write_time(path, timeError);
    if (!timeError) {
        state.modified_ns = static_cast<std::int64_t>(modified.time_since_epoch().count());
        state.modified_known = true;
    }
    return true;
}
#endif

bool sameState(const FileState& before, const FileState& after) {
    if (before.exists != after.exists) {
        return false;
    }
    if (!before.exists) {
        return true;
    }
    if (before.kind != after.kind || before.size != after.size) {
        return false;
    }
    if (before.identity.valid && after.identity.valid
        && before.identity != after.identity) {
        return false;
    }
    return !before.modified_known || !after.modified_known
           || (before.modified_ns == after.modified_ns
               && (!before.changed_known || !after.changed_known
                   || before.changed_ns == after.changed_ns));
}

bool sameRenamedState(const FileState& before, const FileState& after) {
    if (before.exists != after.exists) {
        return false;
    }
    if (!before.exists) {
        return true;
    }
    return before.kind == after.kind && before.size == after.size
           && (!before.identity.valid || !after.identity.valid
               || before.identity == after.identity)
           && (!before.modified_known || !after.modified_known
               || before.modified_ns == after.modified_ns);
}

std::string temporaryName() {
    static std::atomic<std::uint64_t> sequence{0};
    const auto ticks = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::uint64_t serial = sequence.fetch_add(1, std::memory_order_relaxed);
    std::ostringstream output;
    output << ".diskmap-snapshot-" << ticks << '-' << serial << ".tmp";
    return output.str();
}

#if defined(__linux__)
int openTemporaryAt(int parent, const std::string& name) {
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int descriptor = ::openat(parent, name.c_str(), flags, 0600);
    if (descriptor < 0) {
        throw SnapshotError("cannot create snapshot temporary file: "
                            + std::generic_category().message(errno));
    }
    return descriptor;
}

void writeAndSync(int descriptor, const std::string& content) {
    std::size_t offset = 0;
    while (offset < content.size()) {
        const ssize_t count = ::write(descriptor, content.data() + offset,
                                      content.size() - offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            throw SnapshotError("cannot write snapshot temporary file");
        }
        offset += static_cast<std::size_t>(count);
    }
    if (::fsync(descriptor) != 0) {
        throw SnapshotError("cannot sync snapshot temporary file");
    }
}

void closeDescriptor(int descriptor) {
    if (::close(descriptor) != 0) {
        throw SnapshotError("cannot close snapshot temporary file");
    }
}

FileState checkedRegularStateAt(int parent,
                                const std::string& name,
                                const char* changedMessage) {
    FileState state;
    std::string error;
    if (!inspectFileAt(parent, name, state, error)) {
        throw SnapshotError(error);
    }
    if (state.exists && state.kind != FsKind::RegularFile) {
        throw SnapshotError(changedMessage);
    }
    return state;
}

void installTemporaryAt(int parent,
                        const detail::FileDescriptor& parentDirectory,
                        const fs::path& parentPath,
                        const std::string& temporary,
                        const std::string& target,
                        const FileState& expected,
                        const FileState& installed,
                        bool& preserveTemporary) {
    if (!expected.exists) {
        if (::linkat(parent, temporary.c_str(), parent, target.c_str(), 0) != 0) {
            throw SnapshotError("cannot atomically install snapshot: "
                                + std::generic_category().message(errno));
        }
        FileState targetState;
        std::string error;
        if (!inspectFileAt(parent, target, targetState, error)
            || !sameRenamedState(installed, targetState)) {
            preserveTemporary = true;
            throw SnapshotError(
                "snapshot destination changed during atomic installation; "
                "the temporary snapshot was preserved as "
                + temporary);
        }
        if (!detail::directoryPathMatches(parentPath, parentDirectory, error)) {
            preserveTemporary = true;
            throw SnapshotError(
                "snapshot destination directory changed during atomic installation; "
                "the temporary snapshot was preserved as "
                + temporary);
        }
        if (::unlinkat(parent, temporary.c_str(), 0) != 0) {
            preserveTemporary = true;
            throw SnapshotError("snapshot installed but its temporary link could not be removed");
        }
        return;
    }
    if (::syscall(SYS_renameat2, parent, temporary.c_str(), parent,
                  target.c_str(), RENAME_EXCHANGE) != 0) {
        throw SnapshotError("cannot atomically exchange snapshot destination: "
                            + std::generic_category().message(errno));
    }

    FileState displaced;
    FileState targetState;
    std::string error;
    if (!inspectFileAt(parent, temporary, displaced, error)
        || !sameRenamedState(expected, displaced)
        || !inspectFileAt(parent, target, targetState, error)
        || !sameRenamedState(installed, targetState)
        || !detail::directoryPathMatches(parentPath, parentDirectory, error)) {
        preserveTemporary = true;
        throw SnapshotError(
            "snapshot destination raced during atomic exchange; displaced "
            "entries were preserved for recovery as "
            + temporary);
    }
    if (::unlinkat(parent, temporary.c_str(), 0) != 0) {
        preserveTemporary = true;
        throw SnapshotError("snapshot installed but the prior destination was preserved as "
                            + temporary);
    }
}

void lockSnapshotDirectory(const detail::FileDescriptor& parent) {
    if (::flock(parent.get(), LOCK_EX | LOCK_NB) == 0) {
        return;
    }
    const int value = errno;
    if (value == EWOULDBLOCK) {
        throw SnapshotError(
            "another snapshot operation is using the destination directory");
    }
    throw SnapshotError("cannot lock snapshot destination directory: "
                        + std::generic_category().message(value));
}

void writeSnapshotLinux(const std::string& json,
                        const fs::path& target) {
    std::string error;
    detail::FileDescriptor parent =
        detail::openAbsoluteDirectory(target.parent_path(), error);
    if (!parent) {
        throw SnapshotError(error.empty() ? "snapshot destination parent is not a directory"
                                          : error);
    }
    lockSnapshotDirectory(parent);
    const std::string targetName = target.filename().native();
    const FileState before = checkedRegularStateAt(
        parent.get(), targetName,
        "snapshot destination must be a regular file, not a symlink or special file");

    const std::string temporary = temporaryName();
    bool temporaryExists = false;
    bool preserveTemporary = false;
    int descriptor = -1;
    try {
        descriptor = openTemporaryAt(parent.get(), temporary);
        temporaryExists = true;
        writeAndSync(descriptor, json);
        const int closingDescriptor = descriptor;
        descriptor = -1;
        closeDescriptor(closingDescriptor);

        FileState after;
        if (!inspectFileAt(parent.get(), targetName, after, error)
            || !sameState(before, after)) {
            throw SnapshotError("snapshot destination changed during atomic write");
        }
        const FileState installed = checkedRegularStateAt(
            parent.get(), temporary,
            "snapshot temporary file changed before installation");
        if (!installed.exists) {
            throw SnapshotError("snapshot temporary file disappeared before installation");
        }
        if (!detail::directoryPathMatches(target.parent_path(), parent, error)) {
            throw SnapshotError("snapshot destination directory changed during atomic write");
        }
        installTemporaryAt(parent.get(), parent, target.parent_path(), temporary,
                           targetName, before, installed, preserveTemporary);
        temporaryExists = false;
        if (::fsync(parent.get()) != 0
            || !detail::directoryPathMatches(target.parent_path(), parent, error)) {
            if (!error.empty()) {
                throw SnapshotError(error);
            }
            throw SnapshotError("cannot sync snapshot directory");
        }
    } catch (...) {
        if (descriptor >= 0) {
            ::close(descriptor);
        }
        if (temporaryExists && !preserveTemporary) {
            ::unlinkat(parent.get(), temporary.c_str(), 0);
        }
        throw;
    }
}
#else
void writeSnapshotPortable(const std::string& json, const fs::path& target) {
    const fs::path parent = target.parent_path();
    FileState parentState;
    std::string error;
    if (!inspectFile(parent, parentState, error)) {
        throw SnapshotError(error);
    }
    if (!parentState.exists || parentState.kind != FsKind::Directory) {
        throw SnapshotError("snapshot destination parent is not a directory");
    }
    FileState before;
    if (!inspectFile(target, before, error)) {
        throw SnapshotError(error);
    }
    if (before.exists && before.kind != FsKind::RegularFile) {
        throw SnapshotError(
            "snapshot destination must be a regular file, not a symlink or special file");
    }

    const fs::path temporary = parent / temporaryName();
    bool temporaryExists = false;
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::out);
        if (!output) {
            throw SnapshotError("cannot create snapshot temporary file");
        }
        temporaryExists = true;
        output.write(json.data(), static_cast<std::streamsize>(json.size()));
        output.close();
        if (!output) {
            throw SnapshotError("cannot write snapshot temporary file");
        }
        std::error_code modeError;
        fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace, modeError);
        if (modeError) {
            throw SnapshotError("cannot restrict snapshot temporary file");
        }
        FileState after;
        if (!inspectFile(target, after, error) || !sameState(before, after)) {
            throw SnapshotError("snapshot destination changed during atomic write");
        }
        std::error_code renameError;
        fs::rename(temporary, target, renameError);
        if (renameError) {
            throw SnapshotError("cannot atomically install snapshot: "
                                + renameError.message());
        }
        temporaryExists = false;
    } catch (...) {
        if (temporaryExists) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
        }
        throw;
    }
}
#endif

} // namespace

void writeSnapshotAtomically(const Snapshot& snapshot,
                             const std::filesystem::path& input,
                             const SnapshotLimits& inputLimits) {
    const SnapshotLimits limits = detail::checkedSnapshotLimits(inputLimits);
    const std::string json = serializeSnapshot(snapshot, limits);
    const fs::path target = detail::absoluteSnapshotFilePath(input);
#if defined(__linux__)
    writeSnapshotLinux(json, target);
#else
    writeSnapshotPortable(json, target);
#endif
}

} // namespace diskmap
