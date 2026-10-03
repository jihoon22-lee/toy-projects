#include "diskmap/trash.hpp"
#include "trash_linux_internal.hpp"
#include "snapshot_internal.hpp"

#include <algorithm>
#include <atomic>
#include <map>
#include <sstream>
#include <set>
#include <string>
#include <vector>
#if defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace diskmap {
#if defined(__linux__)
namespace detail {
namespace {
FileDescriptor historyDirectory(const TrashDirectories& dirs, bool create, std::string& error) {
    constexpr auto name = ".diskmap-receipts";
    if (create && ::mkdirat(dirs.root_directory.get(), name, 0700) != 0 && errno != EEXIST) {
        error = errnoMessage("cannot create receipt directory"); return {};
    }
    FileDescriptor result(::openat(dirs.root_directory.get(), name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (!result) { if (errno != ENOENT) error = errnoMessage("cannot open receipt directory"); return {}; }
    if (!secureOwnedDirectory(result, error)) return {};
    return result;
}
std::vector<std::string> names(int fd, std::size_t limit, std::string& error) {
    DIR* directory = ::fdopendir(::dup(fd));
    if (!directory) { error = errnoMessage("cannot list receipts"); return {}; }
    std::vector<std::string> result;
    while (const auto* entry = ::readdir(directory)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (result.size() >= limit) { error = "Trash history listing reached its configured bound"; break; }
        result.push_back(name);
    }
    ::closedir(directory);
    std::sort(result.begin(), result.end());
    return result;
}
bool readRecord(int parent, const std::string& name, std::string& content, std::string& error) {
    FileDescriptor fd(::openat(parent, name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    return fd && readBounded(fd.get(), content, error);
}
}
bool recordTrashReceipt(const TrashDirectories& dirs, const TrashReceipt& receipt, std::string& error) {
    auto directory = historyDirectory(dirs, true, error);
    if (!directory) return false;
    static std::atomic<std::uint64_t> sequence{0};
    const auto name = nextToken(sequence.fetch_add(1)) + ".receipt";
    const std::string content = "diskmap.receipt/v1\n" + std::to_string(static_cast<int>(receipt.status))
        + "\n" + bytesHex(receipt.original_path.native()) + "\n" + bytesHex(receipt.trashed_path.native())
        + "\n" + receipt.restore_token + "\n" + bytesHex(receipt.message) + "\n";
    if (content.size() > kMaxInfoBytes) { error = "receipt exceeds size bound"; return false; }
    FileDescriptor fd(::openat(directory.get(), name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (!fd || !writeAll(fd.get(), content, error) || ::fsync(fd.get()) != 0
        || ::fsync(directory.get()) != 0 || ::fsync(dirs.root_directory.get()) != 0) {
        error = "cannot persist receipt"; return false;
    }
    return true;
}
} // namespace detail
#endif

std::vector<TrashReceipt> listTrashHistory(const TrashOptions& options, std::string& error) {
    std::vector<TrashReceipt> result;
    error.clear();
#if defined(__linux__)
    detail::TrashDirectories dirs;
    if (!detail::openTrashDirectories(options, false, dirs, error)) return result;
    auto history = detail::historyDirectory(dirs, false, error);
    std::map<std::string, TrashReceipt> byToken;
    std::set<std::string> verifiedTokens;
    if (history) {
        for (const auto& name : detail::names(history.get(), options.max_targets, error)) {
            if (options.cancelled && options.cancelled()) break;
            if (name.size() < 8 || name.substr(name.size() - 8) != ".receipt") continue;
            std::string content;
            if (!detail::readRecord(history.get(), name, content, error)) continue;
            std::istringstream input(content);
            std::string schema, status, original, trashed, token, message, extra;
            if (!std::getline(input, schema) || schema != "diskmap.receipt/v1"
                || !std::getline(input, status) || !std::getline(input, original)
                || !std::getline(input, trashed) || !std::getline(input, token)
                || !std::getline(input, message) || std::getline(input, extra)) continue;
            try {
                std::size_t consumed = 0;
                const auto value = std::stoi(status, &consumed);
                if (consumed != status.size() || value < 0 || value > static_cast<int>(TrashStatus::Cancelled)) continue;
                TrashReceipt receipt;
                receipt.status = static_cast<TrashStatus>(value);
                receipt.original_path = detail::bytesFromHex(original);
                receipt.trashed_path = detail::bytesFromHex(trashed);
                receipt.restore_token = token;
                receipt.message = detail::bytesFromHex(message);
                if (!token.empty() && !detail::validToken(token)) continue;
                if (!token.empty()) {
                    const auto existing = byToken.find(token);
                    // A successful restore is terminal for a unique token. File
                    // iteration/PID ordering across restarts must not undo it.
                    if (existing == byToken.end() || existing->second.status != TrashStatus::Restored
                        || receipt.status == TrashStatus::Restored) byToken[token] = std::move(receipt);
                } else result.push_back(std::move(receipt));
            } catch (...) { error = "Malformed receipt ignored"; }
        }
    }
    // Reconstruct recovery receipts even if a crash occurred before audit persistence.
    for (const auto& name : detail::names(dirs.info.get(), options.max_targets, error)) {
        const auto suffix = name.rfind('.');
        if (suffix == std::string::npos) continue;
        if (name.substr(suffix) != ".trashinfo" && name.substr(suffix) != ".tmp") continue;
        const auto token = name.substr(0, suffix);
        if (!detail::validToken(token)) continue;
        std::string content;
        detail::RestoreMetadata metadata;
        if (!detail::readRecord(dirs.info.get(), name, content, error)
            || !detail::parseRestoreMetadata(content, metadata, error)) continue;
        struct stat payload{};
        if (::fstatat(dirs.files.get(), token.c_str(), &payload, AT_SYMLINK_NOFOLLOW) != 0
            || FileIdentity{static_cast<std::uint64_t>(payload.st_dev), static_cast<std::uint64_t>(payload.st_ino), true} != metadata.identity
            || detail::kindFromMode(payload.st_mode) != metadata.kind) {
            const auto prior = byToken.find(token);
            if (prior != byToken.end() && prior->second.status == TrashStatus::Restored) continue;
            auto& stale = byToken[token];
            stale.status = TrashStatus::RevalidationFailed;
            stale.original_path = metadata.original;
            stale.restore_token.clear();
            stale.message = "Stored receipt has no matching recoverable payload";
            continue;
        }
        TrashReceipt receipt;
        receipt.status = TrashStatus::Moved;
        receipt.original_path = metadata.original;
        receipt.trashed_path = dirs.root / "files" / token;
        receipt.restore_token = token;
        receipt.message = "Recovered durable Trash receipt; move did not free storage";
        verifiedTokens.insert(token);
        byToken[token] = std::move(receipt);
    }
    for (auto& [token, receipt] : byToken) {
        if (receipt.status == TrashStatus::Moved && verifiedTokens.find(token) == verifiedTokens.end()) {
            receipt.status = TrashStatus::MissingToken;
            receipt.restore_token.clear();
            receipt.message = "Historical move; no verified recovery metadata and payload in this listing";
        }
        if (receipt.status == TrashStatus::Moved) {
            struct stat metadata{};
            if (::fstatat(dirs.info.get(), (token + ".trashinfo").c_str(), &metadata, AT_SYMLINK_NOFOLLOW) != 0
                && ::fstatat(dirs.info.get(), (token + ".tmp").c_str(), &metadata, AT_SYMLINK_NOFOLLOW) != 0) {
                receipt.status = TrashStatus::MissingToken;
                receipt.restore_token.clear();
                receipt.message = "Historical move; recovery metadata is no longer available";
            }
        } else receipt.restore_token.clear();
        result.push_back(std::move(receipt));
    }
#else
    (void)options;
    error = "Trash recovery is available on Linux";
#endif
    return result;
}
} // namespace diskmap
