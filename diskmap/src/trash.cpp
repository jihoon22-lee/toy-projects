#include "diskmap/trash.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace diskmap {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kMaxInfoBytes = 16 * 1024;
constexpr std::size_t kMaxTokenBytes = 128;

TrashReceipt failure(TrashStatus status,
                     const fs::path& original,
                     std::string message) {
    TrashReceipt receipt;
    receipt.status = status;
    receipt.original_path = original;
    receipt.message = std::move(message);
    return receipt;
}

fs::path normalizedAbsolute(const fs::path& path) {
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

bool validAbsoluteEntry(const fs::path& path) {
    if (!path.is_absolute() || path.filename().empty()) {
        return false;
    }
    const fs::path filename = path.filename();
    return filename != "." && filename != "..";
}

bool validToken(const std::string& token) {
    if (token.empty() || token.size() > kMaxTokenBytes
        || token.rfind("diskmap-", 0) != 0) {
        return false;
    }
    return std::all_of(token.begin(), token.end(), [](unsigned char value) {
        return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
               || (value >= '0' && value <= '9') || value == '-' || value == '_';
    });
}

fs::path environmentDataHome(std::string& error) {
    const char* configured = std::getenv("XDG_DATA_HOME");
    if (configured != nullptr && *configured != '\0') {
        return configured;
    }
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        error = "HOME and XDG_DATA_HOME are unavailable";
        return {};
    }
    return fs::path(home) / ".local" / "share";
}

fs::path configuredDataHome(const TrashOptions& options, std::string& error) {
    fs::path dataHome = options.data_home.empty() ? environmentDataHome(error)
                                                  : options.data_home;
    if (dataHome.empty()) {
        return {};
    }
    dataHome = normalizedAbsolute(dataHome);
    if (!dataHome.is_absolute()) {
        error = "trash data home must resolve to an absolute path";
        return {};
    }
    return dataHome;
}

std::string errnoMessage(const std::string& action, int value = errno) {
    return action + ": " + std::generic_category().message(value);
}

#if defined(__linux__)

class FileDescriptor {
public:
    explicit FileDescriptor(int value = -1) : value_(value) {}
    ~FileDescriptor() {
        if (value_ >= 0) {
            ::close(value_);
        }
    }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : value_(other.value_) {
        other.value_ = -1;
    }
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            if (value_ >= 0) {
                ::close(value_);
            }
            value_ = other.value_;
            other.value_ = -1;
        }
        return *this;
    }
    int get() const { return value_; }
    explicit operator bool() const { return value_ >= 0; }

private:
    int value_;
};

FileDescriptor openAbsoluteDirectory(const fs::path& input, std::string& error) {
    const fs::path path = normalizedAbsolute(input);
    if (!path.is_absolute()) {
        error = "directory path is not absolute";
        return FileDescriptor();
    }
    FileDescriptor current(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (!current) {
        error = errnoMessage("cannot open filesystem root");
        return FileDescriptor();
    }
    for (const fs::path& component : path.relative_path()) {
        const std::string name = component.native();
        if (name.empty() || name == ".") {
            continue;
        }
        if (name == "..") {
            error = "parent traversal is not allowed";
            return FileDescriptor();
        }
        FileDescriptor next(::openat(current.get(), name.c_str(),
                                     O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        if (!next) {
            error = errnoMessage("cannot open directory component '" + name + "'");
            return FileDescriptor();
        }
        current = std::move(next);
    }
    return current;
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
    struct stat status {};
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
    struct stat status {};
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
    struct stat status {};
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
    while (content.size() <= kMaxInfoBytes) {
        const ssize_t count = ::read(descriptor, buffer, sizeof(buffer));
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
    error = "restore metadata exceeds the size bound";
    return false;
}

#endif

bool ensureTrashDirectories(const fs::path& root, std::string& error) {
    std::error_code createError;
    fs::create_directories(root / "files", createError);
    if (!createError) {
        fs::create_directories(root / "info", createError);
    }
    if (createError) {
        error = "cannot create recoverable Trash directories: " + createError.message();
        return false;
    }
    return true;
}

bool unreservedByte(unsigned char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
           || (value >= '0' && value <= '9') || value == '-' || value == '_'
           || value == '.' || value == '~' || value == '/';
}

std::string percentEncodePath(const fs::path& path) {
    static const char digits[] = "0123456789ABCDEF";
    std::string result;
    const std::string bytes = path.native();
    result.reserve(bytes.size());
    for (unsigned char value : bytes) {
        if (unreservedByte(value)) {
            result += static_cast<char>(value);
        } else {
            result += '%';
            result += digits[value >> 4U];
            result += digits[value & 0x0FU];
        }
    }
    return result;
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool percentDecodePath(const std::string& encoded, fs::path& path) {
    std::string bytes;
    bytes.reserve(encoded.size());
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        if (encoded[index] != '%') {
            bytes += encoded[index];
            continue;
        }
        if (index + 2 >= encoded.size()) {
            return false;
        }
        const int high = hexValue(encoded[index + 1]);
        const int low = hexValue(encoded[index + 2]);
        if (high < 0 || low < 0) {
            return false;
        }
        const char decoded = static_cast<char>((high << 4) | low);
        if (decoded == '\0') {
            return false;
        }
        bytes += decoded;
        index += 2;
    }
    path = fs::path(bytes).lexically_normal();
    return validAbsoluteEntry(path);
}

std::string deletionTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local {};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32] {};
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%S", &local);
    return text;
}

const char* kindName(FsKind kind) {
    switch (kind) {
    case FsKind::RegularFile: return "file";
    case FsKind::Directory: return "directory";
    case FsKind::Symlink: return "symlink";
    case FsKind::Other: return "other";
    }
    return "other";
}

bool parseKind(const std::string& value, FsKind& kind) {
    if (value == "file") {
        kind = FsKind::RegularFile;
    } else if (value == "directory") {
        kind = FsKind::Directory;
    } else if (value == "symlink") {
        kind = FsKind::Symlink;
    } else {
        return false;
    }
    return true;
}

std::string infoContent(const CleanupTarget& target, const fs::path& original) {
    return "[Trash Info]\nPath=" + percentEncodePath(original)
           + "\nDeletionDate=" + deletionTimestamp()
           + "\nX-DiskMap-Device=" + std::to_string(target.identity.device)
           + "\nX-DiskMap-File=" + std::to_string(target.identity.file)
           + "\nX-DiskMap-Kind=" + kindName(target.kind) + "\n";
}

std::string nextToken(std::uint64_t sequence) {
#if defined(__linux__)
    const auto process = static_cast<unsigned long long>(::getpid());
#else
    const unsigned long long process = 0;
#endif
    const auto ticks = static_cast<unsigned long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream output;
    output << "diskmap-" << process << '-' << ticks << '-'
           << static_cast<unsigned long long>(sequence);
    return output.str();
}

struct RestoreMetadata {
    fs::path original;
    FileIdentity identity;
    FsKind kind = FsKind::Other;
};

bool parseUnsigned(const std::string& value, std::uint64_t& result) {
    if (value.empty()
        || !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(value, &consumed);
        if (consumed != value.size()) {
            return false;
        }
        result = static_cast<std::uint64_t>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseRestoreMetadata(const std::string& content,
                          RestoreMetadata& metadata,
                          std::string& error) {
    const std::string prefix = "[Trash Info]\nPath=";
    if (content.rfind(prefix, 0) != 0) {
        error = "restore metadata has an invalid header";
        return false;
    }
    const std::size_t end = content.find('\n', prefix.size());
    if (end == std::string::npos
        || !percentDecodePath(content.substr(prefix.size(), end - prefix.size()),
                              metadata.original)) {
        error = "restore metadata has an invalid original path";
        return false;
    }
    const auto field = [&](const std::string& name) -> std::string {
        const std::string marker = "\n" + name + "=";
        const std::size_t begin = content.find(marker, end);
        if (begin == std::string::npos) {
            return {};
        }
        const std::size_t valueBegin = begin + marker.size();
        const std::size_t valueEnd = content.find('\n', valueBegin);
        return content.substr(valueBegin, valueEnd - valueBegin);
    };
    metadata.identity.valid =
        parseUnsigned(field("X-DiskMap-Device"), metadata.identity.device)
        && parseUnsigned(field("X-DiskMap-File"), metadata.identity.file);
    if (!metadata.identity.valid || !parseKind(field("X-DiskMap-Kind"), metadata.kind)) {
        error = "restore metadata lacks DiskMap identity evidence";
        return false;
    }
    return true;
}

#if defined(__linux__)

struct TrashDirectories {
    fs::path root;
    FileDescriptor files;
    FileDescriptor info;
};

bool openTrashDirectories(const TrashOptions& options,
                          bool create,
                          TrashDirectories& directories,
                          std::string& error) {
    const fs::path dataHome = configuredDataHome(options, error);
    if (dataHome.empty()) {
        return false;
    }
    directories.root = dataHome / "Trash";
    if (create && !ensureTrashDirectories(directories.root, error)) {
        return false;
    }
    directories.files = openAbsoluteDirectory(directories.root / "files", error);
    directories.info = openAbsoluteDirectory(directories.root / "info", error);
    return directories.files && directories.info
           && secureOwnedDirectory(directories.files, error)
           && secureOwnedDirectory(directories.info, error);
}

struct ValidatedSource {
    fs::path original;
    std::string name;
    FileDescriptor parent;
};

bool validateSource(const CleanupTarget& target,
                    std::uint64_t generation,
                    ValidatedSource& source,
                    TrashReceipt& receipt) {
    source.original = normalizedAbsolute(target.path);
    if (!validAbsoluteEntry(source.original) || !target.identity.valid
        || target.scan_generation != generation) {
        receipt = failure(
            TrashStatus::InvalidRequest, source.original,
            "cleanup target is not valid for this reviewed scan generation");
        return false;
    }
    std::string error;
    source.parent = openAbsoluteDirectory(source.original.parent_path(), error);
    if (!source.parent) {
        receipt = failure(TrashStatus::RevalidationFailed, source.original, error);
        return false;
    }
    source.name = source.original.filename().native();
    struct stat status {};
    if (::fstatat(source.parent.get(), source.name.c_str(), &status,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        receipt = failure(TrashStatus::RevalidationFailed, source.original,
                          errnoMessage("cannot revalidate cleanup target"));
        return false;
    }
    const TrashStatus validation = validateStat(target, status, error);
    if (validation != TrashStatus::Ready) {
        receipt = failure(validation, source.original, error);
        return false;
    }
    return true;
}

bool reserveMetadata(const TrashDirectories& directories,
                     const CleanupTarget& target,
                     const ValidatedSource& source,
                     const std::string& tempName,
                     FileDescriptor& metadata,
                     TrashReceipt& receipt) {
    metadata = FileDescriptor(::openat(
        directories.info.get(), tempName.c_str(),
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    std::string error;
    if (metadata && writeAll(metadata.get(), infoContent(target, source.original), error)) {
        return true;
    }
    const std::string message =
        metadata ? error : errnoMessage("cannot reserve trash metadata");
    ::unlinkat(directories.info.get(), tempName.c_str(), 0);
    receipt = failure(TrashStatus::IoError, source.original, message);
    return false;
}

TrashReceipt rollbackMoved(const TrashDirectories& directories,
                           const ValidatedSource& source,
                           const std::string& token,
                           const std::string& tempName,
                           TrashStatus status,
                           const std::string& message) {
    const int rollback = renameNoReplace(directories.files.get(), token.c_str(),
                                         source.parent.get(), source.name.c_str());
    ::unlinkat(directories.info.get(), tempName.c_str(), 0);
    TrashReceipt receipt = failure(
        status, source.original,
        rollback == 0 ? message : message + "; rollback failed");
    if (rollback != 0) {
        receipt.trashed_path = directories.root / "files" / token;
        receipt.restore_token = token;
    }
    return receipt;
}

TrashReceipt finalizeTrashMove(const TrashDirectories& directories,
                               const CleanupTarget& target,
                               const ValidatedSource& source,
                               const std::string& token,
                               const std::string& tempName) {
    std::string error;
    struct stat movedStatus {};
    if (::fstatat(directories.files.get(), token.c_str(), &movedStatus,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        return rollbackMoved(directories, source, token, tempName,
                             TrashStatus::RevalidationFailed,
                             errnoMessage("cannot verify the moved Trash target"));
    }
    if (validateStat(target, movedStatus, error) != TrashStatus::Ready) {
        return rollbackMoved(directories, source, token, tempName,
                             TrashStatus::RevalidationFailed, error);
    }
    const std::string infoName = token + ".trashinfo";
    if (renameNoReplace(directories.info.get(), tempName.c_str(),
                        directories.info.get(), infoName.c_str()) != 0) {
        const int value = errno;
        return rollbackMoved(
            directories, source, token, tempName, TrashStatus::IoError,
            errnoMessage("cannot finalize trash metadata", value));
    }
    ::fsync(directories.files.get());
    ::fsync(directories.info.get());
    TrashReceipt receipt;
    receipt.status = TrashStatus::Moved;
    receipt.original_path = source.original;
    receipt.trashed_path = directories.root / "files" / token;
    receipt.restore_token = token;
    receipt.message = "moved to recoverable Trash";
    return receipt;
}

TrashReceipt moveOneToTrash(const CleanupTarget& target,
                            std::uint64_t generation,
                            const TrashDirectories& directories,
                            const std::string& token) {
    ValidatedSource source;
    TrashReceipt receipt;
    if (!validateSource(target, generation, source, receipt)) {
        return receipt;
    }
    std::string error;
    if (!sameDevice(directories.files, target.identity.device, error)
        || !sameDevice(directories.info, target.identity.device, error)) {
        return failure(TrashStatus::DifferentFilesystem, source.original, error);
    }
    const std::string tempName = token + ".tmp";
    FileDescriptor metadata;
    if (!reserveMetadata(directories, target, source, tempName, metadata, receipt)) {
        return receipt;
    }
    if (renameNoReplace(source.parent.get(), source.name.c_str(),
                        directories.files.get(), token.c_str()) != 0) {
        const int value = errno;
        ::unlinkat(directories.info.get(), tempName.c_str(), 0);
        return failure(value == EEXIST ? TrashStatus::DestinationExists
                                       : TrashStatus::IoError,
                       source.original,
                       errnoMessage("cannot move target to Trash", value));
    }
    return finalizeTrashMove(directories, target, source, token, tempName);
}

bool loadRestoreMetadata(const TrashDirectories& directories,
                         const std::string& token,
                         RestoreMetadata& restore,
                         TrashReceipt& receipt) {
    const std::string infoName = token + ".trashinfo";
    FileDescriptor metadata(::openat(directories.info.get(), infoName.c_str(),
                                     O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!metadata) {
        const int value = errno;
        receipt = failure(value == ENOENT ? TrashStatus::MissingToken
                                          : TrashStatus::IoError,
                          {}, errnoMessage("cannot open restore metadata", value));
        return false;
    }
    std::string content;
    std::string error;
    if (!readBounded(metadata.get(), content, error)
        || !parseRestoreMetadata(content, restore, error)) {
        receipt = failure(TrashStatus::IoError, {}, error);
        return false;
    }
    return true;
}

TrashReceipt restoreOne(const TrashDirectories& directories,
                        const std::string& token,
                        const RestoreMetadata& restore) {
    struct stat trashedStatus {};
    if (::fstatat(directories.files.get(), token.c_str(), &trashedStatus,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        const int value = errno;
        return failure(value == ENOENT ? TrashStatus::MissingToken : TrashStatus::IoError,
                       restore.original,
                       errnoMessage("cannot inspect trashed target", value));
    }
    const FileIdentity trashedIdentity{
        static_cast<std::uint64_t>(trashedStatus.st_dev),
        static_cast<std::uint64_t>(trashedStatus.st_ino), true};
    if (trashedIdentity != restore.identity || kindFromMode(trashedStatus.st_mode) != restore.kind) {
        return failure(TrashStatus::RevalidationFailed, restore.original,
                       "trashed target no longer matches its identity metadata");
    }
    std::string error;
    FileDescriptor parent = openAbsoluteDirectory(restore.original.parent_path(), error);
    if (!parent) {
        return failure(TrashStatus::IoError, restore.original, error);
    }
    const std::string name = restore.original.filename().native();
    struct stat ignored {};
    if (::fstatat(parent.get(), name.c_str(), &ignored, AT_SYMLINK_NOFOLLOW) == 0
        || errno != ENOENT) {
        return failure(TrashStatus::DestinationExists, restore.original,
                       "restore destination already exists or cannot be inspected safely");
    }
    if (renameNoReplace(directories.files.get(), token.c_str(),
                        parent.get(), name.c_str()) != 0) {
        const int value = errno;
        return failure(value == EEXIST ? TrashStatus::DestinationExists
                                       : TrashStatus::IoError,
                       restore.original,
                       errnoMessage("cannot restore trashed target", value));
    }
    const std::string infoName = token + ".trashinfo";
    const bool metadataRemoved =
        ::unlinkat(directories.info.get(), infoName.c_str(), 0) == 0;
    ::fsync(parent.get());
    ::fsync(directories.files.get());
    ::fsync(directories.info.get());
    TrashReceipt receipt;
    receipt.status = TrashStatus::Restored;
    receipt.original_path = restore.original;
    receipt.restore_token = token;
    receipt.message = metadataRemoved
                          ? "restored from recoverable Trash"
                          : "restored, but stale Trash metadata could not be removed";
    return receipt;
}

#endif

} // namespace

const char* trashStatusName(TrashStatus status) {
    switch (status) {
    case TrashStatus::Ready: return "ready";
    case TrashStatus::Moved: return "moved";
    case TrashStatus::Restored: return "restored";
    case TrashStatus::UnsupportedPlatform: return "unsupported-platform";
    case TrashStatus::InvalidRequest: return "invalid-request";
    case TrashStatus::RevalidationFailed: return "revalidation-failed";
    case TrashStatus::DifferentFilesystem: return "different-filesystem";
    case TrashStatus::DestinationExists: return "destination-exists";
    case TrashStatus::MissingToken: return "missing-token";
    case TrashStatus::IoError: return "io-error";
    }
    return "unknown";
}

TrashCapability inspectTrashCapability(const CleanupTarget& target,
                                        const TrashOptions& options) {
    TrashCapability capability;
#if !defined(__linux__)
    (void)target;
    (void)options;
    capability.status = TrashStatus::UnsupportedPlatform;
    capability.message = "the recoverable Trash backend is currently available on Linux";
    return capability;
#else
    if (!validAbsoluteEntry(target.path) || !target.identity.valid) {
        capability.status = TrashStatus::InvalidRequest;
        capability.message = "cleanup target lacks an absolute path or stable identity";
        return capability;
    }
    std::string error;
    const fs::path dataHome = configuredDataHome(options, error);
    if (dataHome.empty()) {
        capability.status = TrashStatus::InvalidRequest;
        capability.message = std::move(error);
        return capability;
    }
    capability.trash_root = dataHome / "Trash";
    fs::path probe = dataHome;
    std::error_code existsError;
    while (!fs::exists(probe, existsError) && probe.has_parent_path()
           && probe.parent_path() != probe) {
        probe = probe.parent_path();
    }
    struct stat status {};
    if (existsError || ::stat(probe.c_str(), &status) != 0) {
        capability.status = TrashStatus::IoError;
        capability.message = existsError ? "cannot inspect Trash location: " + existsError.message()
                                         : errnoMessage("cannot inspect Trash location");
        return capability;
    }
    if (static_cast<std::uint64_t>(status.st_dev) != target.identity.device) {
        capability.status = TrashStatus::DifferentFilesystem;
        capability.message = "source and home Trash are on different filesystems";
        return capability;
    }
    capability.available = true;
    capability.status = TrashStatus::Ready;
    capability.message = "recoverable same-filesystem Trash is available";
    return capability;
#endif
}

std::vector<TrashReceipt> movePlanToTrash(const CleanupPlan& plan,
                                          const TrashOptions& options) {
    std::vector<TrashReceipt> receipts;
    if (options.max_targets == 0 || plan.targets.size() > options.max_targets) {
        receipts.push_back(failure(TrashStatus::InvalidRequest, {},
                                   "cleanup plan exceeds the execution bound"));
        return receipts;
    }
    receipts.reserve(plan.targets.size());
#if !defined(__linux__)
    for (const CleanupTarget& target : plan.targets) {
        receipts.push_back(failure(
            TrashStatus::UnsupportedPlatform, target.path,
            "the recoverable Trash backend is currently available on Linux"));
    }
#else
    std::string error;
    TrashDirectories directories;
    if (!openTrashDirectories(options, true, directories, error)) {
        for (const CleanupTarget& target : plan.targets) {
            receipts.push_back(failure(TrashStatus::IoError, target.path, error));
        }
        return receipts;
    }
    std::uint64_t sequence = 0;
    for (const CleanupTarget& target : plan.targets) {
        const std::string token = nextToken(sequence++);
        receipts.push_back(
            moveOneToTrash(target, plan.scan_generation, directories, token));
    }
#endif
    return receipts;
}

TrashReceipt restoreFromTrash(const std::string& token,
                              const TrashOptions& options) {
    if (!validToken(token)) {
        return failure(TrashStatus::InvalidRequest, {}, "restore token is invalid");
    }
#if !defined(__linux__)
    (void)options;
    return failure(TrashStatus::UnsupportedPlatform, {},
                   "the recoverable Trash backend is currently available on Linux");
#else
    std::string error;
    TrashDirectories directories;
    if (!openTrashDirectories(options, false, directories, error)) {
        return failure(TrashStatus::IoError, {}, error);
    }
    RestoreMetadata restore;
    TrashReceipt receipt;
    if (!loadRestoreMetadata(directories, token, restore, receipt)) {
        return receipt;
    }
    return restoreOne(directories, token, restore);
#endif
}

} // namespace diskmap
