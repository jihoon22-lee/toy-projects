// Safety and identity contracts for cleanup staging and revalidation.

#include "assert.hpp"
#include "diskmap/cleanup.hpp"

#include <cstdlib>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using diskmap::CleanupPolicy;
using diskmap::CleanupSkipReason;
using diskmap::FileIdentity;
using diskmap::FsKind;
using diskmap::FsMetadata;
using diskmap::FsNode;
using diskmap::NodeKey;
using diskmap::ScanResult;

namespace {

FsMetadata metadata(FsKind kind,
                    std::uint64_t file,
                    std::uint64_t size = 0,
                    std::uint64_t allocated = 0,
                    std::uint64_t links = 1) {
    FsMetadata value;
    value.kind = kind;
    value.identity = FileIdentity{1, file, true};
    value.logical_size = size;
    value.allocated_size = allocated;
    value.allocated_size_known = true;
    value.hard_link_count = links;
    value.hard_link_count_known = true;
    value.complete = true;
    return value;
}

FsNode file(std::string path,
            std::uint64_t identity,
            std::uint64_t size,
            std::uint64_t allocated,
            std::uint64_t links = 1) {
    FsNode node;
    node.name = std::filesystem::path(path).filename().string();
    node.path = std::move(path);
    node.metadata = metadata(FsKind::RegularFile, identity, size, allocated, links);
    node.size = size;
    node.logical_size_known = true;
    node.allocated_size = allocated;
    node.allocated_size_known = true;
    node.reclaimable_size = links == 1 ? allocated : 0;
    node.reclaimable_size_known = true;
    node.scan_generation = 7;
    return node;
}

FsNode directory(std::string path,
                 std::uint64_t identity,
                 std::vector<FsNode> children = {}) {
    FsNode node;
    node.name = std::filesystem::path(path).filename().string();
    node.path = std::move(path);
    node.is_dir = true;
    node.metadata = metadata(FsKind::Directory, identity);
    node.children = std::move(children);
    node.scan_generation = 7;
    return node;
}

FsNode symlink(std::string path, std::uint64_t identity, std::vector<FsNode> followed = {}) {
    FsNode node;
    node.name = std::filesystem::path(path).filename().string();
    node.path = std::move(path);
    node.is_dir = !followed.empty();
    node.metadata = metadata(FsKind::Symlink, identity, 8, 512);
    node.followed = !followed.empty();
    node.children = std::move(followed);
    node.scan_generation = 7;
    return node;
}

ScanResult result(FsNode root) {
    ScanResult scan;
    scan.root = std::move(root);
    scan.generation = 7;
    return scan;
}

bool rejectedFor(const diskmap::CleanupPlan& plan,
                 const NodeKey& key,
                 CleanupSkipReason reason) {
    for (const auto& item : plan.rejected) {
        if (item.key == key && item.reason == reason) {
            return true;
        }
    }
    return false;
}

class FakeSource final : public diskmap::FsSource {
public:
    std::vector<diskmap::DirEntry> list(
        const std::filesystem::path&,
        std::string& error,
        const diskmap::CancellationCheck& = {}) const override {
        error.clear();
        return {};
    }

    FsMetadata inspect(const std::filesystem::path& path, bool follow) const override {
        CHECK(!follow);
        const auto found = records.find(path.lexically_normal().generic_string());
        return found == records.end() ? FsMetadata{} : found->second;
    }

    std::map<std::string, FsMetadata> records;
};

} // namespace

int main() {
    // Parent directories cover descendants, while a similarly prefixed path
    // is not accidentally treated as a child.
    {
        FsNode root = directory("/scan", 1, {
            directory("/scan/data", 2, {file("/scan/data/a", 3, 10, 512)}),
            directory("/scan/database", 4, {file("/scan/database/b", 5, 20, 1024)}),
        });
        ScanResult scan = result(std::move(root));
        const NodeKey parent = diskmap::nodeKey(scan.root.children[0]);
        const NodeKey child = diskmap::nodeKey(scan.root.children[0].children[0]);
        const NodeKey prefixed = diskmap::nodeKey(scan.root.children[1]);
        const diskmap::CleanupPlan plan = diskmap::planCleanup(scan, {child, prefixed, parent});
        CHECK_EQ(plan.targets.size(), static_cast<std::size_t>(2));
        CHECK(plan.targets[0].key == parent);
        CHECK(plan.targets[1].key == prefixed);
        CHECK(rejectedFor(plan, child, CleanupSkipReason::CoveredByParent));
        CHECK_EQ(plan.reclaimable_bytes, static_cast<std::uint64_t>(1536));
        CHECK(plan.reclaimable_bytes_known);
    }

    // Root, protected subtree, and mount-boundary decisions are explicit and
    // use path components rather than unsafe string-prefix matching.
    {
        FsNode root = directory("/scan", 10, {
            file("/scan/home/item", 11, 1, 512),
            file("/scan/home2/item", 12, 1, 512),
            directory("/scan/mount", 13),
        });
        root.children[2].mount_boundary_skipped = true;
        root.children[2].complete = false;
        ScanResult scan = result(std::move(root));
        CleanupPolicy policy;
        policy.protected_roots = {"/scan/home"};
        const NodeKey rootKey = diskmap::nodeKey(scan.root);
        const NodeKey protectedKey = diskmap::nodeKey(scan.root.children[0]);
        const NodeKey prefixKey = diskmap::nodeKey(scan.root.children[1]);
        const NodeKey mountKey = diskmap::nodeKey(scan.root.children[2]);
        const auto plan = diskmap::planCleanup(
            scan, {rootKey, protectedKey, prefixKey, mountKey}, policy);
        CHECK_EQ(plan.targets.size(), static_cast<std::size_t>(1));
        CHECK(plan.targets[0].key == prefixKey);
        CHECK(rejectedFor(plan, rootKey, CleanupSkipReason::RootTarget));
        CHECK(rejectedFor(plan, protectedKey, CleanupSkipReason::ProtectedRoot));
        CHECK(rejectedFor(plan, mountKey, CleanupSkipReason::MountBoundary));
    }

    // A symlink itself is safe to stage as a link entry. Descendants reached
    // through its followed target are never cleanup targets.
    {
        FsNode link = symlink("/scan/link", 22, {file("/scan/link/inside", 23, 4, 512)});
        link.complete = false; // followed target traversal failed; link metadata is still exact
        link.children[0].complete = false;
        ScanResult scan = result(directory("/scan", 21, {std::move(link)}));
        const NodeKey linkKey = diskmap::nodeKey(scan.root.children[0]);
        const NodeKey childKey = diskmap::nodeKey(scan.root.children[0].children[0]);
        const auto plan = diskmap::planCleanup(scan, {linkKey, childKey});
        CHECK_EQ(plan.targets.size(), static_cast<std::size_t>(1));
        CHECK(plan.targets[0].key == linkKey);
        CHECK(plan.targets[0].symlink);
        CHECK_EQ(plan.reclaimable_bytes, static_cast<std::uint64_t>(512));
        CHECK(rejectedFor(plan, childKey, CleanupSkipReason::IncompleteScan)
              || rejectedFor(plan, childKey, CleanupSkipReason::SymlinkDescendant));

        FsNode nestedLink = symlink(
            "/scan/parent/link", 25,
            {file("/scan/parent/link/outside-target", 26, 999, 8192)});
        nestedLink.children[0].complete = false;
        ScanResult parentScan = result(directory(
            "/scan", 21,
            {directory("/scan/parent", 24, {std::move(nestedLink)})}));
        const NodeKey parentKey = diskmap::nodeKey(parentScan.root.children[0]);
        const auto parentPlan = diskmap::planCleanup(parentScan, {parentKey});
        CHECK_EQ(parentPlan.targets.size(), static_cast<std::size_t>(1));
        // Moving the directory moves the symlink entry, never target data
        // reached while browsing through that symlink.
        CHECK_EQ(parentPlan.reclaimable_bytes, static_cast<std::uint64_t>(512));
    }

    // Reclaimability is a selected identity union. One alias alone is known
    // to reclaim zero bytes; selecting every observed link reclaims once.
    {
        ScanResult scan = result(directory("/scan", 30, {
            file("/scan/a", 31, 20, 4096, 2),
            file("/scan/b", 31, 20, 4096, 2),
        }));
        const NodeKey first = diskmap::nodeKey(scan.root.children[0]);
        const NodeKey second = diskmap::nodeKey(scan.root.children[1]);
        const auto partial = diskmap::planCleanup(scan, {first});
        CHECK(partial.reclaimable_bytes_known);
        CHECK_EQ(partial.reclaimable_bytes, static_cast<std::uint64_t>(0));
        const auto complete = diskmap::planCleanup(scan, {first, second});
        CHECK(complete.reclaimable_bytes_known);
        CHECK_EQ(complete.reclaimable_bytes, static_cast<std::uint64_t>(4096));
    }

    // Global scan incompleteness and stale generation cannot be hidden by a
    // healthy-looking leaf.
    {
        ScanResult scan = result(directory("/scan", 40, {file("/scan/a", 41, 1, 512)}));
        const NodeKey key = diskmap::nodeKey(scan.root.children[0]);
        scan.totals_filtered = true;
        CHECK(rejectedFor(diskmap::planCleanup(scan, {key}), key,
                          CleanupSkipReason::ScannerFiltered));
        scan.totals_filtered = false;
        scan.root.children[0].complete = false;
        CHECK(rejectedFor(diskmap::planCleanup(scan, {key}), key,
                          CleanupSkipReason::IncompleteScan));
        scan.root.children[0].complete = true;
        scan.root.children[0].scan_generation = 6;
        CHECK(rejectedFor(diskmap::planCleanup(scan, {key}), key,
                          CleanupSkipReason::StaleGeneration));
    }

    // Revalidation inspects the entry itself and rejects every material
    // identity/type/size/hard-link change.
    {
        ScanResult scan = result(directory("/scan", 50, {file("/scan/a", 51, 7, 1024, 1)}));
        const NodeKey key = diskmap::nodeKey(scan.root.children[0]);
        const auto plan = diskmap::planCleanup(scan, {key});
        CHECK_EQ(plan.targets.size(), static_cast<std::size_t>(1));
        const auto target = plan.targets[0];
        FakeSource source;
        source.records["/scan/a"] = metadata(FsKind::RegularFile, 51, 7, 1024, 1);
        CHECK(diskmap::revalidateCleanupTarget(target, source).accepted);
        source.records["/scan/a"].identity.file = 99;
        CHECK(diskmap::revalidateCleanupTarget(target, source).reason
              == CleanupSkipReason::IdentityChanged);
        source.records["/scan/a"] = metadata(FsKind::Directory, 51, 7, 1024, 1);
        CHECK(diskmap::revalidateCleanupTarget(target, source).reason
              == CleanupSkipReason::TypeChanged);
        source.records["/scan/a"] = metadata(FsKind::RegularFile, 51, 8, 1024, 1);
        CHECK(diskmap::revalidateCleanupTarget(target, source).reason
              == CleanupSkipReason::SizeChanged);
        source.records["/scan/a"] = metadata(FsKind::RegularFile, 51, 7, 1024, 2);
        CHECK(diskmap::revalidateCleanupTarget(target, source).reason
              == CleanupSkipReason::HardLinkChanged);
    }

    // Selection bounds fail closed before any planning work is performed.
    {
        ScanResult scan = result(directory("/scan", 60, {file("/scan/a", 61, 1, 512)}));
        const NodeKey key = diskmap::nodeKey(scan.root.children[0]);
        CleanupPolicy policy;
        policy.max_selected = 1;
        bool threw = false;
        try {
            (void)diskmap::planCleanup(scan, {key, key}, policy);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        CHECK(threw);
    }

    // The configured home directory itself is protected by default; explicit
    // protected_roots cover descendants (as exercised above). This check is
    // skipped only on hosts without HOME.
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        const std::filesystem::path protectedPath = std::filesystem::path(home);
        ScanResult scan = result(directory(
            "/scan", 70, {file(protectedPath.string(), 71, 1, 512)}));
        const NodeKey key = diskmap::nodeKey(scan.root.children[0]);
        CHECK(rejectedFor(diskmap::planCleanup(scan, {key}), key,
                          CleanupSkipReason::ProtectedRoot));
    }

    return testSummary();
}
