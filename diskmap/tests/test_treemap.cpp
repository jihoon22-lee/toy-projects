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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using diskmap::FsNode;
using diskmap::MetricValue;
using diskmap::NodeIssue;
using diskmap::Rect;
using diskmap::SizeMetric;
using diskmap::SortSpec;
using diskmap::Tile;
using diskmap::ViewFilter;

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

FsNode makeMetricFile(std::string name,
                      std::uint64_t logical,
                      std::uint64_t allocated,
                      std::uint64_t reclaimable) {
    FsNode node = makeFile(std::move(name), logical);
    node.metadata.kind = diskmap::FsKind::RegularFile;
    node.metadata.logical_size = logical;
    node.metadata.allocated_size = allocated;
    node.metadata.allocated_size_known = true;
    node.metadata.hard_link_count = 1;
    node.metadata.hard_link_count_known = true;
    node.metadata.complete = true;
    node.complete = true;
    node.allocated_size = allocated;
    node.allocated_size_known = true;
    node.reclaimable_size = reclaimable;
    node.reclaimable_size_known = true;
    return node;
}

void markKnown(FsNode& node) {
    node.complete = true;
    node.metadata.kind = node.is_dir ? diskmap::FsKind::Directory
                                     : diskmap::FsKind::RegularFile;
    node.metadata.complete = true;
    for (FsNode& child : node.children) {
        markKnown(child);
    }
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

std::vector<std::string> namesAtDepth(const std::vector<Tile>& tiles, int depth) {
    std::vector<std::string> names;
    for (const Tile& tile : tiles) {
        if (tile.depth == depth && tile.node != nullptr) {
            names.push_back(tile.node->name);
        }
    }
    return names;
}

void checkNames(const std::vector<Tile>& tiles,
                int depth,
                std::initializer_list<const char*> expected) {
    const std::vector<std::string> actual = namesAtDepth(tiles, depth);
    CHECK_EQ(actual.size(), expected.size());
    const std::size_t count = std::min(actual.size(), expected.size());
    auto expectedName = expected.begin();
    for (std::size_t index = 0; index < count; ++index, ++expectedName) {
        CHECK_EQ(actual[index], std::string(*expectedName));
    }
}

void checkSameTiles(const std::vector<Tile>& first, const std::vector<Tile>& second) {
    CHECK_EQ(first.size(), second.size());
    const std::size_t count = std::min(first.size(), second.size());
    for (std::size_t index = 0; index < count; ++index) {
        CHECK_EQ(first[index].node, second[index].node);
        CHECK_EQ(first[index].depth, second[index].depth);
        CHECK_EQ(first[index].metric, second[index].metric);
        CHECK_EQ(first[index].issue, second[index].issue);
        CHECK_NEAR(first[index].rect.x, second[index].rect.x, kEps);
        CHECK_NEAR(first[index].rect.y, second[index].rect.y, kEps);
        CHECK_NEAR(first[index].rect.w, second[index].rect.w, kEps);
        CHECK_NEAR(first[index].rect.h, second[index].rect.h, kEps);
    }
}

void checkMetricAreas(const std::vector<Tile>& tiles,
                      const FsNode& root,
                      const Rect& bounds,
                      SizeMetric metric) {
    std::uint64_t total = 0;
    for (const FsNode& child : root.children) {
        total += diskmap::metricValue(child, metric).bytes;
    }
    for (const FsNode& child : root.children) {
        const MetricValue expected = diskmap::metricValue(child, metric);
        const Tile* tile = findTileFor(tiles, &child);
        CHECK(tile != nullptr);
        if (tile) {
            CHECK_EQ(tile->metric, expected);
            CHECK_EQ(tile->issue, NodeIssue::None);
            const double expectedArea =
                area(bounds) * static_cast<double>(expected.bytes)
                / static_cast<double>(total);
            CHECK_NEAR(area(tile->rect), expectedArea, kEps);
        }
    }
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

    // --- projected treemap applies the filter independently at every level --
    {
        FsNode root = makeDir("root", {
            makeDir("keep-one", {
                makeDir("keep-deep", {
                    makeFile("keep-leaf", 7),
                    makeFile("drop-deep", 90),
                }),
                makeFile("keep-mid", 5),
                makeFile("drop-mid", 100),
            }),
            makeDir("drop-root", {makeFile("keep-hidden", 500)}),
            makeFile("keep-file", 3),
            makeFile("drop-file", 1000),
        });
        diskmap::aggregateSizes(root);
        markKnown(root);

        ViewFilter filter;
        filter.search = "keep";
        SortSpec sort;
        sort.metric = SizeMetric::Logical;
        sort.descending = true;
        const Rect bounds{0, 0, 120, 80};
        const std::vector<Tile> tiles =
            diskmap::squarify(root, bounds, -1, filter, sort);

        // Root keeps two children; keep-one keeps two children; and the
        // nested keep-deep directory keeps one child. A matching descendant
        // under drop-root must not be resurrected after its parent is filtered.
        CHECK_EQ(tiles.size(), static_cast<std::size_t>(5));
        checkNames(tiles, 0, {"keep-one", "keep-file"});
        checkNames(tiles, 1, {"keep-deep", "keep-mid"});
        checkNames(tiles, 2, {"keep-leaf"});
        CHECK(findTileFor(tiles, &root.children[1]) == nullptr); // drop-root
        CHECK(findTileFor(tiles, &root.children[1].children[0]) == nullptr);
        CHECK(findTileFor(tiles, &root.children[0].children[2]) == nullptr);
        CHECK(findTileFor(tiles, &root.children[0].children[0].children[1]) == nullptr);

        const Tile* keepOneTile = findTileFor(tiles, &root.children[0]);
        const Tile* keepDeepTile = findTileFor(tiles, &root.children[0].children[0]);
        CHECK(keepOneTile != nullptr);
        CHECK(keepDeepTile != nullptr);
        checkPartition(tiles, root, bounds);
        if (keepOneTile) {
            checkPartition(tiles, root.children[0], keepOneTile->rect);
        }
        if (keepDeepTile) {
            checkPartition(tiles, root.children[0].children[0], keepDeepTile->rect);
        }
    }

    // --- projected metric selection drives both order and area weights -----
    {
        FsNode root = makeDir("metrics", {
            makeMetricFile("alpha", 100, 10, 60),
            makeMetricFile("beta", 50, 80, 5),
            makeMetricFile("gamma", 10, 30, 40),
        });
        const Rect bounds{0, 0, 120, 100};
        ViewFilter filter;

        for (const SizeMetric metric : {SizeMetric::Logical, SizeMetric::Allocated,
                                        SizeMetric::Reclaimable}) {
            SortSpec sort;
            sort.metric = metric;
            sort.descending = true;
            const std::vector<Tile> tiles =
                diskmap::squarify(root, bounds, 0, filter, sort);

            CHECK_EQ(tiles.size(), static_cast<std::size_t>(3));
            if (metric == SizeMetric::Logical) {
                checkNames(tiles, 0, {"alpha", "beta", "gamma"});
            } else if (metric == SizeMetric::Allocated) {
                checkNames(tiles, 0, {"beta", "gamma", "alpha"});
            } else {
                checkNames(tiles, 0, {"alpha", "gamma", "beta"});
            }
            checkMetricAreas(tiles, root, bounds, metric);
        }
    }

    // --- unknown zero metrics remain visible and retain uncertainty --------
    {
        FsNode root = makeDir("unknown metric", {
            makeMetricFile("known", 9, 9, 9),
            makeMetricFile("unknown-b", 0, 0, 0),
            makeMetricFile("unknown-a", 0, 0, 0),
        });
        for (FsNode& child : root.children) {
            if (child.name.rfind("unknown-", 0) == 0) {
                child.complete = false;
                child.error = "permission denied";
                child.metadata.allocated_size_known = false;
                child.allocated_size_known = false;
                child.reclaimable_size_known = false;
            }
        }

        const Rect bounds{0, 0, 110, 10};
        ViewFilter filter;
        SortSpec sort;
        sort.metric = SizeMetric::Allocated;
        sort.descending = true;
        const std::vector<Tile> first =
            diskmap::squarify(root, bounds, 0, filter, sort);
        const std::vector<Tile> second =
            diskmap::squarify(root, bounds, 0, filter, sort);

        CHECK_EQ(first.size(), static_cast<std::size_t>(3));
        CHECK_EQ(second.size(), first.size());
        checkNames(first, 0, {"known", "unknown-a", "unknown-b"});
        checkPartition(first, root, bounds);
        for (const FsNode& child : root.children) {
            const Tile* tile = findTileFor(first, &child);
            CHECK(tile != nullptr);
            if (tile && child.name.rfind("unknown-", 0) == 0) {
                CHECK_EQ(tile->metric, (MetricValue{0, false, false}));
                CHECK_EQ(tile->issue, NodeIssue::Incomplete);
                CHECK(area(tile->rect) > 0.0);
                CHECK_NEAR(area(tile->rect), area(bounds) / 11.0, kEps);
            }
        }
        checkSameTiles(first, second);
    }

    // --- projected ordering is deterministic for ties in either direction -
    {
        FsNode root = makeDir("ordering", {
            makeMetricFile("zeta", 20, 20, 20),
            makeMetricFile("alpha", 20, 20, 20),
            makeMetricFile("middle", 5, 5, 5),
        });
        ViewFilter filter;
        SortSpec descending;
        descending.metric = SizeMetric::Logical;
        descending.descending = true;
        const std::vector<Tile> desc =
            diskmap::squarify(root, Rect{0, 0, 100, 100}, 0, filter, descending);
        checkNames(desc, 0, {"alpha", "zeta", "middle"});

        SortSpec ascending = descending;
        ascending.descending = false;
        const std::vector<Tile> asc =
            diskmap::squarify(root, Rect{0, 0, 100, 100}, 0, filter, ascending);
        checkNames(asc, 0, {"middle", "alpha", "zeta"});
        // Sorting is projection-only; the source child order remains intact.
        CHECK_EQ(root.children[0].name, std::string("zeta"));
        CHECK_EQ(root.children[1].name, std::string("alpha"));
    }

    // --- legacy overload keeps size/name layout and compatibility metadata -
    {
        FsNode root = makeDir("legacy", {
            makeFile("zeta", 10),
            makeFile("alpha", 10),
            makeFile("largest", 30),
        });
        root.children[0].logical_size_known = false;
        root.children[1].complete = false;

        const Rect bounds{0, 0, 90, 40};
        const std::vector<Tile> tiles = diskmap::squarify(root, bounds, 0);
        CHECK_EQ(tiles.size(), static_cast<std::size_t>(3));
        checkNames(tiles, 0, {"largest", "alpha", "zeta"});
        checkPartition(tiles, root, bounds);
        checkAreaProportional(tiles, root, bounds);
        for (const Tile& tile : tiles) {
            CHECK_EQ(tile.metric, (MetricValue{tile.node->size, true, true}));
            CHECK_EQ(tile.issue, NodeIssue::None);
        }
    }

    // --- invalid dimensions are sanitized for the projected overload ------
    {
        const FsNode root = makeDir("invalid bounds", {makeFile("child", 1)});
        const Rect invalid{7, 11, -5, std::numeric_limits<double>::quiet_NaN()};
        ViewFilter filter;
        SortSpec sort;
        const std::vector<Tile> tiles =
            diskmap::squarify(root, invalid, 0, filter, sort);
        CHECK_EQ(tiles.size(), static_cast<std::size_t>(1));
        for (const Tile& tile : tiles) {
            CHECK(std::isfinite(tile.rect.x));
            CHECK(std::isfinite(tile.rect.y));
            CHECK(std::isfinite(tile.rect.w));
            CHECK(std::isfinite(tile.rect.h));
            CHECK(tile.rect.w >= 0.0);
            CHECK(tile.rect.h >= 0.0);
            CHECK_NEAR(tile.rect.x, invalid.x, kEps);
            CHECK_NEAR(tile.rect.y, invalid.y, kEps);
            CHECK_NEAR(area(tile.rect), 0.0, kEps);
        }
    }

    return testSummary();
}
