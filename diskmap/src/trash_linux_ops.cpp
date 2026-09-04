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

thread_local TrashMutationTestHook mutationTestHook = nullptr;

void notifyMutationTestHook(TrashMutationTestPoint point,
                            const fs::path& original,
                            const fs::path& trashed) noexcept {
    if (mutationTestHook != nullptr) {
        mutationTestHook(point, original, trashed);
    }
}

} // namespace

void setTrashMutationTestHook(TrashMutationTestHook hook) noexcept {
    mutationTestHook = hook;
}

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
        || !sameDevice(closest, sourceDevice, error)
        || (complete && !directoryOwnedByEffectiveUser(closest, error))) {
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
        if (::fsync(metadata.get()) == 0) {
            return true;
        }
        error = errnoMessage("cannot sync reserved trash metadata");
    }
    const std::string message =
        metadata ? error : errnoMessage("cannot reserve trash metadata");
    if (metadata) {
        ::unlinkat(directories.info.get(), tempName.c_str(), 0);
    }
    receipt = trashFailure(TrashStatus::IoError, source.original, message);
    return false;
}

bool finalizeRecoveryMetadata(const TrashDirectories& directories,
                              const ValidatedSource& source,
                              const std::string& token,
                              const std::string& tempName,
                              FileDescriptor& metadata,
                              std::string& recoveryError) {
    struct stat movedStatus{};
    if (::fstatat(directories.files.get(), token.c_str(), &movedStatus,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        recoveryError = errnoMessage("cannot inspect payload after rollback failure");
        return false;
    }
    CleanupTarget actual;
    actual.identity = FileIdentity{static_cast<std::uint64_t>(movedStatus.st_dev),
                                   static_cast<std::uint64_t>(movedStatus.st_ino), true};
    actual.kind = kindFromMode(movedStatus.st_mode);
    if (::ftruncate(metadata.get(), 0) != 0 || ::lseek(metadata.get(), 0, SEEK_SET) < 0
        || !writeAll(metadata.get(), infoContent(actual, source.original), recoveryError)
        || ::fsync(metadata.get()) != 0) {
        if (recoveryError.empty()) {
            recoveryError = errnoMessage("cannot rewrite recovery metadata");
        }
        return false;
    }
    const std::string infoName = token + ".trashinfo";
    if (renameNoReplace(directories.info.get(), tempName.c_str(),
                        directories.info.get(), infoName.c_str()) != 0) {
        recoveryError = errnoMessage("cannot finalize recovery metadata");
        return false;
    }
    if (::fsync(directories.info.get()) != 0) {
        recoveryError = errnoMessage("cannot sync recovery metadata directory");
        return false;
    }
    return true;
}

TrashReceipt movedRecoveryReceipt(const TrashDirectories& directories,
                                  const ValidatedSource& source,
                                  const std::string& token,
                                  const std::string& tempName,
                                  FileDescriptor& metadata,
                                  TrashStatus status,
                                  const std::string& message,
                                  const std::string& reason) {
    std::string anchorError;
    if (!trashDirectoriesAnchored(directories, anchorError)) {
        return trashFailure(
            TrashStatus::IoError, source.original,
            message + "; payload remains in the originally opened Trash directory, "
                      "but its public path changed; no recovery token was issued");
    }
    if (::fsync(directories.files.get()) != 0) {
        return trashFailure(
            TrashStatus::IoError, source.original,
            message + "; payload remains in Trash, but its directory could not be synced");
    }
    std::string recoveryError;
    finalizeRecoveryMetadata(directories, source, token, tempName, metadata,
                             recoveryError);
    anchorError.clear();
    if (!trashDirectoriesAnchored(directories, anchorError)) {
        return trashFailure(
            TrashStatus::IoError, source.original,
            message + "; Trash location changed while recovery metadata was finalized; "
                      "no recovery token was issued");
    }
    TrashReceipt receipt = trashFailure(
        status, source.original,
        recoveryError.empty() ? message + "; " + reason
                              : message + "; rollback recovery metadata failed: "
                                    + recoveryError);
    receipt.trashed_path = directories.root / "files" / token;
    if (recoveryError.empty()) {
        receipt.restore_token = token;
    }
    return receipt;
}

TrashReceipt rollbackMoved(const TrashDirectories& directories,
                           const ValidatedSource& source,
                           const std::string& token,
                           const std::string& tempName,
                           FileDescriptor& metadata,
                           TrashStatus status,
                           const std::string& message) {
    std::string rollbackError;
    const bool parentAnchored = directoryPathMatches(
        source.original.parent_path(), source.parent, rollbackError);
    if (!parentAnchored) {
        return movedRecoveryReceipt(
            directories, source, token, tempName, metadata, status, message,
            "rollback was refused after the source directory changed; "
            "payload preserved in Trash");
    }

    notifyMutationTestHook(TrashMutationTestPoint::BeforeMoveRollback,
                           source.original, directories.root / "files" / token);
    const int rollback = renameNoReplace(directories.files.get(), token.c_str(),
                                         source.parent.get(), source.name.c_str());
    if (rollback != 0) {
        return movedRecoveryReceipt(
            directories, source, token, tempName, metadata, status, message,
            "rollback was blocked, replacement preserved in Trash");
    }

    if (directoryPathMatches(source.original.parent_path(), source.parent,
                             rollbackError)) {
        if (::fsync(source.parent.get()) != 0
            || ::fsync(directories.files.get()) != 0) {
            return trashFailure(
                TrashStatus::IoError, source.original,
                message + "; rollback completed, but its directories could not be synced");
        }
        if (::unlinkat(directories.info.get(), tempName.c_str(), 0) != 0
            || ::fsync(directories.info.get()) != 0) {
            return trashFailure(
                TrashStatus::IoError, source.original,
                message + "; rollback completed, but stale recovery metadata may remain");
        }
        return trashFailure(status, source.original, message);
    }
    if (renameNoReplace(source.parent.get(), source.name.c_str(),
                        directories.files.get(), token.c_str()) == 0) {
        if (::fsync(source.parent.get()) != 0) {
            return trashFailure(
                TrashStatus::IoError, source.original,
                message + "; payload returned to Trash, but the detached source "
                          "directory could not be synced");
        }
        return movedRecoveryReceipt(
            directories, source, token, tempName, metadata, status, message,
            "source directory changed during rollback; payload returned to Trash");
    }
    return trashFailure(
        TrashStatus::IoError, source.original,
        message + "; source directory changed during rollback and the payload "
                  "could not be returned to Trash; no recovery token was issued");
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
    if (!trashDirectoriesAnchored(directories, error)) {
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
    if (::fsync(directories.info.get()) != 0
        || !trashDirectoriesAnchored(directories, error)) {
        const std::string failure = error.empty()
                                        ? errnoMessage("cannot sync trash metadata directory")
                                        : error;
        if (renameNoReplace(directories.info.get(), infoName.c_str(),
                            directories.info.get(), tempName.c_str()) == 0) {
            return rollbackMoved(directories, source, token, tempName, metadata,
                                 TrashStatus::IoError, failure);
        }
        TrashReceipt receipt = trashFailure(
            TrashStatus::IoError, source.original,
            failure + "; payload and metadata remain recoverable in Trash");
        receipt.trashed_path = directories.root / "files" / token;
        receipt.restore_token = token;
        return receipt;
    }
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
    if (!trashDirectoriesAnchored(directories, error)
        || !directoryPathMatches(source.original.parent_path(), source.parent,
                                 error)) {
        return trashFailure(TrashStatus::RevalidationFailed, source.original, error);
    }
    const std::string tempName = token + ".tmp";
    notifyMutationTestHook(TrashMutationTestPoint::BeforeMetadataReserve,
                           source.original, directories.root / "info" / tempName);
    FileDescriptor metadata;
    if (!reserveMetadata(directories, target, source, tempName, metadata, receipt)) {
        return receipt;
    }
    if (::fsync(directories.info.get()) != 0) {
        const int value = errno;
        ::unlinkat(directories.info.get(), tempName.c_str(), 0);
        return trashFailure(TrashStatus::IoError, source.original,
                            errnoMessage("cannot sync trash metadata directory", value));
    }
    notifyMutationTestHook(TrashMutationTestPoint::BeforePayloadMove,
                           source.original, directories.root / "files" / token);
    if (renameNoReplace(source.parent.get(), source.name.c_str(),
                        directories.files.get(), token.c_str()) != 0) {
        const int value = errno;
        ::unlinkat(directories.info.get(), tempName.c_str(), 0);
        return trashFailure(value == EEXIST ? TrashStatus::DestinationExists
                                            : TrashStatus::IoError,
                            source.original,
                            errnoMessage("cannot move target to Trash", value));
    }
    notifyMutationTestHook(TrashMutationTestPoint::PayloadMoved,
                           source.original, directories.root / "files" / token);
    if (::fsync(directories.files.get()) != 0) {
        return rollbackMoved(
            directories, source, token, tempName, metadata, TrashStatus::IoError,
            errnoMessage("cannot sync moved Trash payload"));
    }
    return finalizeTrashMove(directories, target, source, token, tempName, metadata);
}

bool loadRestoreMetadata(const TrashDirectories& directories,
                         const std::string& token,
                         RestoreMetadata& restore,
                         std::string& metadataName,
                         TrashReceipt& receipt) {
    metadataName = token + ".trashinfo";
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
    FileDescriptor metadata(
        ::openat(directories.info.get(), metadataName.c_str(), flags));
    if (!metadata && errno == ENOENT) {
        metadataName = token + ".tmp";
        metadata = FileDescriptor(
            ::openat(directories.info.get(), metadataName.c_str(), flags));
    }
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

bool restorePayloadMatches(const RestoreMetadata& restore,
                           const struct stat& status) {
    const FileIdentity identity{static_cast<std::uint64_t>(status.st_dev),
                                static_cast<std::uint64_t>(status.st_ino), true};
    return identity == restore.identity && kindFromMode(status.st_mode) == restore.kind;
}

TrashReceipt rollbackRestore(const TrashDirectories& directories,
                             const std::string& token,
                             const RestoreMetadata& restore,
                             const FileDescriptor& parent,
                             const std::string& name,
                             TrashStatus status,
                             const std::string& message) {
    if (renameNoReplace(parent.get(), name.c_str(), directories.files.get(),
                        token.c_str()) == 0) {
        const bool parentSynced = ::fsync(parent.get()) == 0;
        const bool trashSynced = ::fsync(directories.files.get()) == 0;
        std::string anchorError;
        const bool trashAnchored = trashDirectoriesAnchored(directories, anchorError);
        const bool recoveryPublished = parentSynced && trashSynced && trashAnchored;
        TrashReceipt receipt = trashFailure(
            recoveryPublished ? status : TrashStatus::IoError,
            restore.original,
            recoveryPublished
                ? message
                : message
                      + (trashAnchored
                             ? "; rollback completed, but its directories could not be synced; "
                               "no recovery token was issued"
                             : "; rollback completed into the originally opened Trash "
                               "directory, but its public path changed; no recovery token "
                               "was issued"));
        if (recoveryPublished) {
            receipt.trashed_path = directories.root / "files" / token;
            receipt.restore_token = token;
        }
        return receipt;
    }
    TrashReceipt receipt = trashFailure(
        TrashStatus::IoError, restore.original,
        message + "; rollback failed, restored entry and metadata were preserved, "
                  "but no recovery token can safely identify the payload");
    return receipt;
}

bool restoreDestinationAbsent(const FileDescriptor& parent,
                              const std::string& name) {
    struct stat ignored{};
    return ::fstatat(parent.get(), name.c_str(), &ignored,
                     AT_SYMLINK_NOFOLLOW) != 0
           && errno == ENOENT;
}

bool restoredPayloadStable(const TrashDirectories& directories,
                           const RestoreMetadata& restore,
                           const FileDescriptor& parent,
                           const std::string& name,
                           std::string& error) {
    struct stat restoredStatus{};
    return ::fstatat(parent.get(), name.c_str(), &restoredStatus,
                     AT_SYMLINK_NOFOLLOW) == 0
           && restorePayloadMatches(restore, restoredStatus)
           && directoryPathMatches(restore.original.parent_path(), parent, error)
           && trashDirectoriesAnchored(directories, error);
}

TrashReceipt completedRestore(const TrashDirectories& directories,
                              const std::string& token,
                              const RestoreMetadata& restore,
                              const std::string& metadataName) {
    const bool metadataRemoved =
        ::unlinkat(directories.info.get(), metadataName.c_str(), 0) == 0;
    const bool metadataSynced = ::fsync(directories.info.get()) == 0;
    TrashReceipt receipt;
    receipt.status = TrashStatus::Restored;
    receipt.original_path = restore.original;
    receipt.restore_token = token;
    receipt.message = metadataRemoved && metadataSynced
                          ? "restored from recoverable Trash"
                          : "restored, but stale Trash metadata may remain";
    return receipt;
}

TrashReceipt restoreOne(const TrashDirectories& directories,
                        const std::string& token,
                        const RestoreMetadata& restore,
                        const std::string& metadataName) {
    struct stat trashedStatus{};
    if (::fstatat(directories.files.get(), token.c_str(), &trashedStatus,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        const int value = errno;
        return trashFailure(value == ENOENT ? TrashStatus::MissingToken
                                            : TrashStatus::IoError,
                            restore.original,
                            errnoMessage("cannot inspect trashed target", value));
    }
    if (!restorePayloadMatches(restore, trashedStatus)) {
        return trashFailure(TrashStatus::RevalidationFailed, restore.original,
                            "trashed target no longer matches its identity metadata");
    }
    std::string error;
    FileDescriptor parent = openAbsoluteDirectory(restore.original.parent_path(), error);
    if (!parent) {
        return trashFailure(TrashStatus::IoError, restore.original, error);
    }
    if (!trashDirectoriesAnchored(directories, error)
        || !directoryPathMatches(restore.original.parent_path(), parent, error)) {
        return trashFailure(TrashStatus::RevalidationFailed, restore.original, error);
    }
    const std::string name = restore.original.filename().native();
    if (!restoreDestinationAbsent(parent, name)) {
        return trashFailure(TrashStatus::DestinationExists, restore.original,
                            "restore destination already exists or cannot be inspected safely");
    }
    notifyMutationTestHook(TrashMutationTestPoint::BeforePayloadRestore,
                           restore.original, directories.root / "files" / token);
    if (renameNoReplace(directories.files.get(), token.c_str(),
                        parent.get(), name.c_str()) != 0) {
        const int value = errno;
        return trashFailure(value == EEXIST ? TrashStatus::DestinationExists
                                            : TrashStatus::IoError,
                            restore.original,
                            errnoMessage("cannot restore trashed target", value));
    }
    notifyMutationTestHook(TrashMutationTestPoint::PayloadRestored,
                           restore.original, directories.root / "files" / token);
    if (!restoredPayloadStable(directories, restore, parent, name, error)) {
        const std::string failure = error.empty()
                                        ? "restored payload changed during final validation"
                                        : error;
        return rollbackRestore(directories, token, restore, parent, name,
                               TrashStatus::RevalidationFailed, failure);
    }
    if (::fsync(parent.get()) != 0 || ::fsync(directories.files.get()) != 0) {
        return rollbackRestore(
            directories, token, restore, parent, name, TrashStatus::IoError,
            errnoMessage("cannot sync restored payload"));
    }
    return completedRestore(directories, token, restore, metadataName);
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
    std::string metadataName;
    TrashReceipt receipt;
    if (!loadRestoreMetadata(directories, token, restore, metadataName, receipt)) {
        return receipt;
    }
    return restoreOne(directories, token, restore, metadataName);
}

} // namespace detail
} // namespace diskmap

#endif
