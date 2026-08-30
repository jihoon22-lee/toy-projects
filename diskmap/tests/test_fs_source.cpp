// Tests for diskmap::RealFsSource and its identity-preserving metadata
// adapter.  The fixture deliberately exercises lstat/stat differences,
// rather than treating every directory entry as a followed file.

#include "assert.hpp"
#include "diskmap/fs_source.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <unistd.h>
#endif

using diskmap::DirEntry;
using diskmap::FsKind;
using diskmap::FsMetadata;
using diskmap::FsSource;
using diskmap::RealFsSource;

namespace fs = std::filesystem;

namespace {

// create_directory is an atomic uniqueness check.  A fixed directory name
// plus remove_all would let two concurrent test processes destroy one
// another's fixtures, so this helper owns exactly one successfully-created
// directory and retries collisions.
class ScopedTempDirectory {
public:
    ScopedTempDirectory() {
        std::error_code ec;
        const fs::path parent = fs::temp_directory_path(ec);
        if (ec) {
            return;
        }

        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt != 100; ++attempt) {
            const fs::path candidate =
                parent / ("diskmap_fs_source_test_" + std::to_string(stamp) + "_" +
                          std::to_string(attempt));
            ec.clear();
            if (fs::create_directory(candidate, ec)) {
                path_ = candidate;
                return;
            }
            // A collision is safe to retry.  Some implementations report an
            // existing path without setting an error code; that is also a
            // collision and must not make us reuse it.
            if (!ec || ec == std::make_error_code(std::errc::file_exists)) {
                continue;
            }
            return;
        }
    }

    ~ScopedTempDirectory() {
        if (path_.empty()) {
            return;
        }
        std::error_code ec;
        // std::filesystem::remove_all removes a symlink directory entry; it
        // does not follow the link to its target.  This keeps cleanup scoped
        // to the fixture even when the fixture contains symlinks.
        fs::remove_all(path_, ec);
    }

    bool valid() const { return !path_.empty(); }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

const DirEntry* findEntry(const std::vector<DirEntry>& entries, const std::string& name) {
    for (const DirEntry& entry : entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

bool writeFile(const fs::path& path, const std::string& contents) {
    std::ofstream out(path.string(), std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return out.good();
}

bool makeSparseFile(const fs::path& path, std::uint64_t logicalSize) {
    if (logicalSize == 0) {
        return false;
    }
    std::ofstream out(path.string(), std::ios::binary);
    if (!out) {
        return false;
    }
    out.seekp(static_cast<std::streamoff>(logicalSize - 1), std::ios::beg);
    out.put('\0');
    return out.good();
}

void checkComplete(const FsMetadata& metadata) {
    CHECK(metadata.complete);
    CHECK(metadata.error.empty());
    CHECK(metadata.identity.valid);
    CHECK(metadata.hard_link_count_known);
    CHECK(metadata.permissions_known);
    CHECK(metadata.ownership_known);
    CHECK(metadata.modified_time_known);
}

// Compare the adapter result with the OS stat record.  On non-POSIX hosts
// this helper is intentionally a no-op; the portable kind/path/size checks
// still run, while the contract's POSIX fields are covered where they exist.
void checkPosixStat(const FsMetadata& metadata, const fs::path& path, bool follow) {
#if defined(__unix__) || defined(__APPLE__)
    struct stat statResult {
    };
    const int result = follow ? ::stat(path.c_str(), &statResult) : ::lstat(path.c_str(), &statResult);
    CHECK_EQ(result, 0);
    if (result != 0) {
        return;
    }

    CHECK_EQ(metadata.identity.device, static_cast<std::uint64_t>(statResult.st_dev));
    CHECK_EQ(metadata.identity.file, static_cast<std::uint64_t>(statResult.st_ino));
    CHECK(metadata.identity.valid);
    CHECK_EQ(metadata.permissions,
             static_cast<std::uint32_t>(statResult.st_mode & static_cast<mode_t>(07777)));
    CHECK_EQ(metadata.owner, static_cast<std::uint64_t>(statResult.st_uid));
    CHECK_EQ(metadata.group, static_cast<std::uint64_t>(statResult.st_gid));

#if defined(__APPLE__)
    const std::int64_t seconds = static_cast<std::int64_t>(statResult.st_mtimespec.tv_sec);
    const std::int64_t nanos = static_cast<std::int64_t>(statResult.st_mtimespec.tv_nsec);
#else
    const std::int64_t seconds = static_cast<std::int64_t>(statResult.st_mtim.tv_sec);
    const std::int64_t nanos = static_cast<std::int64_t>(statResult.st_mtim.tv_nsec);
#endif
    CHECK_EQ(metadata.modified_ns, seconds * static_cast<std::int64_t>(1000000000) + nanos);
#else
    (void)metadata;
    (void)path;
    (void)follow;
#endif
}

int runTests() {
    ScopedTempDirectory temp;
    CHECK(temp.valid());
    if (!temp.valid()) {
        return 0;
    }

    const fs::path base = temp.path();
    const fs::path subdirPath = base / "subdir";
    const fs::path emptyDirPath = base / "empty";
    const fs::path filePath = base / "file.txt";
    const fs::path hardlinkPath = base / "hardlink.txt";
    const fs::path sparsePath = base / "sparse.bin";
    const fs::path symlinkPath = base / "link_to_file";
    const fs::path directorySymlinkPath = base / "link_to_subdir";
    const fs::path danglingPath = base / "dangling_link";
    const fs::path danglingTargetPath = base / "missing_target";
#if defined(__unix__) || defined(__APPLE__)
    const fs::path fifoPath = base / "named-pipe";
#endif

    std::error_code ec;
    const bool madeSubdir = fs::create_directories(subdirPath, ec);
    CHECK(madeSubdir);
    CHECK(!ec);
    ec.clear();
    const bool madeEmptyDir = fs::create_directory(emptyDirPath, ec);
    CHECK(madeEmptyDir);
    CHECK(!ec);

    const std::string fileContents = "hello";
    const std::string nestedContents = "nested-file-contents";
    CHECK(writeFile(filePath, fileContents));
    CHECK(writeFile(subdirPath / "nested.txt", nestedContents));

#if defined(__unix__) || defined(__APPLE__)
    // Make the expected POSIX permission bits deterministic without removing
    // the owner read/write access needed by the test process.
    ec.clear();
    fs::permissions(filePath, fs::perms::owner_read | fs::perms::owner_write |
                                  fs::perms::group_read,
                    fs::perm_options::replace, ec);
    CHECK(!ec);
#endif

    bool hardLinkSupported = false;
    std::error_code hardLinkEc;
    fs::create_hard_link(filePath, hardlinkPath, hardLinkEc);
    if (!hardLinkEc) {
        hardLinkSupported = true;
    }

    constexpr std::uint64_t sparseLogicalSize = (static_cast<std::uint64_t>(1) << 20) + 17;
    const bool sparseSupported = makeSparseFile(sparsePath, sparseLogicalSize);
    CHECK(sparseSupported);

    std::error_code symlinkEc;
    fs::create_symlink(filePath, symlinkPath, symlinkEc);
    const bool symlinkSupported = !symlinkEc;

    bool directorySymlinkSupported = false;
    if (symlinkSupported) {
        symlinkEc.clear();
        fs::create_directory_symlink(subdirPath, directorySymlinkPath, symlinkEc);
        directorySymlinkSupported = !symlinkEc;
    }

#if defined(__unix__) || defined(__APPLE__)
    bool fifoSupported = ::mkfifo(fifoPath.c_str(), 0600) == 0;
#else
    const bool fifoSupported = false;
#endif

    bool danglingSymlinkSupported = false;
    if (symlinkSupported) {
        symlinkEc.clear();
        fs::create_symlink(danglingTargetPath, danglingPath, symlinkEc);
        danglingSymlinkSupported = !symlinkEc;
    }

    RealFsSource source;

    // --- ordinary files and directories: complete metadata and full paths ---
    std::string error;
    const std::vector<DirEntry> entries = source.list(base.string(), error);
    CHECK(error.empty());

    const DirEntry* fileEntry = findEntry(entries, "file.txt");
    CHECK(fileEntry != nullptr);
    if (fileEntry != nullptr) {
        CHECK_EQ(fileEntry->path, filePath);
        CHECK(!fileEntry->is_dir);
        CHECK(!fileEntry->is_symlink);
        CHECK_EQ(fileEntry->size, static_cast<std::uint64_t>(fileContents.size()));
        CHECK_EQ(fileEntry->metadata.kind, FsKind::RegularFile);
        CHECK_EQ(fileEntry->metadata.logical_size,
                 static_cast<std::uint64_t>(fileContents.size()));
        checkComplete(fileEntry->metadata);
        checkPosixStat(fileEntry->metadata, filePath, false);
        CHECK(fileEntry->metadata.hard_link_count >= (hardLinkSupported ? 2U : 1U));
    }

    const DirEntry* subEntry = findEntry(entries, "subdir");
    CHECK(subEntry != nullptr);
    if (subEntry != nullptr) {
        CHECK_EQ(subEntry->path, subdirPath);
        CHECK(subEntry->is_dir);
        CHECK(!subEntry->is_symlink);
        CHECK_EQ(subEntry->metadata.kind, FsKind::Directory);
        checkComplete(subEntry->metadata);
        checkPosixStat(subEntry->metadata, subdirPath, false);
        CHECK(subEntry->metadata.hard_link_count >= 1U);
    }

    // Listing a nested directory must retain the complete path, not only the
    // basename used by the compatibility field.
    std::string nestedError;
    const std::vector<DirEntry> nestedEntries = source.list(subdirPath.string(), nestedError);
    CHECK(nestedError.empty());
    const DirEntry* nestedEntry = findEntry(nestedEntries, "nested.txt");
    CHECK(nestedEntry != nullptr);
    if (nestedEntry != nullptr) {
        CHECK_EQ(nestedEntry->path, subdirPath / "nested.txt");
        CHECK_EQ(nestedEntry->metadata.kind, FsKind::RegularFile);
        CHECK_EQ(nestedEntry->metadata.logical_size,
                 static_cast<std::uint64_t>(nestedContents.size()));
        checkComplete(nestedEntry->metadata);
        checkPosixStat(nestedEntry->metadata, subdirPath / "nested.txt", false);
    }

    // An empty directory exercises the iterator's natural end condition and
    // keeps the source contract explicit for directories with no entries.
    std::string emptyError;
    const std::vector<DirEntry> emptyEntries = source.list(emptyDirPath, emptyError);
    CHECK(emptyEntries.empty());
    CHECK(emptyError.empty());

    // POSIX special files are not regular files, directories, or symlinks;
    // they remain visible as FsKind::Other with no fabricated size.
    if (fifoSupported) {
        const std::vector<DirEntry> fifoParentEntries = source.list(base, error);
        CHECK(error.empty());
        const DirEntry* fifoEntry = findEntry(fifoParentEntries, "named-pipe");
        CHECK(fifoEntry != nullptr);
        if (fifoEntry != nullptr) {
            CHECK_EQ(fifoEntry->metadata.kind, FsKind::Other);
            CHECK(!fifoEntry->is_dir);
            CHECK(!fifoEntry->is_symlink);
            CHECK_EQ(fifoEntry->size, static_cast<std::uint64_t>(0));
        }
    }

    // --- hard links: one physical identity and an nlink count of at least 2 ---
    if (hardLinkSupported) {
        const DirEntry* first = findEntry(entries, "file.txt");
        const DirEntry* second = findEntry(entries, "hardlink.txt");
        CHECK(first != nullptr);
        CHECK(second != nullptr);
        if (first != nullptr && second != nullptr) {
            CHECK_EQ(second->path, hardlinkPath);
            CHECK_EQ(first->metadata.kind, FsKind::RegularFile);
            CHECK_EQ(second->metadata.kind, FsKind::RegularFile);
            CHECK(first->metadata.identity == second->metadata.identity);
            CHECK(first->metadata.identity.valid);
            CHECK(second->metadata.identity.valid);
            CHECK(first->metadata.hard_link_count >= 2U);
            CHECK(second->metadata.hard_link_count >= 2U);
            checkPosixStat(second->metadata, hardlinkPath, false);
        }
    }

    // A directory symlink keeps its own lstat identity while exposing the
    // followed directory metadata used by the scanner's traversal decision.
    if (directorySymlinkSupported) {
        const DirEntry* linkEntry = findEntry(entries, "link_to_subdir");
        CHECK(linkEntry != nullptr);
        if (linkEntry != nullptr) {
            CHECK_EQ(linkEntry->path, directorySymlinkPath);
            CHECK(linkEntry->is_symlink);
            CHECK(linkEntry->is_dir);
            CHECK_EQ(linkEntry->metadata.kind, FsKind::Symlink);
            CHECK(linkEntry->metadata.identity.valid);
            CHECK(linkEntry->has_target_metadata);
            if (linkEntry->has_target_metadata) {
                CHECK_EQ(linkEntry->target_metadata.kind, FsKind::Directory);
                CHECK(linkEntry->target_metadata.identity.valid);
                CHECK(linkEntry->target_metadata.identity != linkEntry->metadata.identity);
            }
        }
    }

    // --- sparse file: always assert logical size; allocation is conditional ---
    const DirEntry* sparseEntry = findEntry(entries, "sparse.bin");
    CHECK(sparseEntry != nullptr);
    if (sparseEntry != nullptr && sparseSupported) {
        CHECK_EQ(sparseEntry->metadata.kind, FsKind::RegularFile);
        CHECK_EQ(sparseEntry->metadata.logical_size, sparseLogicalSize);
        checkComplete(sparseEntry->metadata);
        checkPosixStat(sparseEntry->metadata, sparsePath, false);
        // Unknown allocation must use the zero/default value.  When the
        // platform exposes allocation, a sparse file must not be reported as
        // consuming more bytes than its logical extent.
        CHECK(sparseEntry->metadata.allocated_size_known ||
              sparseEntry->metadata.allocated_size == 0U);
        if (sparseEntry->metadata.allocated_size_known) {
            CHECK(sparseEntry->metadata.allocated_size <= sparseLogicalSize);
        }
    }

    // --- valid symlink: lstat metadata and followed target metadata differ ---
    if (symlinkSupported) {
        const DirEntry* linkEntry = findEntry(entries, "link_to_file");
        CHECK(linkEntry != nullptr);
        if (linkEntry != nullptr) {
            CHECK_EQ(linkEntry->path, symlinkPath);
            CHECK(linkEntry->is_symlink);
            CHECK(!linkEntry->is_dir);
            CHECK_EQ(linkEntry->metadata.kind, FsKind::Symlink);
            checkComplete(linkEntry->metadata);
            checkPosixStat(linkEntry->metadata, symlinkPath, false);
            if (fileEntry != nullptr) {
                CHECK(linkEntry->metadata.identity != fileEntry->metadata.identity);
            }

            std::error_code readLinkEc;
            const fs::path linkTarget = fs::read_symlink(symlinkPath, readLinkEc);
            CHECK(!readLinkEc);
            if (!readLinkEc) {
                CHECK_EQ(linkEntry->metadata.logical_size,
                         static_cast<std::uint64_t>(linkTarget.string().size()));
            }

            CHECK(linkEntry->has_target_metadata);
            if (linkEntry->has_target_metadata) {
                CHECK_EQ(linkEntry->target_metadata.kind, FsKind::RegularFile);
                checkComplete(linkEntry->target_metadata);
                checkPosixStat(linkEntry->target_metadata, filePath, true);
                if (fileEntry != nullptr) {
                    CHECK(linkEntry->target_metadata.identity == fileEntry->metadata.identity);
                }
                CHECK(linkEntry->target_metadata.identity != linkEntry->metadata.identity);
                CHECK_EQ(linkEntry->target_metadata.logical_size,
                         static_cast<std::uint64_t>(fileContents.size()));
            }
        }
    }

    // --- dangling symlink: complete link record, incomplete target record ---
    if (danglingSymlinkSupported) {
        const DirEntry* danglingEntry = findEntry(entries, "dangling_link");
        CHECK(danglingEntry != nullptr);
        if (danglingEntry != nullptr) {
            CHECK_EQ(danglingEntry->path, danglingPath);
            CHECK(danglingEntry->is_symlink);
            CHECK_EQ(danglingEntry->metadata.kind, FsKind::Symlink);
            checkComplete(danglingEntry->metadata);
            checkPosixStat(danglingEntry->metadata, danglingPath, false);
            CHECK(!danglingEntry->has_target_metadata);
            CHECK(!danglingEntry->target_metadata.complete);
            CHECK(!danglingEntry->target_metadata.identity.valid);
            CHECK(!danglingEntry->target_metadata.error.empty());
        }
    }

    // --- source-level failure remains explicit and does not throw ---
    std::string missingError;
    const std::vector<DirEntry> missingEntries =
        source.list((base / "does_not_exist_at_all").string(), missingError);
    CHECK(missingEntries.empty());
    CHECK(!missingError.empty());

    // The same non-throwing error contract applies when a path exists but is
    // not a directory, and to both metadata lookup modes.
    std::string fileListingError;
    const std::vector<DirEntry> fileListing = source.list(filePath, fileListingError);
    CHECK(fileListing.empty());
    CHECK(!fileListingError.empty());

    const FsMetadata missingLstat = source.inspect(base / "missing-metadata", false);
    CHECK(!missingLstat.complete);
    CHECK(!missingLstat.identity.valid);
    CHECK(!missingLstat.error.empty());
    const FsMetadata missingStat = source.inspect(base / "missing-metadata", true);
    CHECK(!missingStat.complete);
    CHECK(!missingStat.identity.valid);
    CHECK(!missingStat.error.empty());

    if (symlinkSupported) {
        const FsMetadata linkLstat = source.inspect(symlinkPath, false);
        const FsMetadata linkStat = source.inspect(symlinkPath, true);
        CHECK(linkLstat.complete);
        CHECK(linkStat.complete);
        CHECK_EQ(linkLstat.kind, FsKind::Symlink);
        CHECK_EQ(linkStat.kind, FsKind::RegularFile);
        CHECK(linkLstat.identity != linkStat.identity);
    }

    // --- virtual dispatch and virtual destruction still work ---
    {
        FsSource* polymorphic = new RealFsSource();
        std::string polymorphicError;
        const std::vector<DirEntry> polymorphicEntries =
            polymorphic->list(base.string(), polymorphicError);
        CHECK(!polymorphicEntries.empty());
        CHECK(polymorphicError.empty());
        delete polymorphic;
    }

    // ScopedTempDirectory performs the only cleanup after this function
    // returns.  In particular, no cleanup path calls canonical(), status(),
    // or another operation that follows a fixture symlink.
    return 0;
}

} // namespace

int main() {
    runTests();
    return testSummary();
}
