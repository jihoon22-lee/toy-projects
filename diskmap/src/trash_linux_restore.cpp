#include "trash_linux_internal.hpp"

#if defined(__linux__)

#include <cerrno>
#include <string>

#include <fcntl.h>
#include <unistd.h>

namespace diskmap {
namespace detail {

namespace {

bool loadRestoreMetadata(const TrashDirectories& directories,
                         const std::string& token,
                         RestoreMetadata& restore,
                         std::string& metadataName,
                         TrashReceipt& receipt) {
    metadataName = token + ".trashinfo";
    int flags = O_RDONLY;
#ifdef O_NONBLOCK
    // A token is untrusted input and the metadata pathname can be replaced
    // between calls. Refuse special files before readBounded() validates it.
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
    return trashFailure(
        TrashStatus::IoError, restore.original,
        message + "; rollback failed, restored entry and metadata were preserved, "
                  "but no recovery token can safely identify the payload");
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
    notifyTrashMutationTestHook(TrashMutationTestPoint::BeforePayloadRestore,
                                restore.original, directories.root / "files" / token);
    if (renameNoReplace(directories.files.get(), token.c_str(),
                        parent.get(), name.c_str()) != 0) {
        const int value = errno;
        return trashFailure(value == EEXIST ? TrashStatus::DestinationExists
                                            : TrashStatus::IoError,
                            restore.original,
                            errnoMessage("cannot restore trashed target", value));
    }
    notifyTrashMutationTestHook(TrashMutationTestPoint::PayloadRestored,
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
