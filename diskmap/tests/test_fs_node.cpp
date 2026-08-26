// Tests for diskmap::FsNode helpers (src/core/fs_node.hpp / fs_node.cpp):
// aggregateSizes, sortBySizeDesc, findChild, countNodes, topFiles.

#include "assert.hpp"
#include "fake_fs.hpp"
#include "../src/core/fs_node.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using diskmap::FsNode;
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

} // namespace

int main() {
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

        diskmap::sortBySizeDesc(deepRoot); // must complete without crashing

        // exactly kMaxTreeDepth+1 nodes get counted (depths 0..kMaxTreeDepth
        // inclusive); everything deeper is never visited.
        CHECK_EQ(diskmap::countNodes(deepRoot), static_cast<std::size_t>(diskmap::kMaxTreeDepth + 1));

        // the leaf file lies past the cap, so it's never collected either.
        CHECK_EQ(diskmap::topFiles(deepRoot, 5).size(), static_cast<std::size_t>(0));
    }

    return testSummary();
}
