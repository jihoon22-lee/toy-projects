#include "trash_linux_internal.hpp"

#if defined(__linux__)

#include <cerrno>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace diskmap {
namespace detail {

namespace {

enum class ChildDirectoryStatus {
    Opened,
    Missing,
    Error,
};

int childDirectoryFlags() {
    int flags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    return flags;
}

ChildDirectoryStatus openDirectoryChild(const FileDescriptor& current,
                                        const std::string& name,
                                        bool create,
                                        FileDescriptor& result,
                                        std::string& error) {
    result = FileDescriptor(
        ::openat(current.get(), name.c_str(), childDirectoryFlags()));
    if (!result && errno == ENOENT && create) {
        if (::mkdirat(current.get(), name.c_str(), 0700) != 0 && errno != EEXIST) {
            error = errnoMessage("cannot create directory component '" + name + "'");
            return ChildDirectoryStatus::Error;
        }
        result = FileDescriptor(
            ::openat(current.get(), name.c_str(), childDirectoryFlags()));
    }
    if (result) {
        return ChildDirectoryStatus::Opened;
    }
    if (errno == ENOENT) {
        return ChildDirectoryStatus::Missing;
    }
    error = errnoMessage("cannot open directory component '" + name + "'");
    return ChildDirectoryStatus::Error;
}

bool traverseAbsoluteDirectory(const fs::path& input,
                               bool create,
                               bool allowMissing,
                               FileDescriptor& result,
                               bool& complete,
                               std::string& error) {
    const fs::path path = normalizedAbsolute(input);
    if (!path.is_absolute()) {
        error = "directory path is not absolute";
        return false;
    }
    int flags = O_RDONLY | O_DIRECTORY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    FileDescriptor current(::open("/", flags));
    if (!current) {
        error = errnoMessage("cannot open filesystem root");
        return false;
    }
    complete = true;
    for (const fs::path& component : path.relative_path()) {
        const std::string name = component.native();
        if (name.empty() || name == ".") {
            continue;
        }
        if (name == "..") {
            error = "parent traversal is not allowed";
            return false;
        }
        FileDescriptor next;
        const ChildDirectoryStatus status =
            openDirectoryChild(current, name, create, next, error);
        if (status == ChildDirectoryStatus::Missing && allowMissing) {
            complete = false;
            result = std::move(current);
            return true;
        }
        if (status != ChildDirectoryStatus::Opened) {
            if (error.empty()) {
                error = "directory component '" + name + "' does not exist";
            }
            return false;
        }
        current = std::move(next);
    }
    result = std::move(current);
    return true;
}

} // namespace

FileDescriptor::FileDescriptor(int value) : value_(value) {}

FileDescriptor::~FileDescriptor() {
    if (value_ >= 0) {
        ::close(value_);
    }
}

FileDescriptor::FileDescriptor(FileDescriptor&& other) noexcept : value_(other.value_) {
    other.value_ = -1;
}

FileDescriptor& FileDescriptor::operator=(FileDescriptor&& other) noexcept {
    if (this != &other) {
        if (value_ >= 0) {
            ::close(value_);
        }
        value_ = other.value_;
        other.value_ = -1;
    }
    return *this;
}

int FileDescriptor::get() const { return value_; }

FileDescriptor::operator bool() const { return value_ >= 0; }

FileDescriptor openAbsoluteDirectory(const fs::path& input, std::string& error) {
    FileDescriptor result;
    bool complete = false;
    if (!traverseAbsoluteDirectory(input, false, false, result, complete, error)) {
        return FileDescriptor();
    }
    return result;
}

FileDescriptor openOrCreateAbsoluteDirectory(const fs::path& input,
                                             std::string& error) {
    FileDescriptor result;
    bool complete = false;
    if (!traverseAbsoluteDirectory(input, true, false, result, complete, error)) {
        return FileDescriptor();
    }
    return result;
}

bool inspectAbsoluteDirectoryPath(const fs::path& input,
                                  FileDescriptor& closest,
                                  bool& complete,
                                  std::string& error) {
    return traverseAbsoluteDirectory(input, false, true, closest, complete, error);
}

bool directoryWritable(const FileDescriptor& directory, std::string& error) {
    struct stat status{};
    if (::fstat(directory.get(), &status) != 0) {
        error = errnoMessage("cannot inspect Trash creation directory");
        return false;
    }
    if (!S_ISDIR(status.st_mode)) {
        error = "Trash creation path is not a directory";
        return false;
    }
    if (::faccessat(directory.get(), ".", W_OK | X_OK, 0) != 0) {
        error = errnoMessage("Trash location is not writable");
        return false;
    }
    return true;
}

bool directoryOwnedByEffectiveUser(const FileDescriptor& directory,
                                   std::string& error) {
    struct stat status{};
    if (::fstat(directory.get(), &status) != 0) {
        error = errnoMessage("cannot inspect Trash directory ownership");
        return false;
    }
    if (!S_ISDIR(status.st_mode) || status.st_uid != ::geteuid()) {
        error = "Trash directory is not owned by the current user";
        return false;
    }
    return true;
}

bool directoryPathMatches(const fs::path& path,
                          const FileDescriptor& directory,
                          std::string& error) {
    FileDescriptor current = openAbsoluteDirectory(path, error);
    if (!current) {
        return false;
    }
    struct stat expected{};
    struct stat actual{};
    if (::fstat(directory.get(), &expected) != 0
        || ::fstat(current.get(), &actual) != 0) {
        error = errnoMessage("cannot revalidate directory identity");
        return false;
    }
    if (expected.st_dev != actual.st_dev || expected.st_ino != actual.st_ino) {
        error = "directory path changed during operation";
        return false;
    }
    return true;
}

bool trashDirectoriesAnchored(const TrashDirectories& directories,
                              std::string& error) {
    return directoryPathMatches(directories.root, directories.root_directory,
                                error)
           && directoryPathMatches(directories.root / "files",
                                   directories.files, error)
           && directoryPathMatches(directories.root / "info",
                                   directories.info, error);
}

FsKind kindFromMode(mode_t mode) {
    if (S_ISREG(mode)) {
        return FsKind::RegularFile;
    }
    if (S_ISDIR(mode)) {
        return FsKind::Directory;
    }
    if (S_ISLNK(mode)) {
        return FsKind::Symlink;
    }
    return FsKind::Other;
}

std::uint64_t allocatedBytes(const struct stat& status) {
    if (status.st_blocks < 0) {
        return 0;
    }
    constexpr std::uint64_t kBlockBytes = 512;
    const auto blocks = static_cast<std::uint64_t>(status.st_blocks);
    if (blocks > std::numeric_limits<std::uint64_t>::max() / kBlockBytes) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return blocks * kBlockBytes;
}

TrashStatus validateStat(const CleanupTarget& target,
                         const struct stat& status,
                         std::string& message) {
    const FileIdentity identity{static_cast<std::uint64_t>(status.st_dev),
                                static_cast<std::uint64_t>(status.st_ino), true};
    if (!target.identity.valid || identity != target.identity) {
        message = "filesystem identity changed after cleanup review";
        return TrashStatus::RevalidationFailed;
    }
    if (kindFromMode(status.st_mode) != target.kind) {
        message = "filesystem entry type changed after cleanup review";
        return TrashStatus::RevalidationFailed;
    }
    const std::uint64_t logical =
        status.st_size < 0 ? 0 : static_cast<std::uint64_t>(status.st_size);
    if (logical != target.logical_size
        || (target.allocated_size_known
            && allocatedBytes(status) != target.allocated_size)
        || (target.hard_link_count_known
            && static_cast<std::uint64_t>(status.st_nlink) != target.hard_link_count)) {
        message = "filesystem size or hard-link evidence changed after cleanup review";
        return TrashStatus::RevalidationFailed;
    }
    return TrashStatus::Ready;
}

bool sameDevice(const FileDescriptor& directory,
                std::uint64_t expected,
                std::string& error) {
    struct stat status{};
    if (::fstat(directory.get(), &status) != 0) {
        error = errnoMessage("cannot inspect trash directory");
        return false;
    }
    if (static_cast<std::uint64_t>(status.st_dev) != expected) {
        error = "source and home Trash are on different filesystems";
        return false;
    }
    return true;
}

bool secureOwnedDirectory(const FileDescriptor& directory, std::string& error) {
    struct stat status{};
    if (::fstat(directory.get(), &status) != 0) {
        error = errnoMessage("cannot inspect Trash directory");
        return false;
    }
    if (!S_ISDIR(status.st_mode) || status.st_uid != ::geteuid()) {
        error = "Trash directory is not an owned directory";
        return false;
    }
    if (::fchmod(directory.get(), 0700) != 0) {
        error = errnoMessage("cannot restrict Trash directory permissions");
        return false;
    }
    return true;
}

int renameNoReplace(int sourceDirectory,
                    const char* source,
                    int targetDirectory,
                    const char* target) {
    return static_cast<int>(::syscall(SYS_renameat2, sourceDirectory, source,
                                      targetDirectory, target, RENAME_NOREPLACE));
}

bool writeAll(int descriptor, const std::string& content, std::string& error) {
    std::size_t written = 0;
    while (written < content.size()) {
        const ssize_t result = ::write(descriptor, content.data() + written,
                                       content.size() - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            error = errnoMessage("cannot write trash metadata");
            return false;
        }
        written += static_cast<std::size_t>(result);
    }
    if (::fsync(descriptor) != 0) {
        error = errnoMessage("cannot sync trash metadata");
        return false;
    }
    return true;
}

bool readBounded(int descriptor, std::string& content, std::string& error) {
    struct stat status{};
    if (::fstat(descriptor, &status) != 0) {
        error = errnoMessage("cannot inspect restore metadata");
        return false;
    }
    if (!S_ISREG(status.st_mode) || status.st_size < 0
        || static_cast<std::uint64_t>(status.st_size) > kMaxInfoBytes) {
        error = "restore metadata is not a bounded regular file";
        return false;
    }
    content.clear();
    content.reserve(static_cast<std::size_t>(status.st_size));
    char buffer[4096];
    while (content.size() < kMaxInfoBytes) {
        const std::size_t remaining = kMaxInfoBytes - content.size();
        const std::size_t request = std::min<std::size_t>(sizeof(buffer), remaining);
        const ssize_t count = ::read(descriptor, buffer, request);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            error = errnoMessage("cannot read restore metadata");
            return false;
        }
        if (count == 0) {
            return true;
        }
        content.append(buffer, static_cast<std::size_t>(count));
    }
    char extra = '\0';
    const ssize_t count = ::read(descriptor, &extra, 1);
    if (count == 0) {
        return true;
    }
    error = count < 0 ? errnoMessage("cannot read restore metadata")
                      : "restore metadata exceeds the size bound";
    return false;
}

namespace {

bool preflightTrashPath(const fs::path& path, std::string& error) {
    FileDescriptor closest;
    bool complete = false;
    return inspectAbsoluteDirectoryPath(path, closest, complete, error)
           && directoryWritable(closest, error);
}

FileDescriptor openConfiguredTrashDirectory(const fs::path& path,
                                             bool create,
                                             std::string& error) {
    return create ? openOrCreateAbsoluteDirectory(path, error)
                  : openAbsoluteDirectory(path, error);
}

} // namespace

bool openTrashDirectories(const TrashOptions& options,
                          bool create,
                          TrashDirectories& directories,
                          std::string& error) {
    const fs::path dataHome = configuredDataHome(options, error);
    if (dataHome.empty()) {
        return false;
    }
    directories.root = dataHome / "Trash";
    if (create
        && (!preflightTrashPath(directories.root / "files", error)
            || !preflightTrashPath(directories.root / "info", error))) {
        return false;
    }
    directories.root_directory = openConfiguredTrashDirectory(
        directories.root, create, error);
    if (!directories.root_directory
        || !secureOwnedDirectory(directories.root_directory, error)) {
        return false;
    }
    directories.files = openConfiguredTrashDirectory(
        directories.root / "files", create, error);
    if (!directories.files) {
        return false;
    }
    directories.info = openConfiguredTrashDirectory(
        directories.root / "info", create, error);
    return directories.files && directories.info
           && secureOwnedDirectory(directories.files, error)
           && secureOwnedDirectory(directories.info, error)
           && trashDirectoriesAnchored(directories, error);
}

} // namespace detail
} // namespace diskmap

#endif
