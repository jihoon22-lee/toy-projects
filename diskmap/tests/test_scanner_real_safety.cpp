// Integration tests for the identity-aware real-filesystem scanner.
//
// The scanner contract needs both deterministic fake-source tests and a small
// real POSIX fixture: only the latter can prove that the metadata adapter and
// the scanner agree on the host's physical identities.  Fixture creation is
// capability-gated so filesystems without hard-link or symlink support report
// a skip instead of turning an environment limitation into a product failure.

#include "assert.hpp"
#include "diskmap/fs_source.hpp"
#include "diskmap/scanner.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace {

class ScopedTempDirectory {
public:
    ScopedTempDirectory() {
        std::error_code ec;
        const fs::path parent = fs::temp_directory_path(ec);
        if (ec) {
            error_ = "cannot locate the temporary directory: " + ec.message();
            return;
        }

        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt != 100; ++attempt) {
            const fs::path candidate =
                parent / ("diskmap_scanner_real_safety_" + std::to_string(stamp) + "_" +
                          std::to_string(attempt));
            ec.clear();
            if (fs::create_directory(candidate, ec)) {
                path_ = candidate;
                return;
            }
            if (ec && ec != std::make_error_code(std::errc::file_exists)) {
                error_ = "cannot create a temporary fixture directory: " + ec.message();
                return;
            }
        }
        error_ = "could not allocate a unique temporary fixture directory";
    }

    ~ScopedTempDirectory() {
        if (path_.empty()) {
            return;
        }
        std::error_code ec;
        // remove_all removes a symlink entry without traversing its target.
        fs::remove_all(path_, ec);
    }

    bool valid() const { return !path_.empty(); }
    const fs::path& path() const { return path_; }
    const std::string& error() const { return error_; }

private:
    fs::path path_;
    std::string error_;
};

bool writeFile(const fs::path& path, std::uintmax_t bytes) {
    std::ofstream output(path.string(), std::ios::binary);
    if (!output) {
        return false;
    }
    constexpr std::size_t kChunkSize = 4096;
    const std::string chunk(kChunkSize, 'x');
    while (bytes >= chunk.size()) {
        output.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        if (!output) {
            return false;
        }
        bytes -= chunk.size();
    }
    if (bytes != 0) {
        output.write(chunk.data(), static_cast<std::streamsize>(bytes));
    }
    return output.good();
}

void checkRootMetadata(const diskmap::RealFsSource& source, const fs::path& root) {
    const diskmap::FsMetadata inspected = source.inspect(root, false);
    CHECK(inspected.complete);
    CHECK_EQ(inspected.kind, diskmap::FsKind::Directory);
    CHECK(inspected.identity.valid);

    const diskmap::ScanResult result = diskmap::scan(source, root, diskmap::ScanOptions{});
    CHECK(result.errors.empty());
    CHECK(result.root.complete);
    CHECK(result.root.metadata.complete);
    CHECK_EQ(result.root.metadata.kind, diskmap::FsKind::Directory);
    CHECK(result.root.metadata.identity.valid);
    CHECK_EQ(result.root.metadata.identity, inspected.identity);
    CHECK_EQ(result.root.path, root);
}

void checkHardLinks(const fs::path& root) {
    const fs::path original = root / "original.bin";
    const fs::path alias = root / "alias.bin";
    constexpr std::uintmax_t kLogicalSize = 32U * 1024U;
    CHECK(writeFile(original, kLogicalSize));

    std::error_code hardLinkError;
    fs::create_hard_link(original, alias, hardLinkError);
    if (hardLinkError) {
        std::printf("SKIP real hard-link aggregate checks: %s\n", hardLinkError.message().c_str());
        return;
    }

    diskmap::RealFsSource source;
    const diskmap::FsMetadata originalMetadata = source.inspect(original, false);
    const diskmap::FsMetadata aliasMetadata = source.inspect(alias, false);
    CHECK(originalMetadata.complete);
    CHECK(aliasMetadata.complete);
    CHECK(originalMetadata.identity.valid);
    CHECK_EQ(originalMetadata.identity, aliasMetadata.identity);
    CHECK(originalMetadata.hard_link_count_known);
    CHECK(originalMetadata.hard_link_count >= 2U);

    const diskmap::ScanResult result = diskmap::scan(source, root, diskmap::ScanOptions{});
    CHECK(result.errors.empty());
    const diskmap::FsNode* originalNode = diskmap::findChild(result.root, "original.bin");
    const diskmap::FsNode* aliasNode = diskmap::findChild(result.root, "alias.bin");
    CHECK(originalNode != nullptr);
    CHECK(aliasNode != nullptr);
    CHECK_EQ(result.root.size, static_cast<std::uint64_t>(kLogicalSize * 2U));
    CHECK(result.root.allocated_size_known);
    CHECK(result.root.reclaimable_size_known);
    // Both directory entries reference one physical object.  The aggregate
    // must therefore equal one file's allocation, not two copies of it.
    CHECK_EQ(result.root.allocated_size, originalMetadata.allocated_size);
    CHECK_EQ(result.root.reclaimable_size, originalMetadata.allocated_size);
    if (originalNode != nullptr && aliasNode != nullptr) {
        CHECK_EQ(originalNode->size, static_cast<std::uint64_t>(kLogicalSize));
        CHECK_EQ(aliasNode->size, static_cast<std::uint64_t>(kLogicalSize));
        CHECK_EQ(originalNode->metadata.identity, aliasNode->metadata.identity);
    }
}

void checkSymlinkBackEdge(const fs::path& root) {
    const fs::path target = root / "target";
    const fs::path backEdge = root / "back-to-root";
    std::error_code directoryError;
    if (!fs::create_directory(target, directoryError)) {
        std::printf("SKIP real symlink cycle checks: cannot create target directory: %s\n",
                    directoryError.message().c_str());
        return;
    }

    std::error_code symlinkError;
    fs::create_directory_symlink(root, backEdge, symlinkError);
    if (symlinkError) {
        std::printf("SKIP real symlink cycle checks: %s\n", symlinkError.message().c_str());
        return;
    }

    diskmap::RealFsSource source;
    diskmap::ScanOptions options;
    options.follow_symlinks = true;
    const diskmap::ScanResult result = diskmap::scan(source, root, options);

    CHECK(result.errors.empty());
    CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(2));
    const diskmap::FsNode* back = diskmap::findChild(result.root, "back-to-root");
    CHECK(back != nullptr);
    if (back != nullptr) {
        CHECK(back->is_dir);
        CHECK(back->followed);
        CHECK(back->cycle_skipped);
        CHECK(back->children.empty());
        CHECK(back->complete);
        CHECK(back->metadata.complete);
        CHECK(back->has_target_metadata);
        CHECK(back->target_metadata.complete);
        CHECK_EQ(back->target_metadata.identity, result.root.metadata.identity);
    }
}

int runTests() {
#if !defined(__unix__) && !defined(__APPLE__)
    std::printf("SKIP real scanner fixture: POSIX identity semantics are unavailable\n");
    return 0;
#else
    ScopedTempDirectory temporary;
    if (!temporary.valid()) {
        std::printf("SKIP real scanner fixture: %s\n", temporary.error().c_str());
        return 0;
    }

    checkRootMetadata(diskmap::RealFsSource{}, temporary.path());
    checkHardLinks(temporary.path());
    checkSymlinkBackEdge(temporary.path());
    return 0;
#endif
}

} // namespace

int main() {
    runTests();
    return testSummary();
}
