#include "diskmap/fs_source.hpp"

#include <exception>
#include <filesystem>
#include <system_error>

namespace diskmap {

namespace {

namespace fs = std::filesystem;

DirEntry makeDirEntry(const fs::directory_entry& entry) {
    DirEntry result;
    result.name = entry.path().filename().string();

    std::error_code symlinkEc;
    result.is_symlink = entry.is_symlink(symlinkEc) && !symlinkEc;

    std::error_code dirEc;
    result.is_dir = entry.is_directory(dirEc) && !dirEc;

    if (!result.is_dir) {
        std::error_code sizeEc;
        std::uintmax_t size = entry.file_size(sizeEc);
        result.size = sizeEc ? 0 : static_cast<std::uint64_t>(size);
    }
    return result;
}

} // namespace

FsSource::~FsSource() = default;

std::vector<DirEntry> RealFsSource::list(const std::string& path, std::string& error) const {
    error.clear();
    std::vector<DirEntry> entries;

    std::error_code openEc;
    fs::directory_iterator it(path, fs::directory_options::skip_permission_denied, openEc);
    if (openEc) {
        error = "cannot open directory '" + path + "': " + openEc.message();
        return entries;
    }

    try {
        const fs::directory_iterator end;
        std::error_code stepEc;
        for (; it != end; it.increment(stepEc)) {
            if (stepEc) {
                error = "error walking directory '" + path + "': " + stepEc.message();
                break;
            }
            entries.push_back(makeDirEntry(*it));
        }
    } catch (const std::exception& ex) {
        error = "exception while listing '" + path + "': " + ex.what();
        entries.clear();
    }

    return entries;
}

} // namespace diskmap
