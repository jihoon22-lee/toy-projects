#pragma once

#include "diskmap/trash.hpp"

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace diskmap {
namespace detail {

namespace fs = std::filesystem;

constexpr std::size_t kMaxInfoBytes = 16 * 1024;
constexpr std::size_t kMaxTokenBytes = 128;

TrashReceipt trashFailure(TrashStatus status,
                          const fs::path& original,
                          std::string message);
fs::path normalizedAbsolute(const fs::path& path);
bool validAbsoluteEntry(const fs::path& path);
bool validToken(const std::string& token);
fs::path configuredDataHome(const TrashOptions& options, std::string& error);
std::string errnoMessage(const std::string& action, int value = errno);

std::string infoContent(const CleanupTarget& target, const fs::path& original);
std::string nextToken(std::uint64_t sequence);

struct RestoreMetadata {
    fs::path original;
    FileIdentity identity;
    FsKind kind = FsKind::Other;
};

bool parseRestoreMetadata(const std::string& content,
                          RestoreMetadata& metadata,
                          std::string& error);

#if defined(__linux__)
TrashCapability inspectTrashCapabilityLinux(const CleanupTarget& target,
                                            const TrashOptions& options);
std::vector<TrashReceipt> movePlanToTrashLinux(const CleanupPlan& plan,
                                               const TrashOptions& options);
TrashReceipt restoreFromTrashLinux(const std::string& token,
                                   const TrashOptions& options);
#endif

} // namespace detail
} // namespace diskmap
