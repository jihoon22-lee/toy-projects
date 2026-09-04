#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "diskmap/cleanup.hpp"

namespace diskmap {

// DiskMap never permanently deletes data. Cleanup execution is restricted to
// a recoverable freedesktop.org Trash location on the same filesystem.
enum class TrashStatus {
    Ready,
    Moved,
    Restored,
    UnsupportedPlatform,
    InvalidRequest,
    RevalidationFailed,
    DifferentFilesystem,
    DestinationExists,
    MissingToken,
    IoError,
};

const char* trashStatusName(TrashStatus status);

struct TrashOptions {
    // Empty selects $XDG_DATA_HOME or ~/.local/share. Tests and embedders may
    // provide an isolated data home without changing process environment.
    std::filesystem::path data_home;
    std::size_t max_targets = 10'000;
};

struct TrashCapability {
    bool available = false;
    TrashStatus status = TrashStatus::IoError;
    std::filesystem::path trash_root;
    std::string message;
};

struct TrashReceipt {
    TrashStatus status = TrashStatus::IoError;
    std::filesystem::path original_path;
    std::filesystem::path trashed_path;
    // Opaque basename used by restoreFromTrash. It never contains separators.
    std::string restore_token;
    std::string message;

    bool succeeded() const {
        return status == TrashStatus::Moved || status == TrashStatus::Restored;
    }
};

// Read-only capability probe. It deliberately supports only a same-filesystem
// home Trash; volume-specific trash policy remains unavailable rather than
// silently falling back to copy-and-delete.
TrashCapability inspectTrashCapability(const CleanupTarget& target,
                                        const TrashOptions& options = TrashOptions{});

// Executes a previously reviewed plan one target at a time. Each target is
// identity/type/size/hard-link revalidated through an anchored no-follow
// directory descriptor immediately before rename. Rejected plan entries are
// never passed here, and no copy/delete fallback exists.
std::vector<TrashReceipt> movePlanToTrash(
    const CleanupPlan& plan,
    const TrashOptions& options = TrashOptions{});

// Restores exactly one item named by a receipt token. Existing destinations
// are never replaced. The token is intentionally the only restore selector so
// callers cannot inject arbitrary source or destination paths.
TrashReceipt restoreFromTrash(const std::string& restore_token,
                              const TrashOptions& options = TrashOptions{});

} // namespace diskmap
