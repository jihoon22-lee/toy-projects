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
#include <map>
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
using diskmap_test::makeDirEntry;
using diskmap_test::makeFileEntry;

class InspectingFakeFsSource : public FakeFsSource {
public:
    void addInspection(const std::filesystem::path& path,
                       bool follow,
                       diskmap::FsMetadata metadata) {
        inspections_[std::make_pair(path, follow)] = std::move(metadata);
    }

    diskmap::FsMetadata inspect(const std::filesystem::path& path,
                                bool follow) const override {
        const auto it = inspections_.find(std::make_pair(path, follow));
        if (it != inspections_.end()) {
            return it->second;
        }
        return diskmap::FsSource::inspect(path, follow);
    }

    std::vector<DirEntry> list(const std::filesystem::path& path,
                               std::string& error,
                               const diskmap::CancellationCheck& cancelled = {}) const override {
        listedPaths.push_back(path);
        return FakeFsSource::list(path, error, cancelled);
    }

    mutable std::vector<std::filesystem::path> listedPaths;

private:
    std::map<std::pair<std::filesystem::path, bool>, diskmap::FsMetadata> inspections_;
};

class PartialListingFsSource : public InspectingFakeFsSource {
public:
    void addPartialListing(const std::filesystem::path& path,
                           std::vector<DirEntry> entries,
                           std::string error) {
        partialListings_[path] = PartialListing{std::move(entries), std::move(error)};
    }

    std::vector<DirEntry> list(const std::filesystem::path& path,
                               std::string& error,
                               const diskmap::CancellationCheck& cancelled = {}) const override {
        listedPaths.push_back(path);
        if (cancelled && cancelled()) {
            error.clear();
            return {};
        }
        const auto it = partialListings_.find(path);
        if (it != partialListings_.end()) {
            error = it->second.error;
            return it->second.entries;
        }
        return FakeFsSource::list(path, error, cancelled);
    }

private:
    struct PartialListing {
        std::vector<DirEntry> entries;
        std::string error;
    };

    std::map<std::filesystem::path, PartialListing> partialListings_;
};

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

DirEntry fileSymlink(std::string name,
                     std::uint64_t logicalSize,
                     FileIdentity linkIdentity,
                     FileIdentity targetIdentity,
                     std::uint64_t allocatedSize,
                     std::uint64_t hardLinkCount) {
    DirEntry entry;
    entry.name = std::move(name);
    entry.is_dir = false;
    entry.is_symlink = true;
    entry.size = logicalSize;
    entry.metadata.kind = FsKind::Symlink;
    entry.metadata.identity = linkIdentity;
    entry.metadata.complete = true;
    entry.has_target_metadata = true;
    entry.target_metadata.kind = FsKind::RegularFile;
    entry.target_metadata.identity = targetIdentity;
    entry.target_metadata.logical_size = logicalSize;
    entry.target_metadata.allocated_size = allocatedSize;
    entry.target_metadata.allocated_size_known = true;
    entry.target_metadata.hard_link_count = hardLinkCount;
    entry.target_metadata.hard_link_count_known = true;
    entry.target_metadata.complete = true;
    return entry;
}

const FsNode* child(const FsNode& node, const std::string& name) {
    return diskmap::findChild(node, name);
}

void checkGeneration(const FsNode& node, std::uint64_t generation) {
    CHECK_EQ(node.scan_generation, generation);
    for (const FsNode& childNode : node.children) {
        checkGeneration(childNode, generation);
    }
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

    // --- an unfollowed directory symlink remains visible as a leaf instead
    // of disappearing from the result tree ---
    {
        FakeFsSource fs;
        fs.addListing("/visible-link/root", {
            directorySymlink("directory-alias", identity(8, 80)),
        });

        const ScanResult result = scan(fs, "/visible-link/root", ScanOptions{});
        const FsNode* alias = child(result.root, "directory-alias");
        CHECK(alias != nullptr);
        if (alias != nullptr) {
            CHECK(!alias->is_dir);
            CHECK(!alias->followed);
            CHECK(alias->complete);
            CHECK(alias->children.empty());
        }
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(1));
        CHECK_EQ(result.files_scanned, static_cast<std::size_t>(1));
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

    // --- a followed file symlink uses target storage facts but is not a
    // reclaimable hard-link reference of its target ---
    {
        FakeFsSource fs;
        const FileIdentity linkIdentity = identity(11, 110);
        const FileIdentity targetIdentity = identity(11, 111);
        fs.addListing("/followed-file/root",
                      {fileSymlink("target-link", 32, linkIdentity, targetIdentity, 4096, 1)});

        ScanOptions options;
        options.follow_symlinks = true;
        const ScanResult result = scan(fs, "/followed-file/root", options);
        const FsNode* link = child(result.root, "target-link");
        CHECK(link != nullptr);
        CHECK_EQ(result.root.size, static_cast<std::uint64_t>(32));
        CHECK_EQ(result.root.allocated_size, static_cast<std::uint64_t>(4096));
        CHECK(result.root.allocated_size_known);
        CHECK_EQ(result.root.reclaimable_size, static_cast<std::uint64_t>(0));
        CHECK(result.root.reclaimable_size_known);
        if (link != nullptr) {
            CHECK(link->followed);
            CHECK(link->has_target_metadata);
            CHECK_EQ(link->metadata.identity, linkIdentity);
            CHECK_EQ(link->target_metadata.identity, targetIdentity);
            CHECK_EQ(link->allocated_size, static_cast<std::uint64_t>(4096));
            CHECK(link->allocated_size_known);
            CHECK_EQ(link->reclaimable_size, static_cast<std::uint64_t>(0));
            CHECK(link->reclaimable_size_known);
        }
    }

    // --- a directory with a valid stat record but a failed listing makes the
    // entire physical aggregate incomplete ---
    {
        FakeFsSource fs;
        fs.addListing("/incomplete/root", {physicalDirectory("blocked", identity(12, 120))});
        fs.addError("/incomplete/root/blocked", "permission denied: blocked");

        const ScanResult result = scan(fs, "/incomplete/root", ScanOptions{});
        const FsNode* blocked = child(result.root, "blocked");
        CHECK(blocked != nullptr);
        if (blocked != nullptr) {
            CHECK(!blocked->complete);
            CHECK_EQ(blocked->error, std::string("permission denied: blocked"));
            CHECK(blocked->metadata.complete);
        }
        CHECK(!result.root.allocated_size_known);
        CHECK(!result.root.reclaimable_size_known);
        CHECK_EQ(result.errors.size(), static_cast<std::size_t>(1));
    }

    // --- a partial listing error retains the entries returned before the
    // failure and continues walking retained directories ---
    {
        PartialListingFsSource fs;
        const std::filesystem::path root = "/partial/root";
        fs.addPartialListing(root,
                             {physicalFile("visible.bin", 5, identity(15, 150), 512, 1),
                              physicalDirectory("nested", identity(15, 151))},
                             "directory changed during enumeration");
        fs.addListing(root / "nested",
                      {physicalFile("nested.bin", 7, identity(15, 152), 512, 1)});

        const ScanResult result = scan(fs, root, ScanOptions{});
        const FsNode* visible = child(result.root, "visible.bin");
        const FsNode* nested = child(result.root, "nested");
        CHECK(visible != nullptr);
        CHECK(nested != nullptr);
        CHECK(!result.root.complete);
        CHECK_EQ(result.root.error, std::string("directory changed during enumeration"));
        CHECK_EQ(result.errors.size(), static_cast<std::size_t>(1));
        CHECK_EQ(result.errors[0], std::string("directory changed during enumeration"));
        CHECK_EQ(result.files_scanned, static_cast<std::size_t>(2));
        if (nested != nullptr) {
            CHECK(nested->complete);
            CHECK(child(*nested, "nested.bin") != nullptr);
        }
        CHECK_EQ(fs.listedPaths.size(), static_cast<std::size_t>(2));
        if (fs.listedPaths.size() == 2) {
            CHECK_EQ(fs.listedPaths[0], root);
            CHECK_EQ(fs.listedPaths[1], root / "nested");
        }
    }

    // --- one_file_system retains a different-device directory but never
    // descends into it ---
    {
        InspectingFakeFsSource fs;
        const std::filesystem::path root = "/mount/root";
        diskmap::FsMetadata rootMetadata;
        rootMetadata.kind = FsKind::Directory;
        rootMetadata.identity = identity(20, 200);
        rootMetadata.complete = true;
        fs.addInspection(root, false, rootMetadata);
        fs.addListing(root,
                      {physicalDirectory("same-device", identity(20, 201)),
                       physicalDirectory("other-device", identity(21, 202))});
        fs.addListing(root / "same-device",
                      {physicalFile("inside.bin", 3, identity(20, 203), 512, 1)});
        fs.addListing(root / "other-device",
                      {physicalFile("must-not-be-read.bin", 99, identity(21, 204), 512, 1)});

        ScanOptions options;
        options.one_file_system = true;
        const ScanResult result = scan(fs, root, options);
        const FsNode* same = child(result.root, "same-device");
        const FsNode* other = child(result.root, "other-device");
        CHECK(same != nullptr);
        CHECK(other != nullptr);
        CHECK_EQ(result.mount_boundaries_skipped, static_cast<std::size_t>(1));
        CHECK(result.errors.empty());
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(2));
        CHECK_EQ(result.files_scanned, static_cast<std::size_t>(1));
        if (same != nullptr) {
            CHECK(child(*same, "inside.bin") != nullptr);
        }
        if (other != nullptr) {
            CHECK(!other->complete);
            CHECK(other->mount_boundary_skipped);
            CHECK(other->children.empty());
            CHECK_EQ(other->error, std::string("mount boundary excluded"));
        }
        CHECK_EQ(fs.listedPaths.size(), static_cast<std::size_t>(2));
        if (fs.listedPaths.size() == 2) {
            CHECK_EQ(fs.listedPaths[0], root);
            CHECK_EQ(fs.listedPaths[1], root / "same-device");
        }
    }

    // --- one_file_system fails safe when a directory identity is unknown ---
    {
        InspectingFakeFsSource fs;
        const std::filesystem::path root = "/mount/unknown";
        diskmap::FsMetadata rootMetadata;
        rootMetadata.kind = FsKind::Directory;
        rootMetadata.identity = identity(22, 220);
        rootMetadata.complete = true;
        fs.addInspection(root, false, rootMetadata);
        fs.addListing(root, {physicalDirectory("unknown-device", FileIdentity{})});
        fs.addListing(root / "unknown-device",
                      {physicalFile("must-not-be-read.bin", 99, identity(22, 221), 512, 1)});

        ScanOptions options;
        options.one_file_system = true;
        const ScanResult result = scan(fs, root, options);
        const FsNode* unknown = child(result.root, "unknown-device");
        CHECK(unknown != nullptr);
        CHECK_EQ(result.mount_boundaries_skipped, static_cast<std::size_t>(0));
        CHECK_EQ(result.errors.size(), static_cast<std::size_t>(1));
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(1));
        CHECK_EQ(result.files_scanned, static_cast<std::size_t>(0));
        if (unknown != nullptr) {
            CHECK(!unknown->complete);
            CHECK(!unknown->mount_boundary_skipped);
            CHECK(unknown->children.empty());
            CHECK(unknown->error.find("cannot verify mount boundary") != std::string::npos);
        }
        CHECK_EQ(fs.listedPaths.size(), static_cast<std::size_t>(1));
        if (!fs.listedPaths.empty()) {
            CHECK_EQ(fs.listedPaths.front(), root);
        }
    }

    // --- generation identifies every retained node in a result ---
    {
        FakeFsSource fs;
        fs.addListing("/generation/root", {makeDirEntry("nested"), makeFileEntry("root.bin", 2)});
        fs.addListing("/generation/root/nested", {makeFileEntry("nested.bin", 3)});

        ScanOptions options;
        options.generation = 987654321;
        const ScanResult result = scan(fs, "/generation/root", options);
        CHECK_EQ(result.generation, static_cast<std::uint64_t>(987654321));
        checkGeneration(result.root, 987654321);
    }

    // --- duplicate physical directories are retained as visible aliases but
    // only the first one is expanded ---
    {
        FakeFsSource fs;
        const FileIdentity sharedDirectory = identity(13, 130);
        fs.addListing("/duplicate/root",
                      {physicalDirectory("canonical", sharedDirectory),
                       physicalDirectory("alias", sharedDirectory)});
        fs.addListing("/duplicate/root/canonical", {physicalFile("payload", 7, identity(13, 131), 512, 1)});
        fs.addListing("/duplicate/root/alias", {physicalFile("not-walked", 99, identity(13, 132), 512, 1)});

        const ScanResult result = scan(fs, "/duplicate/root", ScanOptions{});
        const FsNode* canonical = child(result.root, "canonical");
        const FsNode* alias = child(result.root, "alias");
        CHECK(canonical != nullptr);
        CHECK(alias != nullptr);
        if (canonical != nullptr) {
            CHECK(!canonical->cycle_skipped);
            CHECK_EQ(canonical->children.size(), static_cast<std::size_t>(1));
        }
        if (alias != nullptr) {
            CHECK(alias->cycle_skipped);
            CHECK(alias->children.empty());
            CHECK(alias->complete);
        }
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(2));
        CHECK_EQ(result.files_scanned, static_cast<std::size_t>(1));
    }

    // --- following a directory without a target identity is rejected before
    // any untrusted target listing is attempted ---
    {
        FakeFsSource fs;
        DirEntry unsafe = directorySymlink("unsafe", FileIdentity{});
        unsafe.target_metadata.complete = true;
        fs.addListing("/unsafe/root", {unsafe});

        ScanOptions options;
        options.follow_symlinks = true;
        const ScanResult result = scan(fs, "/unsafe/root", options);
        const FsNode* node = child(result.root, "unsafe");
        CHECK(node != nullptr);
        if (node != nullptr) {
            CHECK(node->followed);
            CHECK(!node->complete);
            CHECK(!node->cycle_skipped);
            CHECK(node->children.empty());
            CHECK(node->error.find("target identity is unavailable") != std::string::npos);
        }
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(1));
        CHECK_EQ(result.errors.size(), static_cast<std::size_t>(1));
    }

    // A dangling followed link is retained with its own metadata and the
    // target lookup error; it must not silently become a complete file.
    {
        FakeFsSource fs;
        DirEntry dangling;
        dangling.name = "dangling";
        dangling.is_symlink = true;
        dangling.size = 0;
        dangling.metadata.kind = FsKind::Symlink;
        dangling.metadata.complete = true;
        dangling.has_target_metadata = false;
        dangling.target_metadata.error = "target is missing";
        fs.addListing("/dangling/root", {dangling});

        ScanOptions options;
        options.follow_symlinks = true;
        const ScanResult result = scan(fs, "/dangling/root", options);
        const FsNode* node = child(result.root, "dangling");
        CHECK(node != nullptr);
        if (node != nullptr) {
            CHECK(node->followed);
            CHECK(!node->has_target_metadata);
            CHECK(!node->complete);
            CHECK_EQ(node->error, std::string("target is missing"));
        }
        CHECK_EQ(result.files_scanned, static_cast<std::size_t>(1));
    }

    // --- a regular-file root is a complete one-node scan, not a failed
    // attempt to open that file as a directory ---
    {
        InspectingFakeFsSource fs;
        const std::filesystem::path root = "/root-file/payload.bin";
        diskmap::FsMetadata fileMetadata;
        fileMetadata.kind = FsKind::RegularFile;
        fileMetadata.identity = identity(14, 139);
        fileMetadata.logical_size = 37;
        fileMetadata.allocated_size = 4096;
        fileMetadata.allocated_size_known = true;
        fileMetadata.hard_link_count = 1;
        fileMetadata.hard_link_count_known = true;
        fileMetadata.complete = true;
        fs.addInspection(root, false, fileMetadata);

        const ScanResult result = scan(fs, root, ScanOptions{});
        CHECK(!result.root.is_dir);
        CHECK(result.root.complete);
        CHECK_EQ(result.root.size, static_cast<std::uint64_t>(37));
        CHECK_EQ(result.root.allocated_size, static_cast<std::uint64_t>(4096));
        CHECK(result.root.allocated_size_known);
        CHECK_EQ(result.root.reclaimable_size, static_cast<std::uint64_t>(4096));
        CHECK(result.root.reclaimable_size_known);
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(0));
        CHECK_EQ(result.files_scanned, static_cast<std::size_t>(1));
        CHECK(result.errors.empty());
    }

    // --- an explicitly selected root symlink is dereferenced even when
    // descendant following is disabled; a file target remains a leaf ---
    {
        InspectingFakeFsSource fs;
        const std::filesystem::path root = "/root-link/payload";
        diskmap::FsMetadata linkMetadata;
        linkMetadata.kind = FsKind::Symlink;
        linkMetadata.identity = identity(14, 140);
        linkMetadata.complete = true;
        diskmap::FsMetadata targetMetadata;
        targetMetadata.kind = FsKind::RegularFile;
        targetMetadata.identity = identity(14, 141);
        targetMetadata.logical_size = 73;
        targetMetadata.allocated_size = 8192;
        targetMetadata.allocated_size_known = true;
        targetMetadata.hard_link_count = 1;
        targetMetadata.hard_link_count_known = true;
        targetMetadata.complete = true;
        fs.addInspection(root, false, linkMetadata);
        fs.addInspection(root, true, targetMetadata);

        const ScanResult result = scan(fs, root, ScanOptions{});
        CHECK(result.root.followed);
        CHECK(!result.root.is_dir);
        CHECK(result.root.complete);
        CHECK(result.root.has_target_metadata);
        CHECK_EQ(result.root.size, static_cast<std::uint64_t>(73));
        CHECK_EQ(result.root.allocated_size, static_cast<std::uint64_t>(8192));
        CHECK(result.root.allocated_size_known);
        CHECK_EQ(result.root.reclaimable_size, static_cast<std::uint64_t>(0));
        CHECK(result.root.reclaimable_size_known);
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(0));
        CHECK_EQ(result.files_scanned, static_cast<std::size_t>(1));
        CHECK(result.errors.empty());
    }

    // --- root metadata is allowed to be a symlink, including an incomplete
    // target, and root lookup errors remain explicit ---
    {
        InspectingFakeFsSource fs;
        const std::filesystem::path root = "/root-link/root";
        diskmap::FsMetadata linkMetadata;
        linkMetadata.kind = FsKind::Symlink;
        linkMetadata.identity = identity(14, 140);
        linkMetadata.complete = true;
        diskmap::FsMetadata targetMetadata;
        targetMetadata.kind = FsKind::Directory;
        targetMetadata.identity = identity(14, 141);
        targetMetadata.complete = true;
        fs.addInspection(root, false, linkMetadata);
        fs.addInspection(root, true, targetMetadata);
        fs.addListing(root, {});

        const ScanResult result = scan(fs, root, ScanOptions{});
        CHECK(result.root.followed);
        CHECK(result.root.is_dir);
        CHECK(result.root.complete);
        CHECK(result.root.has_target_metadata);
        CHECK_EQ(result.root.target_metadata.identity, targetMetadata.identity);
        CHECK_EQ(result.root.metadata.identity, linkMetadata.identity);
        CHECK(result.errors.empty());
    }

    {
        InspectingFakeFsSource fs;
        const std::filesystem::path root = "/root-link/broken";
        diskmap::FsMetadata linkMetadata;
        linkMetadata.kind = FsKind::Symlink;
        linkMetadata.identity = identity(14, 142);
        linkMetadata.complete = true;
        diskmap::FsMetadata targetMetadata;
        targetMetadata.error = "target disappeared";
        fs.addInspection(root, false, linkMetadata);
        fs.addInspection(root, true, targetMetadata);
        fs.addListing(root, {});

        const ScanResult result = scan(fs, root, ScanOptions{});
        CHECK(result.root.followed);
        CHECK(!result.root.is_dir);
        CHECK(!result.root.complete);
        CHECK(!result.root.has_target_metadata);
        CHECK_EQ(result.root.error, std::string("target disappeared"));
        CHECK_EQ(result.errors.size(), static_cast<std::size_t>(1));
        CHECK_EQ(result.errors[0], std::string("target disappeared"));
    }

    {
        InspectingFakeFsSource fs;
        const std::filesystem::path root = "/root-error";
        diskmap::FsMetadata failed;
        failed.error = "root metadata unavailable";
        fs.addInspection(root, false, failed);

        const ScanResult result = scan(fs, root, ScanOptions{});
        CHECK(!result.root.complete);
        CHECK_EQ(result.root.error, std::string("root metadata unavailable"));
        CHECK_EQ(result.fatal_error, std::string("root metadata unavailable"));
        CHECK_EQ(result.errors.size(), static_cast<std::size_t>(1));
        CHECK_EQ(result.errors[0], std::string("root metadata unavailable"));
        CHECK(fs.listedPaths.empty());
    }

    {
        InspectingFakeFsSource fs;
        const std::filesystem::path root = "/unreadable-root";
        diskmap::FsMetadata metadata;
        metadata.kind = FsKind::Directory;
        metadata.identity = identity(15, 151);
        metadata.complete = true;
        fs.addInspection(root, false, metadata);
        fs.addError(root, "permission denied: root");

        const ScanResult result = scan(fs, root, ScanOptions{});
        CHECK(!result.root.complete);
        CHECK_EQ(result.fatal_error, std::string("permission denied: root"));
        CHECK_EQ(result.error_count, static_cast<std::size_t>(1));
        CHECK_EQ(result.errors.size(), static_cast<std::size_t>(1));
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(0));
    }

    {
        PartialListingFsSource fs;
        const std::filesystem::path root = "/partial-root";
        diskmap::FsMetadata metadata;
        metadata.kind = FsKind::Directory;
        metadata.identity = identity(16, 161);
        metadata.complete = true;
        fs.addInspection(root, false, metadata);
        fs.addPartialListing(root, {makeFileEntry("kept.bin", 17)},
                             "directory changed during enumeration");

        const ScanResult result = scan(fs, root, ScanOptions{});
        CHECK(!result.root.complete);
        CHECK(result.fatal_error.empty());
        CHECK_EQ(result.files_scanned, static_cast<std::size_t>(1));
        CHECK(diskmap::findChild(result.root, "kept.bin") != nullptr);
    }

    // The root fallback in lastPathComponent is observable only for a path
    // whose normalized filename is empty (the filesystem root itself).
    {
        FakeFsSource fs;
        fs.addListing("/", {});
        const ScanResult result = scan(fs, "/", ScanOptions{});
        CHECK_EQ(result.root.name, std::string("/"));
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(1));
    }

    return testSummary();
}
