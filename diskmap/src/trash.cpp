#include "diskmap/trash.hpp"

#include "trash_internal.hpp"

#include <string>
#include <vector>

namespace diskmap {

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
#if defined(__linux__)
    return detail::inspectTrashCapabilityLinux(target, options);
#else
    (void)target;
    (void)options;
    TrashCapability capability;
    capability.status = TrashStatus::UnsupportedPlatform;
    capability.message = "the recoverable Trash backend is currently available on Linux";
    return capability;
#endif
}

std::vector<TrashReceipt> movePlanToTrash(const CleanupPlan& plan,
                                          const TrashOptions& options) {
#if defined(__linux__)
    return detail::movePlanToTrashLinux(plan, options);
#else
    std::vector<TrashReceipt> receipts;
    if (options.max_targets == 0 || plan.targets.size() > options.max_targets) {
        receipts.push_back(detail::trashFailure(
            TrashStatus::InvalidRequest, {}, "cleanup plan exceeds the execution bound"));
        return receipts;
    }
    for (const CleanupTarget& target : plan.targets) {
        receipts.push_back(detail::trashFailure(
            TrashStatus::UnsupportedPlatform, target.path,
            "the recoverable Trash backend is currently available on Linux"));
    }
    return receipts;
#endif
}

TrashReceipt restoreFromTrash(const std::string& token,
                              const TrashOptions& options) {
    if (!detail::validToken(token)) {
        return detail::trashFailure(TrashStatus::InvalidRequest, {},
                                    "restore token is invalid");
    }
#if defined(__linux__)
    return detail::restoreFromTrashLinux(token, options);
#else
    (void)options;
    return detail::trashFailure(
        TrashStatus::UnsupportedPlatform, {},
        "the recoverable Trash backend is currently available on Linux");
#endif
}

} // namespace diskmap
