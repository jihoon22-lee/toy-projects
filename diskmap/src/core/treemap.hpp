#pragma once

#include <vector>

#include "fs_node.hpp"

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
};

// Builds a squarified treemap (Bruls/Huizing/van Wijk) of root's descendants.
// Tiles of a node's children exactly partition that node's rect (no gaps,
// no overlap, area-preserving). depth 0 tiles are root's direct children;
// recursion stops once depth would exceed maxDepth (negative = unlimited).
std::vector<Tile> squarify(const FsNode& root, const Rect& bounds, int maxDepth);

} // namespace diskmap
