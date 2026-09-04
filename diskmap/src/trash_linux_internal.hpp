#pragma once

#include "trash_internal.hpp"

#if defined(__linux__)

#include <sys/stat.h>

namespace diskmap {
namespace detail {

class FileDescriptor {
public:
    explicit FileDescriptor(int value = -1);
    ~FileDescriptor();
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept;
    FileDescriptor& operator=(FileDescriptor&& other) noexcept;

    int get() const;
    explicit operator bool() const;

private:
    int value_;
};

struct TrashDirectories {
    fs::path root;
    FileDescriptor root_directory;
    FileDescriptor files;
    FileDescriptor info;
};

// Internal deterministic race seam used by the real-filesystem regression
// tests. Production callers never install a hook; thread-local storage keeps
// one test's mutation from affecting another worker.
enum class TrashMutationTestPoint {
    BeforeMetadataReserve,
    BeforePayloadMove,
    PayloadMoved,
    MetadataFinalized,
    BeforeMoveRollback,
    BeforePayloadRestore,
    PayloadRestored,
};
using TrashMutationTestHook = void (*)(TrashMutationTestPoint,
                                       const fs::path& original,
                                       const fs::path& trashed) noexcept;
void setTrashMutationTestHook(TrashMutationTestHook hook) noexcept;
void notifyTrashMutationTestHook(TrashMutationTestPoint point,
                                 const fs::path& original,
                                 const fs::path& trashed) noexcept;

FileDescriptor openAbsoluteDirectory(const fs::path& input, std::string& error);
FileDescriptor openOrCreateAbsoluteDirectory(const fs::path& input,
                                             std::string& error);
bool inspectAbsoluteDirectoryPath(const fs::path& input,
                                  FileDescriptor& closest,
                                  bool& complete,
                                  std::string& error);
bool directoryWritable(const FileDescriptor& directory, std::string& error);
bool directoryOwnedByEffectiveUser(const FileDescriptor& directory,
                                   std::string& error);
bool directoryPathMatches(const fs::path& path,
                          const FileDescriptor& directory,
                          std::string& error);
bool trashDirectoriesAnchored(const TrashDirectories& directories,
                              std::string& error);
FsKind kindFromMode(mode_t mode);
std::uint64_t allocatedBytes(const struct stat& status);
TrashStatus validateStat(const CleanupTarget& target,
                         const struct stat& status,
                         std::string& message);
bool sameDevice(const FileDescriptor& directory,
                std::uint64_t expected,
                std::string& error);
bool secureOwnedDirectory(const FileDescriptor& directory, std::string& error);
int renameNoReplace(int sourceDirectory,
                    const char* source,
                    int targetDirectory,
                    const char* target);
bool writeAll(int descriptor, const std::string& content, std::string& error);
bool readBounded(int descriptor, std::string& content, std::string& error);
bool openTrashDirectories(const TrashOptions& options,
                          bool create,
                          TrashDirectories& directories,
                          std::string& error);

} // namespace detail
} // namespace diskmap

#endif
