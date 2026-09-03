#include "trash_linux_internal.hpp"

#if defined(__linux__)

#include <cerrno>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace diskmap {
namespace detail {

namespace fs = std::filesystem;

namespace {

struct ValidatedSource {
    fs::path original;
    std::string name;
    FileDescriptor parent;
};

bool probeTrashDirectory(const fs::path& path,
                         std::uint64_t sourceDevice,
                         std::string& error) {
    FileDescriptor closest;
    bool complete = false;
    if (!inspectAbsoluteDirectoryPath(path, closest, complete, error)
        || !directoryWritable(closest, error)
        || !sameDevice(closest, sourceDevice, error)) {
        return false;
    }
    return true;
}

bool validateSource(const CleanupTarget& target,
                    std::uint64_t generation,
                    ValidatedSource& source,
                    TrashReceipt& receipt) {
    source.original = normalizedAbsolute(target.path);
    if (!validAbsoluteEntry(target.path) || !target.identity.valid
        || target.scan_generation != generation) {
        receipt = trashFailure(
            TrashStatus::InvalidRequest, source.original,
            "cleanup target is not valid for this reviewed scan generation");
        return false;
    }
    std::string error;
    source.parent = openAbsoluteDirectory(source.original.parent_path(), error);
    if (!source.parent) {
        receipt = trashFailure(TrashStatus::RevalidationFailed, source.original, error);
        return false;
    }
    source.name = source.original.filename().native();
    struct stat status{};
    if (::fstatat(source.parent.get(), source.name.c_str(), &status,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        receipt = trashFailure(TrashStatus::RevalidationFailed, source.original,
                               errnoMessage("cannot revalidate cleanup target"));
        return false;
    }
    const TrashStatus validation = validateStat(target, status, error);
    if (validation != TrashStatus::Ready) {
        receipt = trashFailure(validation, source.original, error);
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
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    metadata = FileDescriptor(
        ::openat(directories.info.get(), tempName.c_str(), flags, 0600));
    std::string error;
    if (metadata && writeAll(metadata.get(), infoContent(target, source.original), error)) {
        return true;
    }
    const std::string message =
        metadata ? error : errnoMessage("cannot reserve trash metadata");
    ::unlinkat(directories.info.get(), tempName.c_str(), 0);
    receipt = trashFailure(TrashStatus::IoError, source.original, message);
    return false;
}

TrashReceipt rollbackMoved(const TrashDirectories& directories,
                           const ValidatedSource& source,
                           const std::string& token,
                           const std::string& tempName,
                           FileDescriptor& metadata,
                           TrashStatus status,
                           const std::string& message) {
    const int rollback = renameNoReplace(directories.files.get(), token.c_str(),
                                         source.parent.get(), source.name.c_str());
    if (rollback == 0) {
        ::unlinkat(directories.info.get(), tempName.c_str(), 0);
        return trashFailure(status, source.original, message);
    }

    // The reviewed name was replaced after validation, so a no-replace
    // rollback can legitimately be blocked. Keep the moved payload
    // recoverable with metadata matching its actual identity instead of
    // deleting the only restore record.
    struct stat movedStatus{};
    std::string recoveryError;
    if (::fstatat(directories.files.get(), token.c_str(), &movedStatus,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        recoveryError = errnoMessage("cannot inspect payload after rollback failure");
    } else {
        CleanupTarget actual;
        actual.identity = FileIdentity{static_cast<std::uint64_t>(movedStatus.st_dev),
                                       static_cast<std::uint64_t>(movedStatus.st_ino), true};
        actual.kind = kindFromMode(movedStatus.st_mode);
        if (::ftruncate(metadata.get(), 0) != 0 || ::lseek(metadata.get(), 0, SEEK_SET) < 0
            || !writeAll(metadata.get(), infoContent(actual, source.original), recoveryError)) {
            if (recoveryError.empty()) {
                recoveryError = errnoMessage("cannot rewrite recovery metadata");
            }
        } else {
            const std::string infoName = token + ".trashinfo";
            if (renameNoReplace(directories.info.get(), tempName.c_str(),
                                directories.info.get(), infoName.c_str()) != 0) {
                recoveryError = errnoMessage("cannot finalize recovery metadata");
            }
        }
    }
    TrashReceipt receipt = trashFailure(
        status, source.original,
        recoveryError.empty()
            ? message + "; rollback was blocked, replacement preserved in Trash"
            : message + "; rollback was blocked and recovery metadata failed: "
                  + recoveryError);
    receipt.trashed_path = directories.root / "files" / token;
    if (recoveryError.empty()) {
        receipt.restore_token = token;
    }
    return receipt;
}

TrashReceipt finalizeTrashMove(const TrashDirectories& directories,
                               const CleanupTarget& target,
                               const ValidatedSource& source,
                               const std::string& token,
                               const std::string& tempName,
                               FileDescriptor& metadata) {
    std::string error;
    struct stat movedStatus{};
    if (::fstatat(directories.files.get(), token.c_str(), &movedStatus,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        return rollbackMoved(directories, source, token, tempName, metadata,
                             TrashStatus::RevalidationFailed,
                             errnoMessage("cannot verify the moved Trash target"));
    }
    if (validateStat(target, movedStatus, error) != TrashStatus::Ready) {
        return rollbackMoved(directories, source, token, tempName, metadata,
                             TrashStatus::RevalidationFailed, error);
    }
    const std::string infoName = token + ".trashinfo";
    if (renameNoReplace(directories.info.get(), tempName.c_str(),
                        directories.info.get(), infoName.c_str()) != 0) {
        const int value = errno;
        return rollbackMoved(
            directories, source, token, tempName, metadata, TrashStatus::IoError,
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
        return trashFailure(TrashStatus::DifferentFilesystem, source.original, error);
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
        return trashFailure(value == EEXIST ? TrashStatus::DestinationExists
                                            : TrashStatus::IoError,
                            source.original,
                            errnoMessage("cannot move target to Trash", value));
    }
    return finalizeTrashMove(directories, target, source, token, tempName, metadata);
}

bool loadRestoreMetadata(const TrashDirectories& directories,
                         const std::string& token,
                         RestoreMetadata& restore,
                         TrashReceipt& receipt) {
    const std::string infoName = token + ".trashinfo";
    int flags = O_RDONLY;
#ifdef O_NONBLOCK
    // A token is untrusted input and the metadata pathname can be replaced
    // between calls.  Refuse FIFOs and other special files without allowing
    // open() to block before readBounded() can validate the descriptor.
    flags |= O_NONBLOCK;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    FileDescriptor metadata(::openat(directories.info.get(), infoName.c_str(), flags));
    if (!metadata) {
        const int value = errno;
        receipt = trashFailure(value == ENOENT ? TrashStatus::MissingToken
                                               : TrashStatus::IoError,
                               {}, errnoMessage("cannot open restore metadata", value));
        return false;
    }
    std::string content;
    std::string error;
    if (!readBounded(metadata.get(), content, error)
        || !parseRestoreMetadata(content, restore, error)) {
        receipt = trashFailure(TrashStatus::IoError, {}, error);
        return false;
    }
    return true;
}

TrashReceipt restoreOne(const TrashDirectories& directories,
                        const std::string& token,
                        const RestoreMetadata& restore) {
    struct stat trashedStatus{};
    if (::fstatat(directories.files.get(), token.c_str(), &trashedStatus,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        const int value = errno;
        return trashFailure(value == ENOENT ? TrashStatus::MissingToken
                                            : TrashStatus::IoError,
                            restore.original,
                            errnoMessage("cannot inspect trashed target", value));
    }
    const FileIdentity trashedIdentity{
        static_cast<std::uint64_t>(trashedStatus.st_dev),
        static_cast<std::uint64_t>(trashedStatus.st_ino), true};
    if (trashedIdentity != restore.identity
        || kindFromMode(trashedStatus.st_mode) != restore.kind) {
        return trashFailure(TrashStatus::RevalidationFailed, restore.original,
                            "trashed target no longer matches its identity metadata");
    }
    std::string error;
    FileDescriptor parent = openAbsoluteDirectory(restore.original.parent_path(), error);
    if (!parent) {
        return trashFailure(TrashStatus::IoError, restore.original, error);
    }
    const std::string name = restore.original.filename().native();
    struct stat ignored{};
    if (::fstatat(parent.get(), name.c_str(), &ignored, AT_SYMLINK_NOFOLLOW) == 0
        || errno != ENOENT) {
        return trashFailure(TrashStatus::DestinationExists, restore.original,
                            "restore destination already exists or cannot be inspected safely");
    }
    if (renameNoReplace(directories.files.get(), token.c_str(),
                        parent.get(), name.c_str()) != 0) {
        const int value = errno;
        return trashFailure(value == EEXIST ? TrashStatus::DestinationExists
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

} // namespace

TrashCapability inspectTrashCapabilityLinux(const CleanupTarget& target,
                                            const TrashOptions& options) {
    TrashCapability capability;
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
    if (!probeTrashDirectory(capability.trash_root / "files",
                             target.identity.device, error)
        || !probeTrashDirectory(capability.trash_root / "info",
                                target.identity.device, error)) {
        capability.status = error == "source and home Trash are on different filesystems"
                                ? TrashStatus::DifferentFilesystem
                                : TrashStatus::IoError;
        capability.message = std::move(error);
        return capability;
    }
    capability.available = true;
    capability.status = TrashStatus::Ready;
    capability.message = "recoverable same-filesystem Trash is available";
    return capability;
}

std::vector<TrashReceipt> movePlanToTrashLinux(const CleanupPlan& plan,
                                               const TrashOptions& options) {
    std::vector<TrashReceipt> receipts;
    if (options.max_targets == 0 || plan.targets.size() > options.max_targets) {
        receipts.push_back(trashFailure(TrashStatus::InvalidRequest, {},
                                        "cleanup plan exceeds the execution bound"));
        return receipts;
    }
    receipts.reserve(plan.targets.size());
    std::string error;
    TrashDirectories directories;
    if (!openTrashDirectories(options, true, directories, error)) {
        for (const CleanupTarget& target : plan.targets) {
            receipts.push_back(trashFailure(TrashStatus::IoError, target.path, error));
        }
        return receipts;
    }
    std::uint64_t sequence = 0;
    for (const CleanupTarget& target : plan.targets) {
        const std::string token = nextToken(sequence++);
        receipts.push_back(moveOneToTrash(target, plan.scan_generation,
                                          directories, token));
    }
    return receipts;
}

TrashReceipt restoreFromTrashLinux(const std::string& token,
                                   const TrashOptions& options) {
    std::string error;
    TrashDirectories directories;
    if (!openTrashDirectories(options, false, directories, error)) {
        return trashFailure(TrashStatus::IoError, {}, error);
    }
    RestoreMetadata restore;
    TrashReceipt receipt;
    if (!loadRestoreMetadata(directories, token, restore, receipt)) {
        return receipt;
    }
    return restoreOne(directories, token, restore);
}

} // namespace detail
} // namespace diskmap

#endif
