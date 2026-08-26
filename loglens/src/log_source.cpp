#include "loglens/log_source.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace loglens {

namespace fs = std::filesystem;

LogSource::~LogSource() = default;

FileTailer::FileTailer(std::string path) : path_(std::move(path)) {}

std::uint64_t FileTailer::offset() const { return offset_; }

std::size_t FileTailer::restarts() const { return restarts_; }

// A file smaller than what we already consumed was truncated or replaced, so
// the old offset is meaningless and reading restarts from the beginning.
bool FileTailer::detectRestart(std::uint64_t size) {
    if (size >= offset_) {
        return false;
    }
    offset_ = 0;
    ++restarts_;
    return true;
}

bool FileTailer::poll(std::vector<std::string>& out, std::string& error) {
    error.clear();
    std::error_code sizeEc;
    const std::uintmax_t size = fs::file_size(path_, sizeEc);
    if (sizeEc) {
        error = "cannot stat '" + path_ + "': " + sizeEc.message();
        return false;
    }
    detectRestart(static_cast<std::uint64_t>(size));

    std::ifstream input(path_, std::ios::binary);
    if (!input) {
        error = "cannot open '" + path_ + "'";
        return false;
    }
    try {
        input.seekg(static_cast<std::streamoff>(offset_));
        std::string line;
        while (std::getline(input, line)) {
            out.push_back(line);
        }
        offset_ = static_cast<std::uint64_t>(size);
    } catch (const std::exception& ex) {
        // Report the failure instead of swallowing it; the caller decides.
        error = std::string("error reading '") + path_ + "': " + ex.what();
        return false;
    }
    return true;
}

} // namespace loglens
