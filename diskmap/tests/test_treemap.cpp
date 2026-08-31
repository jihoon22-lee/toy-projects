// Tests for diskmap::squarify (src/core/treemap.hpp / treemap.cpp).
//
// squarify() returns tiles at MULTIPLE depths (a parent's tile alongside its
// children's tiles), so "sum of every returned tile's area == bounds area"
// is the wrong invariant and would fail. Instead we check, per parent:
//   - sibling tile areas sum to their parent's own rect area,
//   - sibling tiles don't overlap and sit inside the parent's rect,
//   - each tile's area is exactly proportional to its node's size among
//     its siblings (a defining property of the squarified algorithm,
//     independent of how tiles get grouped into rows).

#include "assert.hpp"
#include "diskmap/fs_node.hpp"
#include "diskmap/treemap.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using diskmap::FsNode;
using diskmap::Rect;
using diskmap::Tile;

namespace {

constexpr double kEps = 1e-6;

FsNode makeFile(std::string name, std::uint64_t size) {
    FsNode node;
    node.name = std::move(name);
    node.is_dir = false;
    node.size = size;
    return node;
}

FsNode makeDir(std::string name, std::vector<FsNode> children) {
    FsNode node;
    node.name = std::move(name);
    node.is_dir = true;
    node.children = std::move(children);
    return node;
}

double area(const Rect& r) {
    return r.w * r.h;
}

bool rectsOverlap(const Rect& a, const Rect& b, double eps) {
    const bool disjointX = (a.x + a.w <= b.x + eps) || (b.x + b.w <= a.x + eps);
    const bool disjointY = (a.y + a.h <= b.y + eps) || (b.y + b.h <= a.y + eps);
    return !(disjointX || disjointY);
}

bool rectInside(const Rect& inner, const Rect& outer, double eps) {
    return inner.x >= outer.x - eps && inner.y >= outer.y - eps &&
           inner.x + inner.w <= outer.x + outer.w + eps &&
           inner.y + inner.h <= outer.y + outer.h + eps;
}

const Tile* findTileFor(const std::vector<Tile>& tiles, const FsNode* node) {
    for (const Tile& t : tiles) {
        if (t.node == node) {
            return &t;
        }
    }
    return nullptr;
}

// Verifies that the tiles for `parent`'s direct children (found in `tiles`)
// exactly partition `parentRect`: areas sum to it, none overlap, all fit
// inside it. Silently skips children whose tile is absent (e.g. pruned by
// maxDepth) rather than failing, so callers can reuse this after a
// depth-limited squarify() call.
void checkPartition(const std::vector<Tile>& tiles, const FsNode& parent, const Rect& parentRect) {
    std::vector<Rect> rects;
    double sumArea = 0.0;
    for (const FsNode& child : parent.children) {
        const Tile* t = findTileFor(tiles, &child);
        if (!t) {
            continue;
        }
        CHECK(rectInside(t->rect, parentRect, kEps));
        sumArea += area(t->rect);
        rects.push_back(t->rect);
    }
    if (!rects.empty()) {
        CHECK_NEAR(sumArea, area(parentRect), kEps);
    }
    for (std::size_t i = 0; i < rects.size(); ++i) {
        for (std::size_t j = i + 1; j < rects.size(); ++j) {
            CHECK(!rectsOverlap(rects[i], rects[j], kEps));
        }
    }
}

// Verifies each present child tile's area is exactly proportional to its
// node's size relative to the sum of all its siblings' sizes (skipped when
// every sibling is zero-size; that branch is checked separately).
void checkAreaProportional(const std::vector<Tile>& tiles, const FsNode& parent, const Rect& parentRect) {
    std::uint64_t total = 0;
    for (const FsNode& child : parent.children) {
        total += child.size;
    }
    if (total == 0) {
        return;
    }
    const double parentArea = area(parentRect);
    for (const FsNode& child : parent.children) {
        const Tile* t = findTileFor(tiles, &child);
        if (!t) {
            continue;
        }
        const double expected = parentArea * (static_cast<double>(child.size) / static_cast<double>(total));
        CHECK_NEAR(area(t->rect), expected, kEps);
    }
}

} // namespace

int main() {
    // --- empty children: no tiles at all ---
    {
        const FsNode root = makeDir("root", {});
        const std::vector<Tile> tiles = diskmap::squarify(root, Rect{0, 0, 100, 100}, -1);
        CHECK_EQ(tiles.size(), static_cast<std::size_t>(0));
    }

    // --- single child: gets the entire bounds rect ---
    {
        const FsNode root = makeDir("root", {makeFile("only", 42)});
        const Rect bounds{0, 0, 40, 25};
        const std::vector<Tile> tiles = diskmap::squarify(root, bounds, -1);
        CHECK_EQ(tiles.size(), static_cast<std::size_t>(1));
        CHECK_EQ(tiles[0].depth, 0);
        CHECK_NEAR(tiles[0].rect.x, bounds.x, kEps);
        CHECK_NEAR(tiles[0].rect.y, bounds.y, kEps);
        CHECK_NEAR(tiles[0].rect.w, bounds.w, kEps);
        CHECK_NEAR(tiles[0].rect.h, bounds.h, kEps);
    }

    // --- all-zero-size children: area split evenly among them ---
    {
        const FsNode root = makeDir("root", {makeFile("z1", 0), makeFile("z2", 0), makeFile("z3", 0)});
        const Rect bounds{0, 0, 30, 10}; // area 300
        const std::vector<Tile> tiles = diskmap::squarify(root, bounds, -1);
        CHECK_EQ(tiles.size(), static_cast<std::size_t>(3));
        checkPartition(tiles, root, bounds);
        for (const FsNode& child : root.children) {
            const Tile* t = findTileFor(tiles, &child);
            CHECK(t != nullptr);
            if (t) {
                CHECK_NEAR(area(t->rect), 100.0, kEps); // 300 / 3
            }
        }
    }

    // --- zero-area bounds: must not crash, degenerates to zero-area tiles ---
    {
        const FsNode root = makeDir("root", {makeFile("a", 10), makeFile("b", 20), makeFile("c", 30)});
        for (const Rect bounds : {Rect{0, 0, 0, 5}, Rect{0, 0, 5, 0}, Rect{0, 0, 0, 0}}) {
            const std::vector<Tile> tiles = diskmap::squarify(root, bounds, -1);
            CHECK_EQ(tiles.size(), static_cast<std::size_t>(3));
            for (const Tile& t : tiles) {
                CHECK(std::isfinite(t.rect.x));
                CHECK(std::isfinite(t.rect.y));
                CHECK(std::isfinite(t.rect.w));
                CHECK(std::isfinite(t.rect.h));
                CHECK(t.rect.w >= 0.0);
                CHECK(t.rect.h >= 0.0);
                CHECK_NEAR(area(t.rect), 0.0, kEps);
                CHECK_NEAR(t.rect.x, bounds.x, kEps);
                CHECK_NEAR(t.rect.y, bounds.y, kEps);
            }
        }
    }

    // --- uint64 weights: summing children must not wrap before converting to
    // layout units.  The max-sized child should therefore own essentially all
    // of the bounds, while the one-byte child remains a degenerate sliver.
    {
        constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
        const FsNode root = makeDir("root", {makeFile("maximum", maximum), makeFile("one", 1)});
        const Rect bounds{0, 0, 100, 10};
        const std::vector<Tile> tiles = diskmap::squarify(root, bounds, -1);
        CHECK_EQ(tiles.size(), static_cast<std::size_t>(2));
        const Tile* maximumTile = findTileFor(tiles, &root.children[0]);
        const Tile* oneTile = findTileFor(tiles, &root.children[1]);
        CHECK(maximumTile != nullptr);
        CHECK(oneTile != nullptr);
        if (maximumTile && oneTile) {
            CHECK_NEAR(area(maximumTile->rect), area(bounds), kEps);
            CHECK(area(oneTile->rect) >= 0.0);
            CHECK(area(oneTile->rect) <= kEps);
            checkPartition(tiles, root, bounds);
        }
    }

    // --- extreme aspect ratios: areas still exactly proportional ---
    {
        const FsNode root = makeDir("root", {makeFile("a", 10), makeFile("b", 20), makeFile("c", 30)});
        for (const Rect bounds : {Rect{0, 0, 1000, 1}, Rect{0, 0, 1, 1000}}) {
            const std::vector<Tile> tiles = diskmap::squarify(root, bounds, -1);
            CHECK_EQ(tiles.size(), static_cast<std::size_t>(3));
            checkPartition(tiles, root, bounds);
            checkAreaProportional(tiles, root, bounds);
        }
    }

    // --- two-level tree: partition + proportionality at every level ---
    {
        FsNode root = makeDir("root", {
            makeDir("dirBig", {makeFile("big1", 400), makeFile("big2", 300)}),
            makeDir("dirSmall", {makeFile("small1", 100), makeFile("small2", 200)}),
            makeFile("fileRoot", 200),
        });
        diskmap::aggregateSizes(root); // dirBig=700, dirSmall=300, root=1200

        const Rect bounds{0, 0, 120, 80}; // deliberately non-square, area 9600
        const std::vector<Tile> tiles = diskmap::squarify(root, bounds, -1);

        // depth-0 tiles are root's direct children and partition bounds.
        checkPartition(tiles, root, bounds);
        checkAreaProportional(tiles, root, bounds);

        const FsNode& dirBig = root.children[0];
        const FsNode& dirSmall = root.children[1];
        const Tile* dirBigTile = findTileFor(tiles, &dirBig);
        const Tile* dirSmallTile = findTileFor(tiles, &dirSmall);
        CHECK(dirBigTile != nullptr);
        CHECK(dirSmallTile != nullptr);
        if (dirBigTile) {
            CHECK_EQ(dirBigTile->depth, 0);
            checkPartition(tiles, dirBig, dirBigTile->rect);
            checkAreaProportional(tiles, dirBig, dirBigTile->rect);
        }
        if (dirSmallTile) {
            CHECK_EQ(dirSmallTile->depth, 0);
            checkPartition(tiles, dirSmall, dirSmallTile->rect);
            checkAreaProportional(tiles, dirSmall, dirSmallTile->rect);
        }

        // every grandchild tile should be at depth 1.
        for (const FsNode& gc : dirBig.children) {
            const Tile* t = findTileFor(tiles, &gc);
            CHECK(t != nullptr);
            if (t) {
                CHECK_EQ(t->depth, 1);
            }
        }
    }

    // --- maxDepth = 0: only root's direct children are laid out ---
    {
        FsNode root = makeDir("root", {
            makeDir("dirBig", {makeFile("big1", 400), makeFile("big2", 300)}),
            makeDir("dirSmall", {makeFile("small1", 100), makeFile("small2", 200)}),
            makeFile("fileRoot", 200),
        });
        diskmap::aggregateSizes(root);

        const Rect bounds{0, 0, 120, 80};
        const std::vector<Tile> tiles = diskmap::squarify(root, bounds, 0);
        CHECK_EQ(tiles.size(), static_cast<std::size_t>(3)); // dirBig, dirSmall, fileRoot only
        for (const Tile& t : tiles) {
            CHECK_EQ(t.depth, 0);
        }
        checkPartition(tiles, root, bounds);
        checkAreaProportional(tiles, root, bounds);
        CHECK(findTileFor(tiles, &root.children[0].children[0]) == nullptr);
    }

    // --- maxDepth interplay on a 3-level tree: depth1 present, depth2 absent ---
    {
        FsNode root = makeDir("root", {
            makeDir("mid", {
                makeDir("leafDir", {makeFile("deepfile", 10)}),
                makeFile("midfile", 5),
            }),
        });
        diskmap::aggregateSizes(root);
        const Rect bounds{0, 0, 50, 50};
        const FsNode& mid = root.children[0];
        const FsNode& leafDir = mid.children[0];

        {
            const std::vector<Tile> tiles = diskmap::squarify(root, bounds, 1);
            CHECK(findTileFor(tiles, &mid) != nullptr);
            CHECK(findTileFor(tiles, &leafDir) != nullptr);       // depth 1: present
            CHECK(findTileFor(tiles, &mid.children[1]) != nullptr); // midfile, depth 1
            CHECK(findTileFor(tiles, &leafDir.children[0]) == nullptr); // deepfile, depth 2: pruned
        }
        {
            const std::vector<Tile> tiles = diskmap::squarify(root, bounds, -1);
            const Tile* deep = findTileFor(tiles, &leafDir.children[0]);
            CHECK(deep != nullptr);
            if (deep) {
                CHECK_EQ(deep->depth, 2);
            }
            checkPartition(tiles, root, bounds);
        }
    }

    return testSummary();
}
