// Qt-free tests for the DiskMap explorer view projection.

#include "assert.hpp"
#include "fake_fs.hpp"
#include "diskmap/scanner.hpp"
#include "diskmap/view.hpp"

#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using diskmap::FileIdentity;
using diskmap::FsKind;
using diskmap::FsMetadata;
using diskmap::FsNode;
using diskmap::MetricValue;
using diskmap::NodeIssue;
using diskmap::NodeKey;
using diskmap::SizeMetric;
using diskmap::SortSpec;
using diskmap::ViewFilter;
using diskmap_test::FakeFsSource;
using diskmap_test::makeDirEntry;
using diskmap_test::makeFileEntry;

namespace {

FsNode makeDirectory(std::string name,
                     std::filesystem::path path,
                     std::vector<FsNode> children = {}) {
    FsNode node;
    node.name = std::move(name);
    node.path = std::move(path);
    node.is_dir = true;
    node.metadata.kind = FsKind::Directory;
    node.metadata.complete = true;
    node.children = std::move(children);
    return node;
}

FsNode makeFile(std::string name,
                std::filesystem::path path,
                std::uint64_t logical,
                std::uint64_t allocated = 0,
                bool allocatedKnown = false,
                std::int64_t modified = 0,
                bool modifiedKnown = true,
                FileIdentity identity = {}) {
    FsNode node;
    node.name = std::move(name);
    node.path = std::move(path);
    node.size = logical;
    node.is_dir = false;
    node.metadata.kind = FsKind::RegularFile;
    node.metadata.logical_size = logical;
    node.metadata.allocated_size = allocated;
    node.metadata.allocated_size_known = allocatedKnown;
    node.metadata.hard_link_count = 1;
    node.metadata.hard_link_count_known = true;
    node.metadata.modified_ns = modified;
    node.metadata.modified_time_known = modifiedKnown;
    node.metadata.identity = identity;
    node.metadata.complete = true;
    node.complete = true;
    return node;
}

FsNode makeSymlink(std::string name,
                  std::filesystem::path path,
                  std::uint64_t targetLogical,
                  FileIdentity targetIdentity,
                  bool followed) {
    FsNode node;
    node.name = std::move(name);
    node.path = std::move(path);
    node.is_dir = false;
    node.size = targetLogical;
    node.followed = followed;
    node.metadata.kind = FsKind::Symlink;
    node.metadata.logical_size = 12; // link payload, not target bytes
    node.metadata.identity = FileIdentity{7, 700, true};
    node.metadata.modified_time_known = true;
    node.metadata.complete = true;
    node.has_target_metadata = true;
    node.target_metadata.kind = FsKind::RegularFile;
    node.target_metadata.logical_size = targetLogical;
    node.target_metadata.identity = targetIdentity;
    node.target_metadata.allocated_size = 4096;
    node.target_metadata.allocated_size_known = true;
    node.target_metadata.hard_link_count = 2;
    node.target_metadata.hard_link_count_known = true;
    node.target_metadata.modified_ns = 20;
    node.target_metadata.modified_time_known = true;
    node.target_metadata.complete = true;
    node.complete = true;
    return node;
}

const FsNode* findByName(const std::vector<const FsNode*>& nodes, const std::string& name) {
    for (const FsNode* node : nodes) {
        if (node != nullptr && node->name == name) {
            return node;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    // --- metric selection and logical completeness -----------------------
    {
        FsNode root = makeDirectory("root", "/tmp/root", {
            makeFile("sparse file", "/tmp/root/sparse file", 8193, 4096, true),
        });
        diskmap::aggregateSizes(root);
        diskmap::aggregateStorage(root);

        CHECK_EQ(diskmap::metricValue(root, SizeMetric::Logical),
                 (MetricValue{8193, true}));
        CHECK_EQ(diskmap::metricValue(root, SizeMetric::Allocated),
                 (MetricValue{4096, true, false}));
        CHECK_EQ(diskmap::metricValue(root, SizeMetric::Reclaimable),
                 (MetricValue{4096, true, false}));
        CHECK(diskmap::metricValue(root, SizeMetric::Logical).additive);
        CHECK(!diskmap::metricValue(root, SizeMetric::Allocated).additive);

        root.children.front().complete = false;
        CHECK_EQ(diskmap::metricValue(root, SizeMetric::Logical),
                 (MetricValue{8193, false}));
        root.children.front().complete = true;
        root.children.front().cycle_skipped = true;
        CHECK_EQ(diskmap::metricValue(root, SizeMetric::Logical),
                 (MetricValue{8193, false}));
        root.children.front().cycle_skipped = false;
        root.children.front().error = "scan depth limit reached";
        root.children.front().complete = false;
        CHECK_EQ(diskmap::metricValue(root, SizeMetric::Logical),
                 (MetricValue{8193, false}));

        // Scanner filtering is a separate provenance bit. It is not inferred
        // from a view's own min_size/search predicates.
        root.children.front().complete = true;
        root.children.front().error.clear();
        CHECK_EQ(diskmap::metricValue(root, SizeMetric::Logical, true),
                 (MetricValue{8193, false}));
        CHECK(diskmap::metricValue(root.children.front(), SizeMetric::Logical, true).known);
    }

    // --- hard links, symlink target facts, and unknown physical totals ----
    {
        const FileIdentity shared{42, 100, true};
        FsNode root = makeDirectory("storage", "/tmp/storage", {
            makeFile("first", "/tmp/storage/first", 100, 4096, true, 10, true, shared),
            makeFile("second", "/tmp/storage/second", 100, 4096, true, 11, true, shared),
            makeSymlink("alias", "/tmp/storage/alias", 100, shared, true),
        });
        diskmap::aggregateSizes(root);
        diskmap::aggregateStorage(root);

        // The hard-linked allocation is counted once. The followed symlink is
        // an alias and does not own a hard-link reference.
        CHECK_EQ(root.allocated_size, static_cast<std::uint64_t>(4096));
        CHECK(root.allocated_size_known);
        CHECK_EQ(root.reclaimable_size, static_cast<std::uint64_t>(0));
        CHECK(!root.reclaimable_size_known);

        const FsNode& alias = root.children[2];
        CHECK_EQ(diskmap::nodeKind(alias), FsKind::Symlink);
        CHECK(diskmap::metricValue(alias, SizeMetric::Allocated).known);
        CHECK_EQ(diskmap::normalizedPath(alias), std::string("/tmp/storage/alias"));

        FsNode unknown = makeFile("unknown", "/tmp/storage/unknown", 5, 0, false);
        CHECK(!diskmap::metricValue(unknown, SizeMetric::Allocated).known);
        CHECK(!diskmap::metricValue(unknown, SizeMetric::Reclaimable).known);
    }

    // Logical aggregation saturates on uint64 overflow and carries the loss
    // of exactness into the view metric instead of exposing a wrapped value.
    {
        constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
        FsNode root = makeDirectory("logical overflow", "/tmp/logical-overflow", {
            makeFile("maximum", "/tmp/logical-overflow/maximum", maximum),
            makeFile("one more", "/tmp/logical-overflow/one-more", 1),
        });
        CHECK_EQ(diskmap::aggregateSizes(root), maximum);
        CHECK_EQ(root.size, maximum);
        CHECK(!root.logical_size_known);
        CHECK_EQ(diskmap::metricValue(root, SizeMetric::Logical),
                 (MetricValue{maximum, false}));
    }

    // Numeric storage fields can be left over from a previous aggregation;
    // an incomplete node must still expose both physical metrics as unknown.
    {
        FsNode partial = makeFile("partial", "/tmp/partial", 12, 4096, true);
        partial.allocated_size = 4096;
        partial.reclaimable_size = 2048;
        partial.reclaimable_size_known = true;
        partial.complete = false;
        CHECK_EQ(diskmap::metricValue(partial, SizeMetric::Allocated),
                 (MetricValue{4096, false, false}));
        CHECK_EQ(diskmap::metricValue(partial, SizeMetric::Reclaimable),
                 (MetricValue{2048, false, false}));
    }

    // View metric validation walks current descendants instead of trusting a
    // stale aggregate known bit after the public value tree is modified.
    {
        FsNode root = makeDirectory("stale", "/tmp/stale", {
            makeFile("child", "/tmp/stale/child", 12, 4096, true),
        });
        diskmap::aggregateSizes(root);
        diskmap::aggregateStorage(root);
        CHECK(diskmap::metricValue(root, SizeMetric::Allocated).known);
        root.children.front().complete = false;
        CHECK(!diskmap::metricValue(root, SizeMetric::Allocated).known);
        CHECK(!diskmap::metricValue(root, SizeMetric::Reclaimable).known);

        root.children.front().complete = true;
        root.children.front().metadata.complete = false;
        CHECK(!diskmap::metricValue(root, SizeMetric::Logical).known);
        CHECK(!diskmap::metricValue(root, SizeMetric::Allocated).known);
    }

    // --- deterministic normalized NodeKey and optional identity ---------
    {
        FsNode normalized = makeFile("child", "/tmp/a/../a/child", 4);
        NodeKey key = diskmap::nodeKey(normalized);
        CHECK_EQ(key.normalized_path, std::string("/tmp/a/child"));
        CHECK_EQ(key.kind, FsKind::RegularFile);
        CHECK(!key.followed);
        CHECK(!key.identity.has_value());

        FsNode samePath = makeFile("child", "/tmp/a/child", 9);
        CHECK(key == diskmap::nodeKey(samePath));

        normalized.metadata.identity = FileIdentity{3, 9, true};
        NodeKey withIdentity = diskmap::nodeKey(normalized);
        CHECK(withIdentity.identity.has_value());
        CHECK(withIdentity != key);

        normalized.followed = true;
        normalized.has_target_metadata = true;
        normalized.target_metadata = normalized.metadata;
        normalized.target_metadata.kind = FsKind::RegularFile;
        NodeKey followed = diskmap::nodeKey(normalized);
        CHECK(followed.followed);
        CHECK(followed != withIdentity);

        normalized.target_metadata.identity = FileIdentity{};
        NodeKey invalidTarget = diskmap::nodeKey(normalized);
        CHECK(invalidTarget.identity.has_value());
        CHECK_EQ(invalidTarget.identity->device, static_cast<std::uint64_t>(3));
        CHECK(invalidTarget == followed);

        FsNode trailingSlash = makeFile("child", "/tmp/a/child/", 9);
        CHECK_EQ(diskmap::normalizedPath(trailingSlash), std::string("/tmp/a/child"));
        CHECK(diskmap::nodeKey(trailingSlash) == key);
    }

    // --- issue classification preserves cycle/depth/mount distinctions ---
    {
        FsNode node = makeDirectory("node", "/tmp/node");
        CHECK_EQ(diskmap::classifyNodeIssue(node), NodeIssue::None);
        node.cycle_skipped = true;
        CHECK_EQ(diskmap::classifyNodeIssue(node), NodeIssue::CycleSkipped);
        node.cycle_skipped = false;
        node.mount_boundary_skipped = true;
        CHECK_EQ(diskmap::classifyNodeIssue(node), NodeIssue::MountBoundarySkipped);
        node.mount_boundary_skipped = false;
        node.error = "scan depth limit reached";
        node.complete = false;
        CHECK_EQ(diskmap::classifyNodeIssue(node), NodeIssue::DepthLimitReached);
        node.error = "cannot open directory";
        CHECK_EQ(diskmap::classifyNodeIssue(node), NodeIssue::Incomplete);
        node.complete = true;
        node.error.clear();
        node.metadata.complete = false;
        node.metadata.error = "metadata unavailable";
        CHECK_EQ(diskmap::classifyNodeIssue(node), NodeIssue::MetadataUnknown);
        node.metadata.error.clear();
        node.metadata.complete = true;
        CHECK_EQ(diskmap::classifyNodeIssue(node, true), NodeIssue::ScannerFiltered);

        FsNode noMetadataError = makeFile("metadata-missing", "/tmp/metadata-missing", 1);
        noMetadataError.metadata.complete = false;
        noMetadataError.metadata.error.clear();
        CHECK_EQ(diskmap::classifyNodeIssue(noMetadataError), NodeIssue::MetadataUnknown);
    }

    // The canonical optional metric wins even when a compatibility alias is
    // populated; explicit Logical must not be mistaken for an unset value.
    {
        FsNode root = makeDirectory("metric aliases", "/tmp/metric-aliases", {
            makeFile("logical winner", "/tmp/metric-aliases/logical", 100, 1, true),
            makeFile("allocated winner", "/tmp/metric-aliases/allocated", 10, 100, true),
        });
        diskmap::aggregateSizes(root);
        diskmap::aggregateStorage(root);

        ViewFilter logical;
        logical.metric = SizeMetric::Logical;
        logical.size_metric = SizeMetric::Allocated;
        logical.min_size = 50;
        std::vector<const FsNode*> visible = diskmap::visibleChildren(root, logical);
        CHECK_EQ(visible.size(), static_cast<std::size_t>(1));
        if (visible.size() == 1) {
            CHECK_EQ(visible.front()->name, std::string("logical winner"));
        }

        ViewFilter allocated;
        allocated.metric = SizeMetric::Allocated;
        allocated.size_metric = SizeMetric::Logical;
        allocated.min_size = 50;
        visible = diskmap::visibleChildren(root, allocated);
        CHECK_EQ(visible.size(), static_cast<std::size_t>(1));
        if (visible.size() == 1) {
            CHECK_EQ(visible.front()->name, std::string("allocated winner"));
        }
    }

    // --- visibleChildren: literal case-insensitive path/name search and
    // conjunctive type/size/age/issue filters ------------------------------
    {
        FsNode root = makeDirectory("root dir", "/tmp/Explorer Root", {
            makeFile("Alpha Report.TXT", "/tmp/Explorer Root/Alpha Report.TXT", 30, 30, true, 10),
            makeFile("beta.log", "/tmp/Explorer Root/sub dir/beta.log", 20, 20, true, 20),
            makeFile("γ data.bin", "/tmp/Explorer Root/γ data.bin", 40, 40, true, 30),
            makeDirectory("nested", "/tmp/Explorer Root/nested"),
        });
        // The direct children intentionally include one path with a nested
        // lexical segment, which exercises normalized full-path matching.
        diskmap::aggregateSizes(root);
        diskmap::aggregateStorage(root);

        ViewFilter text;
        text.search = "REPORT";
        std::vector<const FsNode*> visible = diskmap::visibleChildren(root, text);
        CHECK_EQ(visible.size(), static_cast<std::size_t>(1));
        CHECK_EQ(visible[0]->name, std::string("Alpha Report.TXT"));

        text.search = "explorer root";
        visible = diskmap::visibleChildren(root, text);
        CHECK_EQ(visible.size(), static_cast<std::size_t>(4));

        text.search = "[not a regex]";
        CHECK(diskmap::visibleChildren(root, text).empty());

        ViewFilter combined;
        combined.search = "explorer root";
        combined.kind = FsKind::RegularFile;
        combined.min_size = 25;
        combined.max_size = 35;
        combined.modified_after_ns = 5;
        combined.modified_before_ns = 15;
        visible = diskmap::visibleChildren(root, combined);
        CHECK_EQ(visible.size(), static_cast<std::size_t>(1));
        CHECK_EQ(visible[0]->name, std::string("Alpha Report.TXT"));

        combined.max_size = 29;
        CHECK(diskmap::visibleChildren(root, combined).empty());

        // Unknown size and age values never match an active bound.
        root.children[1].allocated_size_known = false;
        ViewFilter unknownSize;
        unknownSize.metric = SizeMetric::Allocated;
        unknownSize.min_size = 0;
        unknownSize.max_size = 100;
        unknownSize.search = "beta";
        CHECK(diskmap::visibleChildren(root, unknownSize).empty());

        root.children[0].metadata.modified_time_known = false;
        ViewFilter unknownAge;
        unknownAge.modified_after_ns = 0;
        unknownAge.search = "alpha";
        CHECK(diskmap::visibleChildren(root, unknownAge).empty());
        root.children[0].metadata.modified_time_known = true;
        root.children[0].complete = false;
        CHECK(diskmap::visibleChildren(root, unknownAge).empty());
        root.children[0].complete = true;

        ViewFilter issue;
        issue.issue = NodeIssue::None;
        issue.kind = FsKind::RegularFile;
        visible = diskmap::visibleChildren(root, issue);
        CHECK_EQ(visible.size(), static_cast<std::size_t>(3));
    }

    // --- size ordering: unknowns last in both directions, ties by key ----
    {
        FsNode root = makeDirectory("root", "/tmp/order", {
            makeFile("zeta", "/tmp/order/zeta", 10, 0, false),
            makeFile("same-b", "/tmp/order/same-b", 20, 20, true),
            makeFile("same-a", "/tmp/order/same-a", 20, 20, true),
            makeFile("small", "/tmp/order/small", 5, 5, true),
        });
        diskmap::aggregateSizes(root);
        diskmap::aggregateStorage(root);

        ViewFilter filter;
        SortSpec descending;
        descending.metric = SizeMetric::Allocated;
        descending.descending = true;
        std::vector<const FsNode*> sorted = diskmap::visibleChildren(root, filter, descending);
        CHECK_EQ(sorted.size(), static_cast<std::size_t>(4));
        CHECK_EQ(sorted[0]->name, std::string("same-a"));
        CHECK_EQ(sorted[1]->name, std::string("same-b"));
        CHECK_EQ(sorted[2]->name, std::string("small"));
        CHECK_EQ(sorted[3]->name, std::string("zeta"));

        SortSpec ascending = descending;
        ascending.descending = false;
        sorted = diskmap::visibleChildren(root, filter, ascending);
        CHECK_EQ(sorted[0]->name, std::string("small"));
        CHECK_EQ(sorted[1]->name, std::string("same-a"));
        CHECK_EQ(sorted[2]->name, std::string("same-b"));
        CHECK_EQ(sorted[3]->name, std::string("zeta"));
    }

    // --- largestFiles: regular files only, recursive, filtered and bounded
    {
        const FileIdentity identity{9, 90, true};
        FsNode root = makeDirectory("root", "/tmp/largest", {
            makeDirectory("sub", "/tmp/largest/sub", {
                makeFile("large.bin", "/tmp/largest/sub/large.bin", 100, 100, true, 100),
                makeFile("small.bin", "/tmp/largest/sub/small.bin", 10, 10, true, 10),
                makeSymlink("alias.bin", "/tmp/largest/sub/alias.bin", 100, identity, true),
            }),
            makeDirectory("empty", "/tmp/largest/empty"),
            makeFile("root file", "/tmp/largest/root file", 50, 50, true, 50),
        });
        diskmap::aggregateSizes(root);
        diskmap::aggregateStorage(root);

        SortSpec sort;
        sort.metric = SizeMetric::Logical;
        diskmap::LargestFilesResult largestResult = diskmap::largestFiles(root, 2, {}, sort);
        CHECK(largestResult.complete);
        CHECK_EQ(largestResult.files.size(), static_cast<std::size_t>(2));
        CHECK_EQ(largestResult.files[0]->name, std::string("large.bin"));
        CHECK_EQ(largestResult.files[1]->name, std::string("root file"));
        CHECK(findByName(largestResult.files, "alias.bin") == nullptr);

        ViewFilter filter;
        filter.search = "ROOT FILE";
        largestResult = diskmap::largestFiles(root, filter, 10);
        CHECK(largestResult.complete);
        CHECK_EQ(largestResult.files.size(), static_cast<std::size_t>(1));
        CHECK_EQ(largestResult.files[0]->name, std::string("root file"));

        filter.search = "sub/large";
        largestResult = diskmap::largestFiles(root, 10, filter);
        CHECK(largestResult.complete);
        CHECK_EQ(largestResult.files.size(), static_cast<std::size_t>(1));
        CHECK_EQ(largestResult.files[0]->name, std::string("large.bin"));

        filter.search.clear();
        filter.min_size = 101;
        CHECK(diskmap::largestFiles(root, 10, filter).files.empty());
        CHECK(diskmap::largestFiles(root, 0).files.empty());
    }

    // Largest-files projection does not claim an exhaustive ranking when a
    // branch is incomplete, cyclic, mount-pruned, or depth-pruned. Such
    // branches are skipped, while complete siblings remain useful.
    {
        FsNode incomplete = makeDirectory("incomplete", "/tmp/issues/incomplete", {
            makeFile("hidden-incomplete", "/tmp/issues/incomplete/hidden", 1000),
        });
        incomplete.complete = false;

        FsNode cycle = makeDirectory("cycle", "/tmp/issues/cycle", {
            makeFile("hidden-cycle", "/tmp/issues/cycle/hidden", 900),
        });
        cycle.cycle_skipped = true;

        FsNode mount = makeDirectory("mount", "/tmp/issues/mount", {
            makeFile("hidden-mount", "/tmp/issues/mount/hidden", 800),
        });
        mount.mount_boundary_skipped = true;
        mount.complete = false;

        FsNode depth = makeDirectory("depth", "/tmp/issues/depth", {
            makeFile("hidden-depth", "/tmp/issues/depth/hidden", 700),
        });
        depth.error = "scan depth limit reached";
        depth.complete = false;

        FsNode root = makeDirectory("issues", "/tmp/issues", {
            incomplete,
            cycle,
            mount,
            depth,
            makeFile("visible", "/tmp/issues/visible", 10),
        });
        const diskmap::LargestFilesResult result = diskmap::largestFiles(root, 10);
        CHECK(!result.complete);
        CHECK_EQ(result.files.size(), static_cast<std::size_t>(1));
        if (result.files.size() == 1) {
            CHECK_EQ(result.files.front()->name, std::string("visible"));
        }
        const diskmap::LargestFilesResult zeroLimit = diskmap::largestFiles(root, 0);
        CHECK(zeroLimit.files.empty());
        CHECK(!zeroLimit.complete);

        ViewFilter filtered;
        filtered.scanner_totals_filtered = true;
        const diskmap::LargestFilesResult filteredResult =
            diskmap::largestFiles(root, 10, filtered);
        CHECK(!filteredResult.complete);
    }

    // A scanner-produced listing failure must remain visible as an incomplete
    // node and make a largest-files ranking partial without hiding good files.
    {
        FakeFsSource fs;
        fs.addListing("/scanner-view/root", {
            makeDirEntry("blocked"),
            makeFileEntry("kept.bin", 42),
        });
        fs.addError("/scanner-view/root/blocked", "permission denied: blocked");

        const diskmap::ScanResult scanned =
            diskmap::scan(fs, "/scanner-view/root", diskmap::ScanOptions{});
        const FsNode* blocked = diskmap::findChild(scanned.root, "blocked");
        CHECK(blocked != nullptr);
        if (blocked != nullptr) {
            CHECK(!blocked->complete);
            CHECK_EQ(diskmap::classifyNodeIssue(*blocked), NodeIssue::Incomplete);
        }
        const diskmap::LargestFilesResult result = diskmap::largestFiles(scanned.root, 10);
        CHECK(!result.complete);
        CHECK_EQ(result.files.size(), static_cast<std::size_t>(1));
        if (result.files.size() == 1) {
            CHECK_EQ(result.files.front()->name, std::string("kept.bin"));
        }
    }

    // Unknown values cannot silently produce an "exhaustive" largest-files
    // result when they affect filtering or ordering. A decisive text mismatch
    // remains a complete exclusion because later conjunctive fields are moot.
    {
        FsNode root = makeDirectory("unknowns", "/tmp/unknowns", {
            makeFile("selected", "/tmp/unknowns/selected", 20, 20, true, 20, true),
            makeFile("unknown-size", "/tmp/unknowns/unknown-size", 30),
            makeFile("unknown-age", "/tmp/unknowns/unknown-age", 10, 10, true, 0, false),
        });
        diskmap::aggregateSizes(root);
        diskmap::aggregateStorage(root);

        SortSpec physicalSort;
        physicalSort.metric = SizeMetric::Allocated;
        diskmap::LargestFilesResult result =
            diskmap::largestFiles(root, 10, {}, physicalSort);
        CHECK(!result.complete);
        CHECK_EQ(result.files.size(), static_cast<std::size_t>(3));
        CHECK_EQ(result.files.front()->name, std::string("selected"));

        ViewFilter sizeBound;
        sizeBound.metric = SizeMetric::Allocated;
        sizeBound.min_size = 0;
        result = diskmap::largestFiles(root, 10, sizeBound);
        CHECK(!result.complete);
        CHECK_EQ(result.files.size(), static_cast<std::size_t>(2));
        CHECK(findByName(result.files, "unknown-size") == nullptr);

        ViewFilter ageBound;
        ageBound.modified_after_ns = 0;
        result = diskmap::largestFiles(root, 10, ageBound);
        CHECK(!result.complete);
        CHECK_EQ(result.files.size(), static_cast<std::size_t>(2));
        CHECK(findByName(result.files, "unknown-age") == nullptr);

        ViewFilter decisiveSearch;
        decisiveSearch.search = "selected";
        decisiveSearch.modified_after_ns = 0;
        result = diskmap::largestFiles(root, 10, decisiveSearch);
        CHECK(result.complete);
        CHECK_EQ(result.files.size(), static_cast<std::size_t>(1));
        CHECK_EQ(result.files.front()->name, std::string("selected"));
    }

    // Hard-linked entries are one logical candidate. The representative is
    // selected by the stable NodeKey ordering, independent of child order.
    {
        const FileIdentity shared{55, 550, true};
        FsNode root = makeDirectory("hardlinks", "/tmp/hardlinks", {
            makeFile("zeta", "/tmp/hardlinks/zeta", 100, 100, true, 0, true, shared),
            makeFile("alpha", "/tmp/hardlinks/alpha", 100, 100, true, 0, true, shared),
            makeFile("unique", "/tmp/hardlinks/unique", 90, 90, true),
        });

        diskmap::LargestFilesResult result = diskmap::largestFiles(root, 10);
        CHECK(result.complete);
        CHECK_EQ(result.files.size(), static_cast<std::size_t>(2));
        CHECK_EQ(result.files[0]->name, std::string("alpha"));
        CHECK_EQ(result.files[1]->name, std::string("unique"));

        std::swap(root.children[0], root.children[1]);
        result = diskmap::largestFiles(root, 10);
        CHECK_EQ(result.files.size(), static_cast<std::size_t>(2));
        CHECK_EQ(result.files[0]->name, std::string("alpha"));
        CHECK_EQ(result.files[1]->name, std::string("unique"));
    }

    // The output cardinality stays bounded by limit even for a large input;
    // this is the functional contract behind the bounded top-N candidate set.
    {
        std::vector<FsNode> children;
        children.reserve(512);
        for (std::uint64_t index = 0; index < 512; ++index) {
            const std::string name = "candidate-" + std::to_string(index);
            children.push_back(makeFile(name, "/tmp/candidates/" + name, index));
        }
        FsNode root = makeDirectory("candidates", "/tmp/candidates", std::move(children));
        const diskmap::LargestFilesResult result = diskmap::largestFiles(root, 5);
        CHECK(result.complete);
        CHECK_EQ(result.files.size(), static_cast<std::size_t>(5));
        if (result.files.size() == 5) {
            CHECK_EQ(result.files[0]->size, static_cast<std::uint64_t>(511));
            CHECK_EQ(result.files[1]->size, static_cast<std::uint64_t>(510));
            CHECK_EQ(result.files[2]->size, static_cast<std::uint64_t>(509));
            CHECK_EQ(result.files[3]->size, static_cast<std::uint64_t>(508));
            CHECK_EQ(result.files[4]->size, static_cast<std::uint64_t>(507));
        }
    }

    // Unicode and space-bearing paths are preserved in keys/search. ASCII
    // case folding applies to the surrounding path while UTF-8 bytes remain
    // literal and deterministic.
    {
        FsNode root = makeDirectory("자료 root", "/tmp/Space Dir/자료 root", {
            makeFile("Résumé.txt", "/tmp/Space Dir/자료 root/Résumé.txt", 7, 7, true),
        });
        ViewFilter path;
        path.search = "space dir/자료";
        CHECK_EQ(diskmap::visibleChildren(root, path).size(), static_cast<std::size_t>(1));
        CHECK_EQ(diskmap::nodeKey(root.children.front()).normalized_path,
                 std::string("/tmp/Space Dir/자료 root/Résumé.txt"));
    }

    return testSummary();
}
