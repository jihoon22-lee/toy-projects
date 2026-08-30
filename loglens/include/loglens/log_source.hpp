#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace loglens {

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
    explicit FileTailer(std::string path);

    bool poll(std::vector<std::string>& out, std::string& error) override;

    // Reads raw bytes after the current offset. Unlike poll(vector<string>&),
    // this preserves a final non-newline-terminated fragment for the shared
    // RecordAssembler to carry across polls.
    bool pollChunk(SourceChunk& out, std::string& error);
    SourceChunk pollChunk();

    std::uint64_t offset() const;
    std::size_t restarts() const;
    std::uint64_t generation() const;

private:
    std::string path_;
    std::uint64_t offset_ = 0;
    std::size_t restarts_ = 0;
    std::uint64_t generation_ = 0;
    FileIdentity identity_;

    SourceChange detectRestart(const FileIdentity& identity, std::uint64_t size);
};

} // namespace loglens
