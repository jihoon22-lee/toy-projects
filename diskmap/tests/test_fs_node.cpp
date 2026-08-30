// Tests for diskmap::FsNode helpers (src/core/fs_node.hpp / fs_node.cpp):
// aggregateSizes, sortBySizeDesc, findChild, countNodes, topFiles.

#include "assert.hpp"
#include "fake_fs.hpp"
#include "diskmap/fs_node.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using diskmap::FsNode;
using diskmap::FsKind;
using diskmap::FsMetadata;
using diskmap::FileIdentity;
using diskmap_test::makeDirNode;
using diskmap_test::makeFileNode;

namespace {

// Builds a chain of `depth` nested single-child directories wrapped around
// `leaf`, from the inside out (a loop, not recursion), so this stays safe to
// call even for depths well past kMaxTreeDepth.
FsNode makeChain(int depth, FsNode leaf) {
    FsNode node = std::move(leaf);
    for (int i = 0; i < depth; ++i) {
        FsNode parent = makeDirNode("d" + std::to_string(i), {});
        parent.children.push_back(std::move(node));
        node = std::move(parent);
    }
    return node;
}

FsNode storageFile(std::string name,
                   FileIdentity fileIdentity,
                   std::uint64_t allocatedSize,
                   bool allocatedKnown,
                   std::uint64_t hardLinkCount,
                   bool hardLinksKnown,
                   bool complete = true) {
    FsNode node = makeFileNode(std::move(name), 1);
    node.metadata.identity = fileIdentity;
    node.metadata.allocated_size = allocatedSize;
    node.metadata.allocated_size_known = allocatedKnown;
    node.metadata.hard_link_count = hardLinkCount;
    node.metadata.hard_link_count_known = hardLinksKnown;
    node.metadata.complete = complete;
    return node;
}

} // namespace

int main() {
    // --- identity and node metadata model: link and target facts coexist ---
    {
        const FileIdentity first{17, 23, true};
        const FileIdentity same{17, 23, true};
        const FileIdentity differentFile{17, 24, true};
        const FileIdentity invalid{0, 0, false};
        CHECK(first == same);
        CHECK(first != differentFile);
        CHECK(first != invalid);

        FsNode link = makeFileNode("link", 5);
        link.path = "/tmp/diskmap/link";
        link.metadata.kind = FsKind::Symlink;
        link.metadata.identity = first;
        link.metadata.logical_size = 23;
        link.metadata.allocated_size = 0;
        link.metadata.allocated_size_known = true;
        link.metadata.hard_link_count = 1;
        link.metadata.hard_link_count_known = true;
        link.metadata.permissions = 0777;
        link.metadata.permissions_known = true;
        link.metadata.owner = 1000;
        link.metadata.group = 1000;
        link.metadata.ownership_known = true;
        link.metadata.modified_ns = 123456789;
        link.metadata.modified_time_known = true;
        link.metadata.complete = true;
        link.has_target_metadata = true;
        link.target_metadata.kind = FsKind::RegularFile;
        link.target_metadata.identity = differentFile;
        link.target_metadata.logical_size = 5;
        link.target_metadata.complete = true;
        link.followed = true;
        link.complete = true;
        link.error.clear();

        CHECK_EQ(link.name, std::string("link"));
        CHECK_EQ(link.path, std::string("/tmp/diskmap/link"));
        CHECK(!link.is_dir);
        CHECK_EQ(link.size, static_cast<std::uint64_t>(5));
        CHECK_EQ(link.metadata.kind, FsKind::Symlink);
        CHECK(link.metadata.identity == first);
        CHECK(link.has_target_metadata);
        CHECK_EQ(link.target_metadata.kind, FsKind::RegularFile);
        CHECK(link.target_metadata.identity == differentFile);
        CHECK(link.target_metadata.identity != link.metadata.identity);
        CHECK(link.followed);
        CHECK(link.complete);
        CHECK(link.error.empty());

        // A failed target lookup is represented on the node without erasing
        // the link's own identity or kind.
        link.has_target_metadata = false;
        link.target_metadata = FsMetadata{};
        link.target_metadata.error = "target does not exist";
        link.target_metadata.complete = false;
        link.complete = false;
        link.error = link.target_metadata.error;
        CHECK_EQ(link.metadata.kind, FsKind::Symlink);
        CHECK(link.metadata.identity == first);
        CHECK(!link.has_target_metadata);
        CHECK(!link.target_metadata.complete);
        CHECK(!link.target_metadata.error.empty());
        CHECK(!link.complete);
        CHECK_EQ(link.error, std::string("target does not exist"));
    }

    // --- aggregateSizes: post-order sum, sets directory sizes ---
    {
        FsNode root = makeDirNode("root", {
            makeFileNode("a.txt", 10),
            makeDirNode("sub", {
                makeFileNode("b.txt", 20),
                makeFileNode("c.txt", 5),
            }),
            makeDirNode("emptyDir", {}),
        });
        const std::uint64_t total = diskmap::aggregateSizes(root);
        CHECK_EQ(total, static_cast<std::uint64_t>(35));
        CHECK_EQ(root.size, static_cast<std::uint64_t>(35));
        CHECK_EQ(root.children[1].size, static_cast<std::uint64_t>(25)); // sub
        CHECK_EQ(root.children[2].size, static_cast<std::uint64_t>(0));  // emptyDir

        // aggregating a lone file just returns its own size unchanged.
        FsNode file = makeFileNode("solo.bin", 99);
        CHECK_EQ(diskmap::aggregateSizes(file), static_cast<std::uint64_t>(99));
        CHECK_EQ(file.size, static_cast<std::uint64_t>(99));
    }

    // Logical bytes have no separate known bit, so overflow must saturate
    // instead of wrapping to a deceptively small directory size.
    {
        constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
        FsNode root = makeDirNode("logical-overflow", {
            makeFileNode("maximum", maximum),
            makeFileNode("one-more", 1),
        });
        CHECK_EQ(diskmap::aggregateSizes(root), maximum);
        CHECK_EQ(root.size, maximum);
    }

    // --- sortBySizeDesc: size desc, ties broken by name asc, recursive ---
    {
        FsNode root = makeDirNode("root", {
            makeFileNode("small", 5),
            makeFileNode("big", 100),
            makeFileNode("tieB", 10),
            makeFileNode("tieA", 10),
            makeDirNode("sub", {
                makeFileNode("subSmall", 1),
                makeFileNode("subBig", 50),
            }),
        });
        diskmap::sortBySizeDesc(root);

        CHECK_EQ(root.children[0].name, std::string("big"));   // 100
        CHECK_EQ(root.children[1].name, std::string("tieA"));  // 10, tie -> name asc
        CHECK_EQ(root.children[2].name, std::string("tieB"));  // 10
        CHECK_EQ(root.children[3].name, std::string("small")); // 5
        const FsNode* sub = diskmap::findChild(root, "sub");
        CHECK(sub != nullptr);
        if (sub) {
            CHECK_EQ(sub->children[0].name, std::string("subBig"));
            CHECK_EQ(sub->children[1].name, std::string("subSmall"));
        }
    }

    // --- findChild: present and absent, and on a leaf with no children ---
    {
        const FsNode root = makeDirNode("root", {makeFileNode("only", 1)});
        CHECK(diskmap::findChild(root, "only") != nullptr);
        CHECK(diskmap::findChild(root, "missing") == nullptr);

        const FsNode leaf = makeFileNode("leaf.txt", 3);
        CHECK(diskmap::findChild(leaf, "anything") == nullptr);
    }

    // --- countNodes: node plus every descendant ---
    {
        const FsNode single = makeFileNode("solo", 1);
        CHECK_EQ(diskmap::countNodes(single), static_cast<std::size_t>(1));

        const FsNode root = makeDirNode("root", {
            makeFileNode("a", 1),
            makeDirNode("sub", {makeFileNode("b", 2), makeFileNode("c", 3)}),
        });
        // root + a + sub + b + c = 5
        CHECK_EQ(diskmap::countNodes(root), static_cast<std::size_t>(5));
    }

    // --- topFiles: n largest FILES (never directories), sorted desc ---
    {
        FsNode root = makeDirNode("root", {
            makeFileNode("small.txt", 5),
            makeDirNode("sub", {
                makeFileNode("huge.bin", 1000),
                makeFileNode("mid.bin", 50),
            }),
            makeFileNode("zero.txt", 0),
        });

        const std::vector<const FsNode*> top2 = diskmap::topFiles(root, 2);
        CHECK_EQ(top2.size(), static_cast<std::size_t>(2));
        CHECK_EQ(top2[0]->name, std::string("huge.bin"));
        CHECK_EQ(top2[1]->name, std::string("mid.bin"));

        // n larger than the number of files: returns every file, no crash.
        const std::vector<const FsNode*> topAll = diskmap::topFiles(root, 100);
        CHECK_EQ(topAll.size(), static_cast<std::size_t>(4));

        // n == 0: empty result.
        const std::vector<const FsNode*> topNone = diskmap::topFiles(root, 0);
        CHECK_EQ(topNone.size(), static_cast<std::size_t>(0));

        // a directory itself is never returned, even as the sole top-level node.
        const FsNode emptyDir = makeDirNode("emptyDir", {});
        CHECK_EQ(diskmap::topFiles(emptyDir, 5).size(), static_cast<std::size_t>(0));

        // topFiles on a lone file returns just that file.
        const FsNode lone = makeFileNode("lone", 7);
        const std::vector<const FsNode*> topLone = diskmap::topFiles(lone, 5);
        CHECK_EQ(topLone.size(), static_cast<std::size_t>(1));
        CHECK_EQ(topLone[0]->name, std::string("lone"));
    }

    // --- identity-free files are safely reclaimable only when the adapter
    // proves that they have exactly one link ---
    {
        FsNode oneLink = storageFile("one-link", FileIdentity{}, 2048, true, 1, true);
        diskmap::aggregateStorage(oneLink);
        CHECK_EQ(oneLink.allocated_size, static_cast<std::uint64_t>(2048));
        CHECK(oneLink.allocated_size_known);
        CHECK_EQ(oneLink.reclaimable_size, static_cast<std::uint64_t>(2048));
        CHECK(oneLink.reclaimable_size_known);

        FsNode multiLink = storageFile("multi-link", FileIdentity{}, 2048, true, 2, true);
        diskmap::aggregateStorage(multiLink);
        CHECK_EQ(multiLink.allocated_size, static_cast<std::uint64_t>(0));
        CHECK(!multiLink.allocated_size_known);
        CHECK_EQ(multiLink.reclaimable_size, static_cast<std::uint64_t>(0));
        CHECK(!multiLink.reclaimable_size_known);
    }

    // --- physical metadata conflicts never become a false precise answer ---
    {
        const FileIdentity shared{21, 210, true};
        FsNode root = makeDirNode("conflict", {
            storageFile("first", shared, 4096, true, 2, true),
            storageFile("second", shared, 8192, true, 3, true),
        });
        diskmap::aggregateStorage(root);
        // Conflicting observations are intentionally represented as unknown;
        // the aggregate must not advertise either observation as authoritative.
        CHECK_EQ(root.allocated_size, static_cast<std::uint64_t>(0));
        CHECK(!root.allocated_size_known);
        CHECK_EQ(root.reclaimable_size, static_cast<std::uint64_t>(0));
        CHECK(!root.reclaimable_size_known);

        FsNode unknown = storageFile("unknown-allocation", shared, 0, false, 1, true);
        diskmap::aggregateStorage(unknown);
        CHECK_EQ(unknown.allocated_size, static_cast<std::uint64_t>(0));
        CHECK(!unknown.allocated_size_known);
        CHECK_EQ(unknown.reclaimable_size, static_cast<std::uint64_t>(0));
        CHECK(!unknown.reclaimable_size_known);
    }

    // A valid identity with unknown physical fields propagates those unknown
    // bits through the identity merge rather than being treated as zero.
    {
        const FileIdentity known = FileIdentity{21, 211, true};
        FsNode root = makeDirNode("unknown-identity-facts", {
            storageFile("known", known, 4096, true, 1, true),
            storageFile("unknown", FileIdentity{21, 212, true}, 0, false, 0, false),
        });
        diskmap::aggregateStorage(root);
        CHECK_EQ(root.allocated_size, static_cast<std::uint64_t>(4096));
        CHECK(!root.allocated_size_known);
        CHECK_EQ(root.reclaimable_size, static_cast<std::uint64_t>(4096));
        CHECK(!root.reclaimable_size_known);
    }

    // Inconsistent link counts (more observed names than nlink reports) are
    // also uncertain, even though the allocation observation agrees.
    {
        const FileIdentity shared = FileIdentity{21, 213, true};
        FsNode root = makeDirNode("over-referenced", {
            storageFile("first", shared, 512, true, 2, true),
            storageFile("second", shared, 512, true, 2, true),
            storageFile("third", shared, 512, true, 2, true),
        });
        diskmap::aggregateStorage(root);
        CHECK_EQ(root.allocated_size, static_cast<std::uint64_t>(512));
        CHECK(root.allocated_size_known);
        CHECK_EQ(root.reclaimable_size, static_cast<std::uint64_t>(0));
        CHECK(!root.reclaimable_size_known);
    }

    // --- an incomplete leaf or subtree invalidates aggregate certainty even
    // when its numeric fields happen to look usable ---
    {
        FsNode root = makeDirNode("incomplete", {
            storageFile("partial", FileIdentity{}, 1024, true, 1, true, false),
            storageFile("known", FileIdentity{}, 512, true, 1, true),
        });
        diskmap::aggregateStorage(root);
        CHECK_EQ(root.allocated_size, static_cast<std::uint64_t>(1536));
        CHECK(!root.allocated_size_known);
        CHECK_EQ(root.reclaimable_size, static_cast<std::uint64_t>(1536));
        CHECK(!root.reclaimable_size_known);
    }

    // --- distinct maximum-sized objects exercise checked aggregate overflow;
    // saturation is retained while the known bit is cleared ---
    {
        constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
        FsNode root = makeDirNode("overflow", {
            storageFile("first", FileIdentity{22, 220, true}, maximum, true, 1, true),
            storageFile("second", FileIdentity{22, 221, true}, maximum, true, 1, true),
        });
        diskmap::aggregateStorage(root);
        CHECK_EQ(root.allocated_size, maximum);
        CHECK(!root.allocated_size_known);
        CHECK_EQ(root.reclaimable_size, maximum);
        CHECK(!root.reclaimable_size_known);
    }

    // Unidentified allocations are merged independently of identity-aware
    // entries, so overflow there must be conservative as well.
    {
        constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
        FsNode root = makeDirNode("unidentified-overflow", {
            storageFile("first", FileIdentity{}, maximum, true, 1, true),
            storageFile("second", FileIdentity{}, maximum, true, 1, true),
        });
        diskmap::aggregateStorage(root);
        CHECK_EQ(root.allocated_size, maximum);
        CHECK(!root.allocated_size_known);
        CHECK_EQ(root.reclaimable_size, maximum);
        CHECK(!root.reclaimable_size_known);
    }

    // --- kMaxTreeDepth guard: a pathologically deep tree neither crashes
    // nor gets fully walked; every helper's recursion silently stops once
    // depth reaches kMaxTreeDepth, so anything past the cap (including the
    // leaf file at the very bottom, here) is not counted. This is the
    // documented anti-stack-overflow safety cap, not a bug.
    {
        constexpr int kExtra = 50;
        constexpr int kDeep = diskmap::kMaxTreeDepth + kExtra;
        FsNode deepRoot = makeChain(kDeep, makeFileNode("bottom", 7));

        const std::uint64_t total = diskmap::aggregateSizes(deepRoot);
        CHECK_EQ(total, static_cast<std::uint64_t>(0)); // leaf lies past the cap
        CHECK_EQ(deepRoot.size, static_cast<std::uint64_t>(0));

        diskmap::aggregateStorage(deepRoot);
        CHECK(!deepRoot.allocated_size_known);
        CHECK(!deepRoot.reclaimable_size_known);

        diskmap::sortBySizeDesc(deepRoot); // must complete without crashing

        // exactly kMaxTreeDepth+1 nodes get counted (depths 0..kMaxTreeDepth
        // inclusive); everything deeper is never visited.
        CHECK_EQ(diskmap::countNodes(deepRoot), static_cast<std::size_t>(diskmap::kMaxTreeDepth + 1));

        // the leaf file lies past the cap, so it's never collected either.
        CHECK_EQ(diskmap::topFiles(deepRoot, 5).size(), static_cast<std::size_t>(0));
    }

    return testSummary();
}
