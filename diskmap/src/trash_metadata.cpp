#include "trash_internal.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace diskmap {
namespace detail {

namespace fs = std::filesystem;

TrashReceipt trashFailure(TrashStatus status,
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

std::string errnoMessage(const std::string& action, int value) {
    return action + ": " + std::generic_category().message(value == 0 ? errno : value);
}

} // namespace detail
} // namespace diskmap
