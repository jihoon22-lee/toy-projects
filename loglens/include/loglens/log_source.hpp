#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace loglens {

constexpr std::size_t kDefaultSourceChunkBytes = 1024 * 1024;
constexpr std::size_t kMaxSourceChunkBytes = 16 * 1024 * 1024;

struct FileIdentity {
    std::uint64_t device = 0;
    std::uint64_t file = 0;
    bool valid = false;

    bool operator==(const FileIdentity& other) const;
    bool operator!=(const FileIdentity& other) const;
};

enum class SourceChange { None, Truncated, Replaced };

enum class SourceErrorKind {
    None,
    Missing,
    PermissionDenied,
    OpenFailed,
    StatFailed,
    ReadFailed,
    UnsupportedFileType,
};

struct SourceError {
    SourceErrorKind kind = SourceErrorKind::None;
    std::string message;
    bool retryable = false;
};

// Abstraction over "give me any new lines" so tailing logic can be unit-tested
// without a real file on disk.
class LogSource {
public:
    virtual ~LogSource();

    // Appends lines available since the last call. Returns false and sets
    // error on failure. Never throws.
    virtual bool poll(std::vector<std::string>& out, std::string& error) = 0;
};

// Bytes read during one poll plus the source generation that owns them. The
// generation changes when FileTailer detects truncation/replacement by size;
// consumers must discard parser/model state before applying the new bytes.
struct SourceChunk {
    std::string bytes;
    bool generation_changed = false;
    std::uint64_t generation = 0;
    std::uint64_t position = 0;
    // Size observed from the opened file handle before this read. A one-shot
    // consumer can retain this boundary and avoid chasing concurrent appends.
    std::uint64_t snapshot_end = 0;
    // True when the same source generation had unread bytes at this poll's
    // snapshot. Consumers can schedule another bounded poll without waiting for
    // the normal follow interval.
    bool more_available = false;
    SourceChange change = SourceChange::None;
    FileIdentity identity;
    SourceError error;

    bool ok() const;
};

// Polling tailer.
//
// POSIX builds compare device/inode from the opened handle, so replacement is
// detected even when the new file is equal in size or larger. Other platforms
// retain the size-based fallback until a native identity adapter is available.
class FileTailer : public LogSource {
public:
    explicit FileTailer(std::string path,
                        std::size_t maxChunkBytes = kDefaultSourceChunkBytes,
                        std::uint64_t initialOffset = 0);

    bool poll(std::vector<std::string>& out, std::string& error) override;

    // Reads raw bytes after the current offset. Unlike poll(vector<string>&),
    // this preserves a final non-newline-terminated fragment for the shared
    // RecordAssembler to carry across polls.
    bool pollChunk(SourceChunk& out, std::string& error);
    SourceChunk pollChunk();
    // Reads no farther than maxPosition even if the file grows after an
    // earlier snapshot. Restart detection and generation reporting are still
    // applied normally.
    SourceChunk pollChunk(std::uint64_t maxPosition);

    std::uint64_t offset() const;
    std::size_t restarts() const;
    std::uint64_t generation() const;

private:
    std::string path_;
    std::uint64_t offset_ = 0;
    std::size_t restarts_ = 0;
    std::uint64_t generation_ = 0;
    FileIdentity identity_;
    // A successful read after any unavailable/error interval is a new source
    // generation even if the filesystem immediately reuses the old inode.
    bool recovery_pending_ = false;
    bool recovery_restart_started_ = false;
    std::size_t max_chunk_bytes_ = kDefaultSourceChunkBytes;

    SourceChunk initialChunk() const;
#ifndef _WIN32
    SourceChunk pollPosixChunk(std::optional<std::uint64_t> maxPosition);
#else
    SourceChunk pollPortableChunk(std::optional<std::uint64_t> maxPosition);
#endif
    SourceChange detectRestart(const FileIdentity& identity, std::uint64_t size);
};

} // namespace loglens
