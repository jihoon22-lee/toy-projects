#include "loglens/log_source.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace loglens {

namespace fs = std::filesystem;

namespace {

SourceError sourceError(SourceErrorKind kind, const std::string& action, const std::string& path,
                        const std::string& detail, bool retryable) {
    SourceError error;
    error.kind = kind;
    error.message = action + " '" + path + "'";
    if (!detail.empty()) {
        error.message += ": " + detail;
    }
    error.retryable = retryable;
    return error;
}

SourceErrorKind openErrorKind(int value) {
    if (value == ENOENT || value == ENOTDIR) {
        return SourceErrorKind::Missing;
    }
    if (value == EACCES || value == EPERM) {
        return SourceErrorKind::PermissionDenied;
    }
    return SourceErrorKind::OpenFailed;
}

#ifndef _WIN32

class FileDescriptor {
public:
    explicit FileDescriptor(int value) : value_(value) {}
    ~FileDescriptor() {
        if (value_ >= 0) {
            ::close(value_);
        }
    }

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    int get() const { return value_; }

private:
    int value_;
};

int readOnlyFlags() {
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NONBLOCK
    // Opening a FIFO without O_NONBLOCK could hang before fstat identifies it.
    flags |= O_NONBLOCK;
#endif
    return flags;
}

#endif

} // namespace

bool FileIdentity::operator==(const FileIdentity& other) const {
    if (!valid || !other.valid) {
        return valid == other.valid;
    }
    return device == other.device && file == other.file;
}

bool FileIdentity::operator!=(const FileIdentity& other) const { return !(*this == other); }

bool SourceChunk::ok() const { return error.kind == SourceErrorKind::None; }

LogSource::~LogSource() = default;

FileTailer::FileTailer(std::string path, std::size_t maxChunkBytes)
    : path_(std::move(path)), max_chunk_bytes_(maxChunkBytes) {
    if (maxChunkBytes == 0 || maxChunkBytes > kMaxSourceChunkBytes) {
        throw std::invalid_argument("source chunk size is outside the supported range");
    }
}

std::uint64_t FileTailer::offset() const { return offset_; }

std::size_t FileTailer::restarts() const { return restarts_; }

std::uint64_t FileTailer::generation() const { return generation_; }

SourceChunk FileTailer::initialChunk() const {
    SourceChunk out;
    out.generation = generation_;
    out.position = offset_;
    out.identity = identity_;
    return out;
}

SourceChange FileTailer::detectRestart(const FileIdentity& identity, std::uint64_t size) {
    if (recovery_pending_) {
        if (identity.valid) {
            identity_ = identity;
        }
        offset_ = 0;
        if (!recovery_restart_started_) {
            ++restarts_;
            ++generation_;
            recovery_restart_started_ = true;
        }
        return SourceChange::Replaced;
    }
    if (identity_.valid && identity.valid && identity_ != identity) {
        identity_ = identity;
        offset_ = 0;
        ++restarts_;
        ++generation_;
        return SourceChange::Replaced;
    }
    if (identity.valid) {
        identity_ = identity;
    }
    if (size < offset_) {
        offset_ = 0;
        ++restarts_;
        ++generation_;
        return SourceChange::Truncated;
    }
    return SourceChange::None;
}

#ifndef _WIN32
SourceChunk FileTailer::pollPosixChunk() {
    SourceChunk out = initialChunk();
    const int rawDescriptor = ::open(path_.c_str(), readOnlyFlags());
    if (rawDescriptor < 0) {
        const int value = errno;
        const SourceErrorKind kind = openErrorKind(value);
        const std::string action =
            kind == SourceErrorKind::Missing ? "cannot stat" : "cannot open";
        out.error = sourceError(kind, action, path_, std::generic_category().message(value), true);
        return out;
    }
    const FileDescriptor descriptor(rawDescriptor);

    struct stat status {};
    if (::fstat(descriptor.get(), &status) != 0) {
        const int value = errno;
        out.error = sourceError(SourceErrorKind::StatFailed, "cannot stat", path_,
                                std::generic_category().message(value), true);
        return out;
    }
    if (!S_ISREG(status.st_mode)) {
        out.error = sourceError(SourceErrorKind::UnsupportedFileType, "cannot follow", path_,
                                "source is not a regular file", false);
        return out;
    }
    if (status.st_size < 0) {
        out.error = sourceError(SourceErrorKind::StatFailed, "cannot stat", path_,
                                "source reported a negative size", true);
        return out;
    }

    FileIdentity observed;
    observed.device = static_cast<std::uint64_t>(status.st_dev);
    observed.file = static_cast<std::uint64_t>(status.st_ino);
    observed.valid = true;
    const std::uint64_t observedSize = static_cast<std::uint64_t>(status.st_size);
    const SourceChange change = detectRestart(observed, observedSize);
    out.change = change;
    out.generation_changed = change != SourceChange::None;
    out.generation = generation_;
    out.identity = identity_;
    out.position = offset_;

    if (offset_ > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) ||
        ::lseek(descriptor.get(), static_cast<off_t>(offset_), SEEK_SET) < 0) {
        const int value = errno;
        out.error = sourceError(SourceErrorKind::ReadFailed, "cannot seek", path_,
                                std::generic_category().message(value), true);
        return out;
    }

    const std::uint64_t start = offset_;
    std::array<char, 64 * 1024> buffer{};
    while (out.bytes.size() < max_chunk_bytes_) {
        const std::size_t remaining = max_chunk_bytes_ - out.bytes.size();
        const std::size_t requested = std::min(buffer.size(), remaining);
        const ssize_t count = ::read(descriptor.get(), buffer.data(), requested);
        if (count > 0) {
            out.bytes.append(buffer.data(), static_cast<std::size_t>(count));
            continue;
        }
        if (count == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        const int value = errno;
        out.bytes.clear();
        out.error = sourceError(SourceErrorKind::ReadFailed, "error reading", path_,
                                std::generic_category().message(value), true);
        return out;
    }
    offset_ = start + static_cast<std::uint64_t>(out.bytes.size());
    out.position = offset_;
    out.more_available = offset_ < observedSize;
    return out;
}
#else
SourceChunk FileTailer::pollPortableChunk() {
    SourceChunk out = initialChunk();
    std::error_code statusError;
    const fs::file_status status = fs::status(path_, statusError);
    if (statusError) {
        const SourceErrorKind kind =
            statusError == std::errc::no_such_file_or_directory
                ? SourceErrorKind::Missing
                : statusError == std::errc::permission_denied ? SourceErrorKind::PermissionDenied
                                                               : SourceErrorKind::StatFailed;
        out.error = sourceError(kind, "cannot stat", path_, statusError.message(), true);
        return out;
    }
    if (!fs::is_regular_file(status)) {
        out.error = sourceError(SourceErrorKind::UnsupportedFileType, "cannot follow", path_,
                                "source is not a regular file", false);
        return out;
    }
    std::error_code sizeError;
    const std::uintmax_t size = fs::file_size(path_, sizeError);
    if (sizeError) {
        out.error = sourceError(SourceErrorKind::StatFailed, "cannot stat", path_,
                                sizeError.message(), true);
        return out;
    }
    out.change = detectRestart(FileIdentity(), static_cast<std::uint64_t>(size));
    out.generation_changed = out.change != SourceChange::None;
    out.generation = generation_;
    out.position = offset_;

    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        out.error = sourceError(SourceErrorKind::OpenFailed, "cannot open", path_, "", true);
        return out;
    }
    try {
        const std::uint64_t start = offset_;
        input.seekg(static_cast<std::streamoff>(start));
        const std::uint64_t available = static_cast<std::uint64_t>(size) - start;
        const std::size_t requested = static_cast<std::size_t>(
            std::min<std::uint64_t>(available, max_chunk_bytes_));
        out.bytes.resize(requested);
        input.read(out.bytes.data(), static_cast<std::streamsize>(requested));
        out.bytes.resize(static_cast<std::size_t>(input.gcount()));
        if (input.bad()) {
            out.bytes.clear();
            out.error =
                sourceError(SourceErrorKind::ReadFailed, "error reading", path_, "", true);
            return out;
        }
        offset_ = start + static_cast<std::uint64_t>(out.bytes.size());
        out.position = offset_;
        out.more_available = offset_ < static_cast<std::uint64_t>(size);
    } catch (const std::exception& ex) {
        out.bytes.clear();
        out.error = sourceError(SourceErrorKind::ReadFailed, "error reading", path_, ex.what(),
                                true);
        return out;
    }
    return out;
}
#endif

SourceChunk FileTailer::pollChunk() {
#ifndef _WIN32
    SourceChunk out = pollPosixChunk();
#else
    SourceChunk out = pollPortableChunk();
#endif
    if (out.ok()) {
        recovery_pending_ = false;
        recovery_restart_started_ = false;
    } else {
        recovery_pending_ = true;
        recovery_restart_started_ =
            recovery_restart_started_ || out.generation_changed;
    }
    return out;
}

bool FileTailer::pollChunk(SourceChunk& out, std::string& error) {
    out = pollChunk();
    error = out.error.message;
    return out.ok();
}

bool FileTailer::poll(std::vector<std::string>& out, std::string& error) {
    SourceChunk chunk;
    if (!pollChunk(chunk, error)) {
        return false;
    }

    std::size_t start = 0;
    while (true) {
        const std::size_t newline = chunk.bytes.find('\n', start);
        if (newline == std::string::npos) {
            break;
        }
        std::string line = chunk.bytes.substr(start, newline - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        out.push_back(std::move(line));
        start = newline + 1;
    }
    if (start < chunk.bytes.size()) {
        std::string line = chunk.bytes.substr(start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        out.push_back(std::move(line));
    }
    return true;
}

} // namespace loglens
