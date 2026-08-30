// Contract tests for D1's identity-safe scanner.
//
// These tests intentionally use only the canned FsSource boundary.  A real
// filesystem fixture would make the hard-link and cycle assertions depend on
// the host filesystem's allocation unit and symlink policy instead of testing
// the scanner's semantics deterministically.

#include "assert.hpp"
#include "fake_fs.hpp"
#include "diskmap/scanner.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

using diskmap::DirEntry;
using diskmap::FileIdentity;
using diskmap::FsKind;
using diskmap::FsNode;
using diskmap::ScanOptions;
using diskmap::ScanResult;
using diskmap::scan;
using diskmap_test::FakeFsSource;

FileIdentity identity(std::uint64_t device, std::uint64_t file) {
    return FileIdentity{device, file, true};
}

DirEntry physicalFile(std::string name,
                      std::uint64_t logicalSize,
                      FileIdentity fileIdentity,
                      std::uint64_t allocatedSize,
                      std::uint64_t hardLinkCount) {
    DirEntry entry;
    entry.name = std::move(name);
    entry.is_dir = false;
    entry.is_symlink = false;
    entry.size = logicalSize;
    entry.metadata.kind = FsKind::RegularFile;
    entry.metadata.identity = fileIdentity;
    entry.metadata.logical_size = logicalSize;
    entry.metadata.allocated_size = allocatedSize;
    entry.metadata.allocated_size_known = true;
    entry.metadata.hard_link_count = hardLinkCount;
    entry.metadata.hard_link_count_known = true;
    entry.metadata.complete = true;
    return entry;
}

DirEntry physicalDirectory(std::string name, FileIdentity directoryIdentity) {
    DirEntry entry;
    entry.name = std::move(name);
    entry.is_dir = true;
    entry.is_symlink = false;
    entry.metadata.kind = FsKind::Directory;
    entry.metadata.identity = directoryIdentity;
    entry.metadata.complete = true;
    return entry;
}

DirEntry directorySymlink(std::string name, FileIdentity targetIdentity) {
    DirEntry entry;
    entry.name = std::move(name);
    entry.is_dir = true;
    entry.is_symlink = true;
    entry.metadata.kind = FsKind::Symlink;
    entry.metadata.complete = true;
    entry.has_target_metadata = true;
    entry.target_metadata.kind = FsKind::Directory;
    entry.target_metadata.identity = targetIdentity;
    entry.target_metadata.complete = true;
    return entry;
}

const FsNode* child(const FsNode& node, const std::string& name) {
    return diskmap::findChild(node, name);
}

} // namespace

int main() {
    // --- filesystem paths remain the boundary for nested traversal ---
    // The entry path is deliberately left empty: scanner path construction
    // must use filesystem semantics rather than concatenating display text.
    {
        const std::filesystem::path root =
            std::filesystem::path("virtual fixture") / "root";
        const std::filesystem::path nested = root / "directory with spaces";

        FakeFsSource fs;
        DirEntry directory = physicalDirectory("directory with spaces", identity(1, 10));
        directory.path.clear();
        fs.addListing(root.string(), {directory});
        DirEntry file = physicalFile("payload.bin", 17, identity(1, 11), 4096, 1);
        file.path.clear();
        fs.addListing(nested.string(), {file});

        const ScanResult result = scan(fs, root.string(), ScanOptions{});
        CHECK_EQ(result.root.path, root.string());
        const FsNode* nestedNode = child(result.root, "directory with spaces");
        CHECK(nestedNode != nullptr);
        if (nestedNode != nullptr) {
            CHECK_EQ(nestedNode->path, nested.string());
            const FsNode* payload = child(*nestedNode, "payload.bin");
            CHECK(payload != nullptr);
            if (payload != nullptr) {
                CHECK_EQ(payload->path, (nested / "payload.bin").string());
            }
        }
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(2));
        CHECK_EQ(result.files_scanned, static_cast<std::size_t>(1));
    }

    // --- followed directory symlinks cannot recurse through a visited identity ---
    {
        FakeFsSource fs;
        fs.addListing("/cycle/root", {physicalDirectory("real", identity(7, 70))});
        // The link points back to the already-expanded `real` directory.  The
        // node must remain visible for explainability, but its children must
        // not be walked again.
        fs.addListing("/cycle/root/real",
                      {directorySymlink("back-to-real", identity(7, 70))});

        ScanOptions options;
        options.follow_symlinks = true;
        const ScanResult result = scan(fs, "/cycle/root", options);

        const FsNode* real = child(result.root, "real");
        CHECK(real != nullptr);
        if (real != nullptr) {
            const FsNode* back = child(*real, "back-to-real");
            CHECK(back != nullptr);
            if (back != nullptr) {
                CHECK(back->is_dir);
                CHECK(back->followed);
                CHECK(back->cycle_skipped);
                CHECK(back->children.empty());
                CHECK(back->complete);
            }
        }
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(2));
        CHECK(result.errors.empty());
    }

    // --- hard links count logical bytes per directory entry, physical bytes once ---
    {
        FakeFsSource fs;
        const FileIdentity shared = identity(9, 90);
        fs.addListing("/links/root",
                      {physicalFile("first.bin", 123, shared, 4096, 2),
                       physicalFile("second.bin", 123, shared, 4096, 2)});

        const ScanResult result = scan(fs, "/links/root", ScanOptions{});
        const FsNode* first = child(result.root, "first.bin");
        const FsNode* second = child(result.root, "second.bin");
        CHECK(first != nullptr);
        CHECK(second != nullptr);
        CHECK_EQ(result.root.size, static_cast<std::uint64_t>(246));
        CHECK_EQ(result.root.allocated_size, static_cast<std::uint64_t>(4096));
        CHECK(result.root.allocated_size_known);
        CHECK_EQ(result.root.reclaimable_size, static_cast<std::uint64_t>(4096));
        CHECK(result.root.reclaimable_size_known);
        if (first != nullptr && second != nullptr) {
            CHECK_EQ(first->size, static_cast<std::uint64_t>(123));
            CHECK_EQ(second->size, static_cast<std::uint64_t>(123));
            CHECK(first->metadata.identity == second->metadata.identity);
        }
    }

    // --- a subtree with only one of two known hard-link references is not reclaimable ---
    {
        FakeFsSource fs;
        const FileIdentity shared = identity(10, 100);
        fs.addListing("/links/subtree/root", {physicalDirectory("one", identity(10, 101)),
                                               physicalDirectory("two", identity(10, 102))});
        fs.addListing("/links/subtree/root/one",
                      {physicalFile("shared.bin", 10, shared, 512, 2)});
        fs.addListing("/links/subtree/root/two",
                      {physicalFile("shared.bin", 10, shared, 512, 2)});

        const ScanResult result = scan(fs, "/links/subtree/root", ScanOptions{});
        const FsNode* one = child(result.root, "one");
        const FsNode* two = child(result.root, "two");
        CHECK(one != nullptr);
        CHECK(two != nullptr);
        CHECK_EQ(result.root.reclaimable_size, static_cast<std::uint64_t>(512));
        CHECK(result.root.reclaimable_size_known);
        if (one != nullptr) {
            CHECK_EQ(one->allocated_size, static_cast<std::uint64_t>(512));
            CHECK(one->allocated_size_known);
            CHECK_EQ(one->reclaimable_size, static_cast<std::uint64_t>(0));
            CHECK(one->reclaimable_size_known);
        }
        if (two != nullptr) {
            CHECK_EQ(two->allocated_size, static_cast<std::uint64_t>(512));
            CHECK(two->allocated_size_known);
            CHECK_EQ(two->reclaimable_size, static_cast<std::uint64_t>(0));
            CHECK(two->reclaimable_size_known);
        }
    }

    // --- missing physical facts stay unknown instead of becoming zero ---
    {
        FakeFsSource fs;
        DirEntry unknown = physicalFile("unknown.bin", 77, FileIdentity{}, 0, 0);
        unknown.metadata.allocated_size_known = false;
        unknown.metadata.hard_link_count_known = false;
        fs.addListing("/unknown/root", {unknown});

        const ScanResult result = scan(fs, "/unknown/root", ScanOptions{});
        CHECK(!result.root.allocated_size_known);
        CHECK(!result.root.reclaimable_size_known);
        CHECK_EQ(result.root.allocated_size, static_cast<std::uint64_t>(0));
        CHECK_EQ(result.root.reclaimable_size, static_cast<std::uint64_t>(0));
        const FsNode* node = child(result.root, "unknown.bin");
        CHECK(node != nullptr);
        if (node != nullptr) {
            CHECK(!node->allocated_size_known);
            CHECK(!node->reclaimable_size_known);
        }
    }

    return testSummary();
}
