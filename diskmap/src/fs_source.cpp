#include "diskmap/fs_source.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace diskmap {

namespace {

namespace fs = std::filesystem;

#ifdef _WIN32
FsKind kindFromStatus(const fs::file_status& status) {
    if (fs::is_regular_file(status)) {
        return FsKind::RegularFile;
    }
    if (fs::is_directory(status)) {
        return FsKind::Directory;
    }
    if (fs::is_symlink(status)) {
        return FsKind::Symlink;
    }
    return FsKind::Other;
}
#endif

#ifndef _WIN32

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

std::int64_t modifiedNanoseconds(const struct stat& status) {
#if defined(__APPLE__)
    const std::int64_t seconds = static_cast<std::int64_t>(status.st_mtimespec.tv_sec);
    const std::int64_t nanoseconds = static_cast<std::int64_t>(status.st_mtimespec.tv_nsec);
#else
    const std::int64_t seconds = static_cast<std::int64_t>(status.st_mtim.tv_sec);
    const std::int64_t nanoseconds = static_cast<std::int64_t>(status.st_mtim.tv_nsec);
#endif
    constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000;
    if (seconds > std::numeric_limits<std::int64_t>::max() / kNanosecondsPerSecond) {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (seconds < std::numeric_limits<std::int64_t>::min() / kNanosecondsPerSecond) {
        return std::numeric_limits<std::int64_t>::min();
    }
    const std::int64_t base = seconds * kNanosecondsPerSecond;
    if (nanoseconds > 0 && base > std::numeric_limits<std::int64_t>::max() - nanoseconds) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return base + nanoseconds;
}

std::uint64_t allocatedBytes(const struct stat& status) {
    if (status.st_blocks < 0) {
        return 0;
    }
    constexpr std::uint64_t kBytesPerBlock = 512;
    const std::uint64_t blocks = static_cast<std::uint64_t>(status.st_blocks);
    if (blocks > std::numeric_limits<std::uint64_t>::max() / kBytesPerBlock) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return blocks * kBytesPerBlock;
}

FsMetadata metadataFromStat(const struct stat& status) {
    FsMetadata metadata;
    metadata.kind = kindFromMode(status.st_mode);
    metadata.identity.device = static_cast<std::uint64_t>(status.st_dev);
    metadata.identity.file = static_cast<std::uint64_t>(status.st_ino);
    metadata.identity.valid = true;
    metadata.logical_size =
        status.st_size < 0 ? 0 : static_cast<std::uint64_t>(status.st_size);
    metadata.allocated_size = allocatedBytes(status);
    metadata.allocated_size_known = status.st_blocks >= 0;
    metadata.hard_link_count = static_cast<std::uint64_t>(status.st_nlink);
    metadata.hard_link_count_known = true;
    metadata.permissions = static_cast<std::uint32_t>(status.st_mode & 07777U);
    metadata.permissions_known = true;
    metadata.owner = static_cast<std::uint64_t>(status.st_uid);
    metadata.group = static_cast<std::uint64_t>(status.st_gid);
    metadata.ownership_known = true;
    metadata.modified_ns = modifiedNanoseconds(status);
    metadata.modified_time_known = true;
    metadata.complete = true;
    return metadata;
}

FsMetadata readMetadata(const fs::path& path, bool follow) {
    struct stat status {};
    const int result = follow ? ::stat(path.c_str(), &status) : ::lstat(path.c_str(), &status);
    if (result == 0) {
        return metadataFromStat(status);
    }
    const int value = errno;
    FsMetadata metadata;
    metadata.error = std::string(follow ? "cannot stat '" : "cannot lstat '") + path.string() +
                     "': " + std::generic_category().message(value);
    return metadata;
}

#else

FsMetadata readMetadata(const fs::path& path, bool follow) {
    std::error_code statusError;
    const fs::file_status status =
        follow ? fs::status(path, statusError) : fs::symlink_status(path, statusError);
    FsMetadata metadata;
    if (statusError) {
        metadata.error = std::string("cannot read metadata '") + path.string() +
                         "': " + statusError.message();
        return metadata;
    }
    metadata.kind = kindFromStatus(status);
    if (metadata.kind == FsKind::RegularFile) {
        std::error_code sizeError;
        const std::uintmax_t size = fs::file_size(path, sizeError);
        if (sizeError) {
            metadata.error = std::string("cannot read size '") + path.string() +
                             "': " + sizeError.message();
            return metadata;
        }
        metadata.logical_size = static_cast<std::uint64_t>(size);
    }
    std::error_code timeError;
    const fs::file_time_type modified = fs::last_write_time(path, timeError);
    if (!timeError) {
        metadata.modified_ns = static_cast<std::int64_t>(modified.time_since_epoch().count());
        metadata.modified_time_known = true;
    }
    metadata.complete = true;
    return metadata;
}

#endif

fs::path displayPath(const fs::path& path) {
    std::error_code absoluteError;
    const fs::path absolute = fs::absolute(path, absoluteError);
    return (absoluteError ? path : absolute).lexically_normal();
}

DirEntry makeDirEntry(const fs::directory_entry& entry) {
    DirEntry result;
    result.name = entry.path().filename().string();
    result.path = displayPath(entry.path());
    result.metadata = readMetadata(entry.path(), false);
    result.is_symlink = result.metadata.complete && result.metadata.kind == FsKind::Symlink;

    if (result.is_symlink) {
        result.target_metadata = readMetadata(entry.path(), true);
        result.has_target_metadata = result.target_metadata.complete;
    }

    const FsMetadata& effective =
        result.is_symlink && result.has_target_metadata ? result.target_metadata : result.metadata;
    result.is_dir = effective.complete && effective.kind == FsKind::Directory;
    if (effective.complete && effective.kind == FsKind::RegularFile) {
        result.size = effective.logical_size;
    }
    return result;
}

} // namespace

FsSource::~FsSource() = default;

std::vector<DirEntry> RealFsSource::list(const std::string& path, std::string& error) const {
    error.clear();
    std::vector<DirEntry> entries;

    std::error_code openError;
    fs::directory_iterator iterator(path, fs::directory_options::none, openError);
    if (openError) {
        error = "cannot open directory '" + path + "': " + openError.message();
        return entries;
    }

    try {
        const fs::directory_iterator end;
        while (iterator != end) {
            entries.push_back(makeDirEntry(*iterator));
            std::error_code stepError;
            iterator.increment(stepError);
            if (stepError) {
                error = "error walking directory '" + path + "': " + stepError.message();
                break;
            }
        }
    } catch (const std::exception& exception) {
        error = "exception while listing '" + path + "': " + exception.what();
        entries.clear();
    }

    std::sort(entries.begin(), entries.end(), [](const DirEntry& left, const DirEntry& right) {
        return left.name < right.name;
    });
    return entries;
}

} // namespace diskmap
