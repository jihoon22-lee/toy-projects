#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "loglens/log_record.hpp"
#include "loglens/log_source.hpp"

namespace loglens {

// Evidence digests are SHA-256. They identify bytes; they are not authentication.
std::string sha256Hex(std::string_view bytes);
std::string sourceIdentity(const FileIdentity &identity);
std::string recordFingerprint(const LogRecord &record);

struct SourceEvidence {
    std::string identity;
    std::string fingerprint;
    std::string modified;
    std::size_t fingerprint_bytes = 0;
    std::uint64_t size = 0;
    std::string error;
    bool ok() const { return error.empty(); }
};
// A bounded prefix digest plus opened-file identity and size. The scope is explicit.
SourceEvidence captureSourceEvidence(const std::string &path, std::size_t maxBytes = 64 * 1024);

} // namespace loglens
