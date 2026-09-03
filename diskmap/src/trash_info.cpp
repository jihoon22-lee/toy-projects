#include "trash_internal.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>
#include <string>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace diskmap {
namespace detail {

namespace fs = std::filesystem;

namespace {

bool unreservedByte(unsigned char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')
           || (value >= '0' && value <= '9') || value == '-' || value == '_'
           || value == '.' || value == '~' || value == '/';
}

std::string percentEncodePath(const fs::path& path) {
    static const char digits[] = "0123456789ABCDEF";
    const std::string bytes = path.native();
    std::string result;
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
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32]{};
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

bool parseUnsigned(const std::string& value, std::uint64_t& result) {
    if (value.empty()
        || !std::all_of(value.begin(), value.end(), [](char c) {
               return c >= '0' && c <= '9';
           })) {
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

} // namespace

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

} // namespace detail
} // namespace diskmap
