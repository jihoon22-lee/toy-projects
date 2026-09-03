#ifndef _WIN32

#include "persistence_io_platform.hpp"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace loglens::detail {

namespace {

namespace fs = std::filesystem;

void setError(PersistenceError& error, PersistenceErrorCode code,
              const std::string& message, std::size_t offset = 0) {
    error.code = code;
    error.offset = offset;
    error.message = message;
}

class ScopedFd {
public:
    explicit ScopedFd(int descriptor = -1) : descriptor_(descriptor) {}
    ~ScopedFd() {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
    }

    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    int get() const { return descriptor_; }

    int release() {
        const int descriptor = descriptor_;
        descriptor_ = -1;
        return descriptor;
    }

    void reset(int descriptor = -1) {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
        descriptor_ = descriptor;
    }

private:
    int descriptor_ = -1;
};

std::string errnoMessage(const char* operation, int value = errno) {
    return std::string(operation) + ": " + std::strerror(value);
}

void setErrnoError(PersistenceError& error, PersistenceErrorCode code,
                  const char* operation, int value = errno) {
    setError(error, code, errnoMessage(operation, value));
}

int directoryOpenFlags() {
    int flags = O_RDONLY;
#ifdef O_DIRECTORY
    flags |= O_DIRECTORY;
#endif
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    return flags;
}

int targetOpenFlags() {
    int flags = O_RDONLY;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    return flags;
}

int temporaryOpenFlags() {
    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    return flags;
}

bool noFollowIsSupported(PersistenceError& error) {
#if !defined(O_NOFOLLOW) || !defined(AT_SYMLINK_NOFOLLOW)
    setError(error, PersistenceErrorCode::UnsafePath,
             "persistence I/O requires POSIX no-follow support");
    return false;
#else
    (void)error;
    return true;
#endif
}

bool unsafePathError(int value) {
    return value == ELOOP || value == ENOTDIR;
}

bool targetName(const fs::path& target, std::string& name,
                PersistenceError& error) {
    name = target.filename().string();
    if (name.empty() || name == "." || name == ".."
        || name.find('\0') != std::string::npos) {
        setError(error, PersistenceErrorCode::UnsafePath,
                 "persistence destination must have a safe filename");
        return false;
    }
    return true;
}

int openParentDirectory(const fs::path& parent, bool missingOkay, bool& missing,
                        PersistenceError& error) {
    missing = false;
    const int descriptor = ::open(parent.c_str(), directoryOpenFlags());
    if (descriptor < 0) {
        if (missingOkay && errno == ENOENT) {
            missing = true;
            return -1;
        }
        if (errno == ENOENT || unsafePathError(errno)) {
            setErrnoError(error, PersistenceErrorCode::UnsafePath,
                          "cannot open persistence parent directory");
        } else {
            setErrnoError(error, PersistenceErrorCode::Io,
                          "cannot open persistence parent directory");
        }
        return -1;
    }
    struct stat status {};
    if (::fstat(descriptor, &status) != 0) {
        const int savedError = errno;
        ::close(descriptor);
        setErrnoError(error, PersistenceErrorCode::Io,
                      "cannot inspect persistence parent directory", savedError);
        return -1;
    }
    if (!S_ISDIR(status.st_mode)) {
        ::close(descriptor);
        setError(error, PersistenceErrorCode::UnsafePath,
                 "persistence destination parent must be a real directory");
        return -1;
    }
    return descriptor;
}

bool readBoundedDescriptor(int descriptor, std::string& bytes,
                           PersistenceError& error) {
    struct stat initialStatus {};
    if (::fstat(descriptor, &initialStatus) != 0) {
        setErrnoError(error, PersistenceErrorCode::Io,
                      "cannot inspect persistence file");
        return false;
    }
    if (!S_ISREG(initialStatus.st_mode)) {
        setError(error, PersistenceErrorCode::UnsafePath,
                 "persistence file must be a regular non-symlink file");
        return false;
    }
    if (initialStatus.st_size < 0) {
        setError(error, PersistenceErrorCode::Io,
                 "persistence file has an invalid size");
        return false;
    }
    const std::uintmax_t size = static_cast<std::uintmax_t>(initialStatus.st_size);
    if (size > kMaxPersistenceFileBytes
        || size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        setError(error, PersistenceErrorCode::LimitExceeded,
                 "persistence file exceeds 4 MiB limit");
        return false;
    }
    const std::size_t expected = static_cast<std::size_t>(size);
    bytes.assign(expected, '\0');
    std::size_t offset = 0;
    while (offset < expected) {
        const ssize_t count = ::read(descriptor, bytes.data() + offset, expected - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            bytes.clear();
            setErrnoError(error, PersistenceErrorCode::Io,
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
    struct stat finalStatus {};
    if (::fstat(descriptor, &finalStatus) != 0) {
        bytes.clear();
        setErrnoError(error, PersistenceErrorCode::Io,
                      "cannot recheck persistence file");
        return false;
    }
    if (finalStatus.st_size != initialStatus.st_size) {
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
    if (!noFollowIsSupported(error)) {
        return false;
    }
    const fs::path target(path);
    std::string name;
    if (!targetName(target, name, error)) {
        return false;
    }
    fs::path parent = target.parent_path();
    if (parent.empty()) {
        parent = fs::path(".");
    }
    bool missingParent = false;
    ScopedFd parentDescriptor(
        openParentDirectory(parent, true, missingParent, error));
    if (missingParent) {
        return true;
    }
    if (parentDescriptor.get() < 0) {
        return false;
    }
    found = true;
    const int descriptor = ::openat(parentDescriptor.get(), name.c_str(),
                                    targetOpenFlags());
    if (descriptor < 0) {
        if (errno == ENOENT) {
            found = false;
            return true;
        }
        if (unsafePathError(errno)) {
            setErrnoError(error, PersistenceErrorCode::UnsafePath,
                          "cannot open persistence file without following links");
        } else {
            setErrnoError(error, PersistenceErrorCode::Io,
                          "cannot open persistence file");
        }
        return false;
    }
    ScopedFd sourceDescriptor(descriptor);
    return readBoundedDescriptor(sourceDescriptor.get(), bytes, error);
}

int noFollowStatFlags() {
#ifdef AT_SYMLINK_NOFOLLOW
    return AT_SYMLINK_NOFOLLOW;
#else
    return 0;
#endif
}

bool validDestinationAt(int parentDescriptor, const std::string& name,
                        PersistenceError& error) {
    struct stat status {};
    if (::fstatat(parentDescriptor, name.c_str(), &status, noFollowStatFlags()) != 0) {
        if (errno == ENOENT) {
            return true;
        }
        setErrnoError(error, PersistenceErrorCode::Io,
                      "cannot inspect persistence destination");
        return false;
    }
    if (!S_ISREG(status.st_mode)) {
        setError(error, PersistenceErrorCode::UnsafePath,
                 "persistence destination must be a regular non-symlink file");
        return false;
    }
    return true;
}

std::uint64_t processToken() {
    return static_cast<std::uint64_t>(::getpid());
}

bool createTemporaryFile(int parentDescriptor, const std::string& target,
                         std::string& name, ScopedFd& descriptor,
                         PersistenceError& error) {
    static std::atomic<std::uint64_t> sequence{0};
    constexpr unsigned kAttempts = 128;
    for (unsigned attempt = 0; attempt < kAttempts; ++attempt) {
        const std::uint64_t token = sequence.fetch_add(1, std::memory_order_relaxed);
        name = target + ".tmp." + std::to_string(processToken()) + "."
               + std::to_string(token);
        const int rawDescriptor =
            ::openat(parentDescriptor, name.c_str(), temporaryOpenFlags(), 0600);
        if (rawDescriptor >= 0) {
            descriptor.reset(rawDescriptor);
            return true;
        }
        if (errno == EEXIST) {
            continue;
        }
        if (unsafePathError(errno)) {
            setErrnoError(error, PersistenceErrorCode::UnsafePath,
                          "cannot create persistence temporary file");
        } else {
            setErrnoError(error, PersistenceErrorCode::Io,
                          "cannot create persistence temporary file");
        }
        return false;
    }
    setError(error, PersistenceErrorCode::AtomicReplace,
             "could not create a unique persistence temporary file");
    return false;
}

void removeTemporaryFile(int parentDescriptor, const std::string& name) {
    if (!name.empty()) {
        ::unlinkat(parentDescriptor, name.c_str(), 0);
    }
}

bool writeTemporaryFile(ScopedFd& descriptor, std::string_view bytes,
                        PersistenceError& error) {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count =
            ::write(descriptor.get(), bytes.data() + offset, bytes.size() - offset);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            setErrnoError(error, PersistenceErrorCode::Io,
                          "cannot write persistence temporary file");
            return false;
        }
        if (count == 0) {
            setError(error, PersistenceErrorCode::Io,
                     "cannot write persistence temporary file");
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

bool closeTemporaryFile(ScopedFd& descriptor, PersistenceError& error) {
    const int rawDescriptor = descriptor.release();
    if (rawDescriptor < 0 || ::close(rawDescriptor) == 0) {
        return true;
    }
    setErrnoError(error, PersistenceErrorCode::Io,
                  "cannot close persistence temporary file");
    return false;
}

bool atomicWrite(const std::string& path, std::string_view bytes,
                 PersistenceError& error) {
    if (path.empty()) {
        setError(error, PersistenceErrorCode::UnsafePath,
                 "persistence path must not be empty");
        return false;
    }
    if (!noFollowIsSupported(error)) {
        return false;
    }
    if (bytes.size() > kMaxPersistenceFileBytes) {
        setError(error, PersistenceErrorCode::LimitExceeded,
                 "serialized persistence file exceeds 4 MiB limit");
        return false;
    }
    const fs::path target(path);
    std::string targetFilename;
    if (!targetName(target, targetFilename, error)) {
        return false;
    }
    fs::path parent = target.parent_path();
    if (parent.empty()) {
        parent = fs::path(".");
    }
    bool missingParent = false;
    ScopedFd parentDescriptor(
        openParentDirectory(parent, false, missingParent, error));
    if (missingParent) {
        setError(error, PersistenceErrorCode::UnsafePath,
                 "persistence destination parent directory is missing");
        return false;
    }
    if (parentDescriptor.get() < 0) {
        return false;
    }
    if (!validDestinationAt(parentDescriptor.get(), targetFilename, error)) {
        return false;
    }

    std::string temporaryName;
    ScopedFd temporaryDescriptor;
    if (!createTemporaryFile(parentDescriptor.get(), targetFilename, temporaryName,
                             temporaryDescriptor, error)) {
        return false;
    }
    if (!writeTemporaryFile(temporaryDescriptor, bytes, error)) {
        temporaryDescriptor.reset();
        removeTemporaryFile(parentDescriptor.get(), temporaryName);
        return false;
    }
    if (!closeTemporaryFile(temporaryDescriptor, error)) {
        removeTemporaryFile(parentDescriptor.get(), temporaryName);
        return false;
    }
    if (::renameat(parentDescriptor.get(), temporaryName.c_str(), parentDescriptor.get(),
                   targetFilename.c_str()) != 0) {
        const int savedError = errno;
        removeTemporaryFile(parentDescriptor.get(), temporaryName);
        setErrnoError(error, PersistenceErrorCode::AtomicReplace,
                      "cannot atomically replace persistence file", savedError);
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
