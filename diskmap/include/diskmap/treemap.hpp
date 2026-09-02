#pragma once

#include <vector>

#include "diskmap/fs_node.hpp"
#include "diskmap/view.hpp"

namespace diskmap {

struct Rect {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
};

struct Tile {
    const FsNode* node = nullptr;
    Rect rect;
    int depth = 0;
    // The projected metric and issue are carried with each tile so a GUI can
    // distinguish an exact area from a conservative/incomplete observation.
    MetricValue metric;
    NodeIssue issue = NodeIssue::None;
};

// Builds a squarified treemap (Bruls/Huizing/van Wijk) of root's descendants.
// Tiles of a node's children exactly partition that node's rect (no gaps,
// no overlap, area-preserving). depth 0 tiles are root's direct children;
// descendants stop once depth would exceed maxDepth (negative = unlimited).
std::vector<Tile> squarify(const FsNode& root, const Rect& bounds, int maxDepth);

// Applies the same filter and sort contract as the table/list projection at
// every rendered level. Unknown zero-byte values receive a deterministic
// minimum layout weight so they remain visible; Tile::metric keeps that
// uncertainty explicit and prevents the area from being presented as exact.
std::vector<Tile> squarify(const FsNode& root,
                           const Rect& bounds,
                           int maxDepth,
                           const ViewFilter& filter,
                           const SortSpec& sort);

} // namespace diskmap
