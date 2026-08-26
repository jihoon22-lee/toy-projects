#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace loglens {

// Abstraction over "give me any new lines" so tailing logic can be unit-tested
// without a real file on disk.
class LogSource {
public:
    virtual ~LogSource();

    // Appends lines available since the last call. Returns false and sets
    // error on failure. Never throws.
    virtual bool poll(std::vector<std::string>& out, std::string& error) = 0;
};

// Polling tailer.
//
// Restart detection is by size only: if the file is now smaller than the offset
// we already consumed, it was truncated or replaced, so reading resumes from the
// beginning instead of silently emitting nothing. Modification time cannot serve
// as a file identity here — it changes on every ordinary append, so using it
// would treat normal writes as rotations. Detecting a rotation to a file that is
// already larger than the old one needs the inode, which std::filesystem does
// not expose; that case is knowingly not covered.
class FileTailer : public LogSource {
public:
    explicit FileTailer(std::string path);

    bool poll(std::vector<std::string>& out, std::string& error) override;

    std::uint64_t offset() const;
    std::size_t restarts() const;

private:
    std::string path_;
    std::uint64_t offset_ = 0;
    std::size_t restarts_ = 0;

    bool detectRestart(std::uint64_t size);
};

} // namespace loglens
