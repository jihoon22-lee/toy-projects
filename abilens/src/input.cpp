#include "input_internal.hpp"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <limits>
#include <utility>

namespace abilens {
namespace detail {
namespace {

InputIdentity identity_from_stat(const struct stat& status) {
    InputIdentity identity;
    identity.device = static_cast<std::uint64_t>(status.st_dev);
    identity.inode = static_cast<std::uint64_t>(status.st_ino);
    identity.mode = static_cast<std::uint64_t>(status.st_mode);
    identity.size = static_cast<std::uint64_t>(status.st_size);
    identity.modified_seconds = static_cast<std::int64_t>(status.st_mtim.tv_sec);
    identity.modified_nanoseconds = static_cast<std::int64_t>(status.st_mtim.tv_nsec);
    identity.changed_seconds = static_cast<std::int64_t>(status.st_ctim.tv_sec);
    identity.changed_nanoseconds = static_cast<std::int64_t>(status.st_ctim.tv_nsec);
    return identity;
}

bool same_identity(const InputIdentity& left,
                   const InputIdentity& right) noexcept {
    return left.device == right.device && left.inode == right.inode &&
           left.mode == right.mode && left.size == right.size &&
           left.modified_seconds == right.modified_seconds &&
           left.modified_nanoseconds == right.modified_nanoseconds &&
           left.changed_seconds == right.changed_seconds &&
           left.changed_nanoseconds == right.changed_nanoseconds;
}

}  // namespace

OpenInput::OpenInput(const std::filesystem::path& path) : path_(path) {
    descriptor_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (descriptor_ < 0) {
        error_message_ = "could not open input";
        return;
    }
    if (descriptor_ <= STDERR_FILENO) {
        const int safe_descriptor = ::fcntl(descriptor_, F_DUPFD_CLOEXEC, 3);
        (void)::close(descriptor_);
        descriptor_ = safe_descriptor;
        if (descriptor_ < 0) {
            error_message_ = "could not reserve a safe input descriptor";
            return;
        }
    }
    struct stat status {};
    if (::fstat(descriptor_, &status) != 0) {
        error_message_ = "could not read input metadata";
        (void)::close(descriptor_);
        descriptor_ = -1;
        return;
    }
    if (!S_ISREG(status.st_mode)) {
        error_message_ = "input is not a readable regular file";
        (void)::close(descriptor_);
        descriptor_ = -1;
        return;
    }
    if (status.st_size < 0) {
        error_status_ = InputStatus::Unsupported;
        error_message_ = "input is larger than the supported address range";
        (void)::close(descriptor_);
        descriptor_ = -1;
        return;
    }
    identity_ = identity_from_stat(status);
    error_message_.clear();
}

OpenInput::~OpenInput() {
    if (descriptor_ >= 0) (void)::close(descriptor_);
}

OpenInput::OpenInput(OpenInput&& other) noexcept
    : path_(std::move(other.path_)),
      descriptor_(std::exchange(other.descriptor_, -1)),
      identity_(other.identity_),
      error_status_(other.error_status_),
      error_message_(std::move(other.error_message_)) {}

OpenInput& OpenInput::operator=(OpenInput&& other) noexcept {
    if (this == &other) return *this;
    if (descriptor_ >= 0) (void)::close(descriptor_);
    path_ = std::move(other.path_);
    descriptor_ = std::exchange(other.descriptor_, -1);
    identity_ = other.identity_;
    error_status_ = other.error_status_;
    error_message_ = std::move(other.error_message_);
    return *this;
}

bool OpenInput::opened() const noexcept { return descriptor_ >= 0; }

int OpenInput::descriptor() const noexcept { return descriptor_; }

std::uint64_t OpenInput::size() const noexcept { return identity_.size; }

InputStatus OpenInput::error_status() const noexcept { return error_status_; }

const std::string& OpenInput::error_message() const noexcept { return error_message_; }

std::string OpenInput::descriptor_path() const {
    return "/proc/self/fd/" + std::to_string(descriptor_);
}

bool OpenInput::read_exact(std::uint64_t offset, unsigned char* destination,
                           std::size_t bytes) const noexcept {
    const auto maximum_offset = static_cast<std::uint64_t>(std::numeric_limits<off_t>::max());
    if (!opened() || offset > maximum_offset || bytes > maximum_offset - offset) {
        return false;
    }
    std::size_t consumed = 0U;
    while (consumed < bytes) {
        const auto position = static_cast<off_t>(offset + consumed);
        const ssize_t count = ::pread(descriptor_, destination + consumed,
                                      bytes - consumed, position);
        if (count > 0) {
            consumed += static_cast<std::size_t>(count);
        } else if (count == 0) {
            return false;
        } else if (errno != EINTR) {
            return false;
        }
    }
    return true;
}

bool OpenInput::unchanged() const noexcept {
    if (!opened()) return false;
    struct stat descriptor_status {};
    struct stat path_status {};
    return ::fstat(descriptor_, &descriptor_status) == 0 &&
           ::stat(path_.c_str(), &path_status) == 0 && descriptor_status.st_size >= 0 &&
           path_status.st_size >= 0 &&
           same_identity(identity_, identity_from_stat(descriptor_status)) &&
           same_identity(identity_, identity_from_stat(path_status));
}

}  // namespace detail
}  // namespace abilens
