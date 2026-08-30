#include "loglens/log_source.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace loglens {

namespace fs = std::filesystem;

LogSource::~LogSource() = default;

FileTailer::FileTailer(std::string path) : path_(std::move(path)) {}

std::uint64_t FileTailer::offset() const { return offset_; }

std::size_t FileTailer::restarts() const { return restarts_; }

std::uint64_t FileTailer::generation() const { return generation_; }

// A file smaller than what we already consumed was truncated or replaced, so
// the old offset is meaningless and reading restarts from the beginning.
bool FileTailer::detectRestart(std::uint64_t size) {
    if (size >= offset_) {
        return false;
    }
    offset_ = 0;
    ++restarts_;
    ++generation_;
    return true;
}

bool FileTailer::pollChunk(SourceChunk& out, std::string& error) {
    out.bytes.clear();
    out.generation_changed = false;
    out.generation = generation_;
    error.clear();
    std::error_code sizeEc;
    const std::uintmax_t size = fs::file_size(path_, sizeEc);
    if (sizeEc) {
        error = "cannot stat '" + path_ + "': " + sizeEc.message();
        return false;
    }
    out.generation_changed = detectRestart(static_cast<std::uint64_t>(size));
    out.generation = generation_;

    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        error = "cannot open '" + path_ + "'";
        return false;
    }
    try {
        const std::uint64_t start = offset_;
        input.seekg(static_cast<std::streamoff>(start));
        out.bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        if (input.bad()) {
            error = "error reading '" + path_ + "'";
            out.bytes.clear();
            return false;
        }
        // The file can grow while it is being read. Advance by bytes actually
        // consumed, not the size observed before opening, so the next poll
        // cannot skip a concurrent append.
        offset_ = start + static_cast<std::uint64_t>(out.bytes.size());
    } catch (const std::exception& ex) {
        // Report the failure instead of swallowing it; the caller decides.
        out.bytes.clear();
        error = std::string("error reading '") + path_ + "': " + ex.what();
        return false;
    }
    return true;
}

bool FileTailer::poll(std::vector<std::string>& out, std::string& error) {
    SourceChunk chunk;
    if (!pollChunk(chunk, error)) {
        return false;
    }

    // Keep the original line-oriented API for simple LogSource users and
    // existing tests. New follow-mode callers use pollChunk so incomplete
    // bytes are not mistaken for a complete record.
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
