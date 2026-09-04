#ifdef _WIN32

#include "persistence_io_platform.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

#    define NOMINMAX
#    include <fcntl.h>
#    include <io.h>
#    include <sys/stat.h>
#    include <windows.h>

namespace loglens::detail {

namespace {

namespace fs = std::filesystem;

void setError(PersistenceError& error, PersistenceErrorCode code,
              const std::string& message, std::size_t offset = 0) {
    error.code = code;
    error.offset = offset;
    error.message = message;
}

void setWindowsError(PersistenceError& error, PersistenceErrorCode code,
                     const char* operation, DWORD value = ::GetLastError()) {
    setError(error, code,
             std::string(operation) + ": "
                 + std::system_category().message(static_cast<int>(value)));
}

class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}
    ~ScopedHandle() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
        }
    }

    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;

    HANDLE get() const { return handle_; }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

bool readBoundedHandle(HANDLE handle, std::string& bytes,
                       PersistenceError& error) {
    BY_HANDLE_FILE_INFORMATION information{};
    if (!::GetFileInformationByHandle(handle, &information)) {
        setWindowsError(error, PersistenceErrorCode::Io,
                        "cannot inspect persistence file");
        return false;
    }
    if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0
        || (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        setError(error, PersistenceErrorCode::UnsafePath,
                 "persistence file must be a regular non-reparse file");
        return false;
    }
    LARGE_INTEGER initialSize{};
    if (!::GetFileSizeEx(handle, &initialSize) || initialSize.QuadPart < 0) {
        setWindowsError(error, PersistenceErrorCode::Io,
                        "cannot read persistence file size");
        return false;
    }
    const std::uint64_t size = static_cast<std::uint64_t>(initialSize.QuadPart);
    if (size > kMaxPersistenceFileBytes
        || size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        setError(error, PersistenceErrorCode::LimitExceeded,
                 "persistence file exceeds 4 MiB limit");
        return false;
    }
    const std::size_t expected = static_cast<std::size_t>(size);
    bytes.assign(expected, '\0');
    std::size_t offset = 0;
    while (offset < expected) {
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            expected - offset, std::numeric_limits<DWORD>::max()));
        DWORD count = 0;
        if (!::ReadFile(handle, bytes.data() + offset, requested, &count, nullptr)) {
            bytes.clear();
            setWindowsError(error, PersistenceErrorCode::Io,
                            "cannot read complete persistence file");
            return false;
        }
        if (count == 0) {
            bytes.clear();
            setError(error, PersistenceErrorCode::Io,
                     "persistence file changed while it was being read");
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    LARGE_INTEGER finalSize{};
    if (!::GetFileSizeEx(handle, &finalSize)
        || finalSize.QuadPart != initialSize.QuadPart) {
        bytes.clear();
        setError(error, PersistenceErrorCode::Io,
                 "persistence file changed while it was being read");
        return false;
    }
    return true;
}

bool readBoundedFile(const std::string& path, std::string& bytes, bool& found,
                     PersistenceError& error) {
    bytes.clear();
    found = false;
    if (path.empty()) {
        setError(error, PersistenceErrorCode::UnsafePath,
                 "persistence path must not be empty");
        return false;
    }
    const fs::path target(path);
    const DWORD access = GENERIC_READ;
    const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    const DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT
                        | FILE_FLAG_BACKUP_SEMANTICS;
    const HANDLE rawHandle = ::CreateFileW(target.c_str(), access, share, nullptr,
                                           OPEN_EXISTING, flags, nullptr);
    if (rawHandle == INVALID_HANDLE_VALUE) {
        const DWORD code = ::GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        if (code == ERROR_CANT_ACCESS_FILE || code == ERROR_INVALID_REPARSE_DATA) {
            setWindowsError(error, PersistenceErrorCode::UnsafePath,
                            "cannot open persistence file without following links", code);
        } else {
            setWindowsError(error, PersistenceErrorCode::Io,
                            "cannot open persistence file", code);
        }
        return false;
    }
    found = true;
    ScopedHandle handle(rawHandle);
    return readBoundedHandle(handle.get(), bytes, error);
}

std::uint64_t processToken() {
    return static_cast<std::uint64_t>(::GetCurrentProcessId());
}

bool validParentDirectory(const fs::path& parent, PersistenceError& error) {
    std::error_code statusError;
    const fs::file_status parentStatus = fs::symlink_status(parent, statusError);
    if (statusError || parentStatus.type() != fs::file_type::directory
        || parentStatus.type() == fs::file_type::symlink) {
        setError(error, PersistenceErrorCode::UnsafePath,
                 "persistence destination parent must be a real directory");
        return false;
    }
    return true;
}

bool validDestination(const fs::path& target, PersistenceError& error) {
    std::error_code targetError;
    const fs::file_status targetStatus = fs::symlink_status(target, targetError);
    if (targetError && targetError != std::errc::no_such_file_or_directory) {
        setError(error, PersistenceErrorCode::Io,
                 "cannot inspect persistence destination: " + targetError.message());
        return false;
    }
    if (!targetError && targetStatus.type() != fs::file_type::not_found
        && (targetStatus.type() == fs::file_type::symlink
            || targetStatus.type() != fs::file_type::regular)) {
        setError(error, PersistenceErrorCode::UnsafePath,
                 "persistence destination must be a regular non-symlink file");
        return false;
    }
    return true;
}

std::string makeTemporaryPath(const fs::path& parent, const fs::path& target,
                              PersistenceError& error) {
    static std::atomic<std::uint64_t> sequence{0};
    const std::string filename = target.filename().string();
    if (filename.empty()) {
        setError(error, PersistenceErrorCode::UnsafePath,
                 "persistence destination must have a filename");
        return {};
    }
    const fs::path temporary =
        parent / (filename + ".tmp." + std::to_string(processToken()) + "."
                  + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
    std::error_code statusError;
    const fs::file_status status = fs::symlink_status(temporary, statusError);
    if (!statusError && status.type() != fs::file_type::not_found) {
        setError(error, PersistenceErrorCode::AtomicReplace,
                 "temporary persistence path already exists");
        return {};
    }
    return temporary.string();
}

bool writeTemporaryFile(const fs::path& temporary, std::string_view bytes,
                        PersistenceError& error) {
    const int flags = _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY;
    const int descriptor =
        ::_wopen(temporary.c_str(), flags, _S_IREAD | _S_IWRITE);
    if (descriptor < 0) {
        if (errno == EEXIST) {
            setError(error, PersistenceErrorCode::AtomicReplace,
                     "temporary persistence path already exists");
        } else {
            setError(error, PersistenceErrorCode::Io,
                     "cannot create temporary persistence file: "
                         + std::strerror(errno));
        }
        return false;
    }
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const unsigned int count = static_cast<unsigned int>(std::min<std::size_t>(
            bytes.size() - offset, std::numeric_limits<unsigned int>::max()));
        const int written =
            ::_write(descriptor, bytes.data() + offset, count);
        if (written <= 0) {
            ::_close(descriptor);
            std::error_code cleanupError;
            fs::remove(temporary, cleanupError);
            setError(error, PersistenceErrorCode::Io,
                     "cannot write temporary persistence file: "
                         + std::strerror(errno));
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (::_close(descriptor) != 0) {
        std::error_code cleanupError;
        fs::remove(temporary, cleanupError);
        setError(error, PersistenceErrorCode::Io,
                 "cannot close temporary persistence file: "
                     + std::strerror(errno));
        return false;
    }
    return true;
}

bool replaceFile(const fs::path& temporary, const fs::path& target,
                 PersistenceError& error) {
    if (::MoveFileExW(temporary.c_str(), target.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    const DWORD code = ::GetLastError();
    setError(error, PersistenceErrorCode::AtomicReplace,
             "cannot atomically replace persistence file: "
                 + std::system_category().message(static_cast<int>(code)));
    return false;
}

bool atomicWrite(const std::string& path, std::string_view bytes,
                 PersistenceError& error) {
    if (path.empty()) {
        setError(error, PersistenceErrorCode::UnsafePath,
                 "persistence path must not be empty");
        return false;
    }
    if (bytes.size() > kMaxPersistenceFileBytes) {
        setError(error, PersistenceErrorCode::LimitExceeded,
                 "serialized persistence file exceeds 4 MiB limit");
        return false;
    }
    const fs::path target(path);
    fs::path parent = target.parent_path();
    if (parent.empty()) {
        parent = fs::path(".");
    }
    if (!validParentDirectory(parent, error) || !validDestination(target, error)) {
        return false;
    }
    const fs::path temporary = makeTemporaryPath(parent, target, error);
    if (temporary.empty() || !writeTemporaryFile(temporary, bytes, error)) {
        return false;
    }
    if (!replaceFile(temporary, target, error)) {
        std::error_code cleanupError;
        fs::remove(temporary, cleanupError);
        return false;
    }
    return true;
}

} // namespace

bool readBoundedPersistenceFilePlatform(const std::string& path, std::string& bytes,
                                        bool& found, PersistenceError& error) {
    return readBoundedFile(path, bytes, found, error);
}

bool atomicWritePersistenceFilePlatform(const std::string& path, std::string_view bytes,
                                       PersistenceError& error) {
    return atomicWrite(path, bytes, error);
}

} // namespace loglens::detail

#endif
