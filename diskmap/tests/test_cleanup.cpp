// Safety and identity contracts for cleanup staging and revalidation.

#include "assert.hpp"
#include "diskmap/cleanup.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using diskmap::CleanupPolicy;
using diskmap::CleanupSkipReason;
using diskmap::CleanupTarget;
using diskmap::FileIdentity;
using diskmap::FsKind;
using diskmap::FsMetadata;
using diskmap::FsNode;
using diskmap::NodeKey;
using diskmap::ScanResult;

namespace {

class ScopedTempDirectory {
public:
    ScopedTempDirectory() {
        std::error_code error;
        const std::filesystem::path parent = std::filesystem::temp_directory_path(error);
        if (error) {
            return;
        }
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt != 100; ++attempt) {
            const std::filesystem::path candidate =
                parent / ("diskmap_cleanup_test_" + std::to_string(stamp) + "_"
                          + std::to_string(attempt));
            error.clear();
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = candidate;
                return;
            }
            if (error && error != std::make_error_code(std::errc::file_exists)) {
                return;
            }
        }
    }

    ~ScopedTempDirectory() {
        if (!path_.empty()) {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }
    }

    bool valid() const { return !path_.empty(); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

bool writeFile(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return output.good();
}

CleanupTarget targetFor(const std::filesystem::path& path,
                        std::uint64_t generation = 1) {
    diskmap::RealFsSource source;
    const std::filesystem::path absolute =
        std::filesystem::absolute(path).lexically_normal();
    const FsMetadata value = source.inspect(absolute, false);
    CleanupTarget target;
    target.path = absolute;
    target.kind = value.kind;
    target.identity = value.identity;
    target.logical_size = value.logical_size;
    target.allocated_size = value.allocated_size;
    target.allocated_size_known = value.allocated_size_known;
    target.hard_link_count = value.hard_link_count;
    target.hard_link_count_known = value.hard_link_count_known;
    target.symlink = value.kind == FsKind::Symlink;
    target.scan_generation = generation;
    target.key.normalized_path = absolute.generic_string();
    target.key.kind = value.kind;
    target.key.identity = value.identity;
    return target;
}

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

void testCleanupReasonNamesAndEvidence() {
    const std::vector<std::pair<CleanupSkipReason, const char*>> names = {
        {CleanupSkipReason::None, "none"},
        {CleanupSkipReason::MissingSelection, "missing-selection"},
        {CleanupSkipReason::RootTarget, "root-target"},
        {CleanupSkipReason::ProtectedRoot, "protected-root"},
        {CleanupSkipReason::MountBoundary, "mount-boundary"},
        {CleanupSkipReason::SymlinkDescendant, "symlink-descendant"},
        {CleanupSkipReason::IncompleteScan, "incomplete-scan"},
        {CleanupSkipReason::ScannerFiltered, "scanner-filtered"},
        {CleanupSkipReason::StaleGeneration, "stale-generation"},
        {CleanupSkipReason::MetadataUnknown, "metadata-unknown"},
        {CleanupSkipReason::UnsupportedType, "unsupported-type"},
        {CleanupSkipReason::CoveredByParent, "covered-by-parent"},
        {CleanupSkipReason::Missing, "missing"},
        {CleanupSkipReason::IdentityChanged, "identity-changed"},
        {CleanupSkipReason::TypeChanged, "type-changed"},
        {CleanupSkipReason::SizeChanged, "size-changed"},
        {CleanupSkipReason::HardLinkChanged, "hard-link-changed"},
    };
    for (const auto& item : names) {
        CHECK_EQ(std::string(diskmap::cleanupSkipReasonName(item.first)),
                 std::string(item.second));
    }
    CHECK_EQ(std::string(diskmap::cleanupSkipReasonName(
                 static_cast<CleanupSkipReason>(255))),
             std::string("unknown"));

    // A key not present in the retained tree remains a visible rejection.
    ScanResult scan = result(directory("/scan", 100, {file("/scan/item", 101, 1, 512)}));
    NodeKey missing;
    missing.normalized_path = "/scan/not-retained";
    missing.kind = FsKind::RegularFile;
    const auto missingPlan = diskmap::planCleanup(scan, {missing});
    CHECK_EQ(missingPlan.targets.size(), static_cast<std::size_t>(0));
    CHECK(rejectedFor(missingPlan, missing, CleanupSkipReason::MissingSelection));

    // Unsupported entries are rejected by cleanup planning, while a valid
    // identity with incomplete allocation evidence remains selectable but
    // cannot claim an exact reclaimable total.
    FsNode other = file("/scan/socket", 102, 0, 0);
    other.metadata.kind = FsKind::Other;
    const NodeKey otherKey = diskmap::nodeKey(other);
    const auto unsupported = diskmap::planCleanup(
        result(directory("/scan", 103, {std::move(other)})), {otherKey});
    CHECK(rejectedFor(unsupported, otherKey, CleanupSkipReason::UnsupportedType));

    FsNode unknown = file("/scan/unknown", 104, 4, 512);
    unknown.metadata.allocated_size_known = false;
    unknown.allocated_size_known = false;
    const NodeKey unknownKey = diskmap::nodeKey(unknown);
    const auto unknownPlan = diskmap::planCleanup(
        result(directory("/scan", 105, {std::move(unknown)})), {unknownKey});
    CHECK_EQ(unknownPlan.targets.size(), static_cast<std::size_t>(1));
    CHECK(!unknownPlan.reclaimable_bytes_known);

    // Conflicting observations of one physical identity are conservative.
    FsNode left = file("/scan/left", 106, 1, 512, 2);
    FsNode right = file("/scan/right", 106, 1, 1024, 2);
    left.metadata.identity.device = 9;
    right.metadata.identity.device = 9;
    const NodeKey leftKey = diskmap::nodeKey(left);
    const NodeKey rightKey = diskmap::nodeKey(right);
    const auto inconsistent = diskmap::planCleanup(
        result(directory("/scan", 107, {std::move(left), std::move(right)})),
        {leftKey, rightKey});
    CHECK_EQ(inconsistent.targets.size(), static_cast<std::size_t>(2));
    CHECK(!inconsistent.reclaimable_bytes_known);

    // Saturating the identity-aware sum must not wrap to a small value.
    FsNode huge = file("/scan/huge-a", 108, 1,
                       std::numeric_limits<std::uint64_t>::max());
    FsNode hugeOther = file("/scan/huge-b", 109, 1,
                            std::numeric_limits<std::uint64_t>::max());
    const NodeKey hugeKey = diskmap::nodeKey(huge);
    const NodeKey hugeOtherKey = diskmap::nodeKey(hugeOther);
    const auto saturated = diskmap::planCleanup(
        result(directory("/scan", 110, {std::move(huge), std::move(hugeOther)})),
        {hugeKey, hugeOtherKey});
    CHECK_EQ(saturated.reclaimable_bytes,
             std::numeric_limits<std::uint64_t>::max());
    CHECK(!saturated.reclaimable_bytes_known);

    // Structural scan flags on a selected subtree are fail-closed, including
    // flags discovered below an otherwise complete directory.
    FsNode cycleChild = file("/scan/cycle/child", 111, 1, 512);
    cycleChild.cycle_skipped = true;
    FsNode cycleParent = directory("/scan/cycle", 112, {std::move(cycleChild)});
    const NodeKey cycleKey = diskmap::nodeKey(cycleParent);
    const auto cyclePlan = diskmap::planCleanup(
        result(directory("/scan", 113, {std::move(cycleParent)})), {cycleKey});
    CHECK(rejectedFor(cyclePlan, cycleKey, CleanupSkipReason::IncompleteScan));

    FsNode mountChild = file("/scan/mount/child", 114, 1, 512);
    mountChild.mount_boundary_skipped = true;
    FsNode mountParent = directory("/scan/mount", 115, {std::move(mountChild)});
    const NodeKey mountParentKey = diskmap::nodeKey(mountParent);
    const auto mountPlan = diskmap::planCleanup(
        result(directory("/scan", 116, {std::move(mountParent)})),
        {mountParentKey});
    CHECK(rejectedFor(mountPlan, mountParentKey, CleanupSkipReason::IncompleteScan));

    ScanResult cancelledScan =
        result(directory("/scan", 117, {file("/scan/cancelled", 118, 1, 512)}));
    const NodeKey cancelledKey = diskmap::nodeKey(cancelledScan.root.children[0]);
    cancelledScan.cancelled = true;
    CHECK(rejectedFor(diskmap::planCleanup(cancelledScan, {cancelledKey}),
                      cancelledKey, CleanupSkipReason::IncompleteScan));
    cancelledScan.cancelled = false;
    cancelledScan.fatal_error = "listing failed";
    CHECK(rejectedFor(diskmap::planCleanup(cancelledScan, {cancelledKey}),
                      cancelledKey, CleanupSkipReason::IncompleteScan));

    // Exercise both sides of the policy switches and component-aware mount
    // matching without depending on the host's real mount table.
    FsNode protectedChild = file("/scan/protected/item", 119, 1, 512);
    const NodeKey protectedChildKey = diskmap::nodeKey(protectedChild);
    ScanResult policyScan =
        result(directory("/scan", 120, {std::move(protectedChild)}));
    CleanupPolicy policy;
    policy.protected_roots = {"/scan/protected"};
    policy.protect_filesystem_roots = false;
    policy.protect_home_root = false;
    policy.protect_subtrees = false;
    const auto unprotected =
        diskmap::planCleanup(policyScan, {protectedChildKey}, policy);
    CHECK_EQ(unprotected.targets.size(), static_cast<std::size_t>(1));
    policy.protect_subtrees = true;
    CHECK(rejectedFor(diskmap::planCleanup(policyScan, {protectedChildKey}, policy),
                      protectedChildKey, CleanupSkipReason::ProtectedRoot));

    FsNode mounted = file("/scan/mount-target", 121, 1, 512);
    const NodeKey mountedKey = diskmap::nodeKey(mounted);
    ScanResult mountPolicyScan =
        result(directory("/scan", 122, {std::move(mounted)}));
    policy.protected_roots.clear();
    policy.protect_subtrees = false;
    policy.mount_roots = {"/scan/other-mount"};
    CHECK_EQ(diskmap::planCleanup(mountPolicyScan, {mountedKey}, policy)
                 .targets.size(),
             static_cast<std::size_t>(1));
    policy.mount_roots = {"/scan/mount-target"};
    CHECK(rejectedFor(diskmap::planCleanup(mountPolicyScan, {mountedKey}, policy),
                      mountedKey, CleanupSkipReason::MountBoundary));
}

void testTemporaryFilesystemRevalidation(const std::filesystem::path& root) {
    diskmap::RealFsSource source;

    const std::filesystem::path path = root / "reviewed.txt";
    CHECK(writeFile(path, "reviewed content"));
    CleanupTarget target = targetFor(path);
    CHECK(target.identity.valid);
    CHECK(diskmap::revalidateCleanupTarget(target, source).accepted);

    // Deleting the reviewed entry is reported as missing, not as a request to
    // operate on a newly-created path.
    std::error_code error;
    std::filesystem::remove(path, error);
    CHECK(!error);
    const auto missing = diskmap::revalidateCleanupTarget(target, source);
    CHECK(!missing.accepted);
    CHECK(missing.reason == CleanupSkipReason::Missing);

    // Replacing the pathname with a same-size file changes its identity.
    CHECK(writeFile(path, "reviewed content"));
    const CleanupTarget original = targetFor(path);
    CHECK(std::filesystem::remove(path, error));
    CHECK(!error);
    CHECK(writeFile(path, "reviewed content"));
    const auto replacement = diskmap::revalidateCleanupTarget(original, source);
    CHECK(replacement.reason == CleanupSkipReason::IdentityChanged);

    // Allocation evidence is independently part of the final check.
    const CleanupTarget allocationTarget = targetFor(path);
    FakeSource allocationSource;
    FsMetadata allocation = source.inspect(path, false);
    allocation.allocated_size = allocationTarget.allocated_size + 512;
    allocationSource.records[path.generic_string()] = allocation;
    CHECK(diskmap::revalidateCleanupTarget(allocationTarget, allocationSource).reason
          == CleanupSkipReason::SizeChanged);

    // Unknown hard-link evidence is deliberately not strengthened into a
    // rejection by this conservative revalidator.
    CleanupTarget linkEvidenceUnknown = allocationTarget;
    linkEvidenceUnknown.hard_link_count_known = false;
    allocation.allocated_size = allocationTarget.allocated_size;
    allocation.hard_link_count = allocationTarget.hard_link_count + 1;
    allocationSource.records[path.generic_string()] = allocation;
    CHECK(diskmap::revalidateCleanupTarget(linkEvidenceUnknown, allocationSource)
              .accepted);

    const std::filesystem::path alias = root / "reviewed.alias";
    error.clear();
    std::filesystem::create_hard_link(path, alias, error);
    CHECK(!error);
    if (!error) {
        const auto linksChanged =
            diskmap::revalidateCleanupTarget(allocationTarget, source);
        CHECK(linksChanged.reason == CleanupSkipReason::HardLinkChanged);
        std::filesystem::remove(alias, error);
        CHECK(!error);
    }

    const std::filesystem::path directoryPath = root / "reviewed-directory";
    error.clear();
    CHECK(std::filesystem::create_directory(directoryPath, error));
    CHECK(!error);
    CleanupTarget wrongType = targetFor(directoryPath);
    wrongType.kind = FsKind::RegularFile;
    CHECK(diskmap::revalidateCleanupTarget(wrongType, source).reason
          == CleanupSkipReason::TypeChanged);
    std::filesystem::remove(directoryPath, error);
    CHECK(!error);
}

} // namespace

int main() {
    testCleanupReasonNamesAndEvidence();

    ScopedTempDirectory temp;
    CHECK(temp.valid());
    if (temp.valid()) {
        testTemporaryFilesystemRevalidation(temp.path());
    }

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
