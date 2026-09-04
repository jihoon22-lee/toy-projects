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

} // namespace

void setTrashMutationTestHook(TrashMutationTestHook hook) noexcept {
    mutationTestHook = hook;
}

void notifyTrashMutationTestHook(TrashMutationTestPoint point,
                                 const fs::path& original,
                                 const fs::path& trashed) noexcept {
    if (mutationTestHook != nullptr) {
        mutationTestHook(point, original, trashed);
    }
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
        || target.kind == FsKind::Other
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
    if (actual.kind == FsKind::Other) {
        recoveryError = "Trash payload kind cannot be restored safely";
        return false;
    }
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
    struct stat payloadStatus{};
    const bool payloadReachable =
        ::fstatat(directories.files.get(), token.c_str(), &payloadStatus,
                  AT_SYMLINK_NOFOLLOW) == 0
        && kindFromMode(payloadStatus.st_mode) != FsKind::Other;
    if (!payloadReachable && recoveryError.empty()) {
        recoveryError = "Trash payload could not be revalidated for recovery publication";
    }
    TrashReceipt receipt = trashFailure(
        status, source.original,
        recoveryError.empty() ? message + "; " + reason
                              : message + "; rollback recovery metadata failed: "
                                    + recoveryError);
    if (payloadReachable) {
        receipt.trashed_path = directories.root / "files" / token;
    }
    if (payloadReachable && recoveryError.empty()) {
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

    notifyTrashMutationTestHook(TrashMutationTestPoint::BeforeMoveRollback,
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
    notifyTrashMutationTestHook(TrashMutationTestPoint::MetadataFinalized,
                                source.original, directories.root / "info" / infoName);
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
        return trashFailure(
            TrashStatus::IoError, source.original,
            failure + "; recovery publication could not be confirmed; "
                      "no recovery token was issued");
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
    notifyTrashMutationTestHook(TrashMutationTestPoint::BeforeMetadataReserve,
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
    notifyTrashMutationTestHook(TrashMutationTestPoint::BeforePayloadMove,
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
    notifyTrashMutationTestHook(TrashMutationTestPoint::PayloadMoved,
                                source.original, directories.root / "files" / token);
    if (::fsync(directories.files.get()) != 0) {
        return rollbackMoved(
            directories, source, token, tempName, metadata, TrashStatus::IoError,
            errnoMessage("cannot sync moved Trash payload"));
    }
    return finalizeTrashMove(directories, target, source, token, tempName, metadata);
}

} // namespace

TrashCapability inspectTrashCapabilityLinux(const CleanupTarget& target,
                                            const TrashOptions& options) {
    TrashCapability capability;
    if (!validAbsoluteEntry(target.path) || !target.identity.valid
        || target.kind == FsKind::Other) {
        capability.status = TrashStatus::InvalidRequest;
        capability.message =
            "cleanup target lacks a supported kind, absolute path, or stable identity";
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

} // namespace detail
} // namespace diskmap

#endif
