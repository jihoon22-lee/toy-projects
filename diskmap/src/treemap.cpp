#include "diskmap/treemap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace diskmap {

namespace {

struct RowLayout {
    std::vector<Rect> rects;
    Rect remaining;
};

double sumAreas(const std::vector<double>& areas) {
    double total = 0.0;
    for (double area : areas) {
        total += area;
    }
    return total;
}

// Bruls/Huizing/van Wijk worst-aspect-ratio metric for a candidate row laid
// out along a strip of the given (fixed) side length.
double worstAspectRatio(const std::vector<double>& row, double side) {
    if (row.empty() || side <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    const double total = sumAreas(row);
    if (total <= 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    const double largest = *std::max_element(row.begin(), row.end());
    const double smallest = *std::min_element(row.begin(), row.end());
    const double sideSquared = side * side;
    const double ratioA = (sideSquared * largest) / (total * total);
    const double ratioB = (total * total) / (sideSquared * smallest);
    return std::max(ratioA, ratioB);
}

bool addingImprovesRow(const std::vector<double>& row, double candidate, double side) {
    std::vector<double> extended = row;
    extended.push_back(candidate);
    return worstAspectRatio(extended, side) <= worstAspectRatio(row, side);
}

// Lays a completed row of areas into rect, sliced along rect's shorter
// side, and returns the leftover rect plus one output rect per row entry
// (same order as the input areas).
RowLayout layoutRow(const std::vector<double>& row, const Rect& rect) {
    RowLayout result;
    const double rowArea = sumAreas(row);
    const bool sliceOnLeft = rect.w >= rect.h;

    if (sliceOnLeft) {
        const double bandWidth = rect.h > 0.0 ? rowArea / rect.h : 0.0;
        double y = rect.y;
        for (double area : row) {
            const double height = bandWidth > 0.0 ? area / bandWidth : 0.0;
            result.rects.push_back(Rect{rect.x, y, bandWidth, height});
            y += height;
        }
        result.remaining = Rect{rect.x + bandWidth, rect.y, rect.w - bandWidth, rect.h};
    } else {
        const double bandHeight = rect.w > 0.0 ? rowArea / rect.w : 0.0;
        double x = rect.x;
        for (double area : row) {
            const double width = bandHeight > 0.0 ? area / bandHeight : 0.0;
            result.rects.push_back(Rect{x, rect.y, width, bandHeight});
            x += width;
        }
        result.remaining = Rect{rect.x, rect.y + bandHeight, rect.w, rect.h - bandHeight};
    }
    return result;
}

// Core squarified layout: greedily grows one row at a time, always picking
// up the next area if doing so does not worsen the row's aspect ratio.
std::vector<Rect> squarifyAreas(const std::vector<double>& areas, const Rect& bounds) {
    std::vector<Rect> placed(areas.size());
    Rect remaining = bounds;
    std::size_t idx = 0;

    while (idx < areas.size()) {
        const double side = std::min(remaining.w, remaining.h);
        std::vector<double> row{areas[idx]};
        std::vector<std::size_t> rowIndices{idx};
        std::size_t next = idx + 1;

        while (next < areas.size() && addingImprovesRow(row, areas[next], side)) {
            row.push_back(areas[next]);
            rowIndices.push_back(next);
            ++next;
        }

        const RowLayout layout = layoutRow(row, remaining);
        for (std::size_t k = 0; k < rowIndices.size(); ++k) {
            placed[rowIndices[k]] = layout.rects[k];
        }
        remaining = layout.remaining;
        idx = next;
    }
    return placed;
}

std::vector<const FsNode*> sortedChildPointers(const FsNode& node) {
    std::vector<const FsNode*> children;
    children.reserve(node.children.size());
    for (const FsNode& child : node.children) {
        children.push_back(&child);
    }
    std::sort(children.begin(), children.end(), [](const FsNode* a, const FsNode* b) {
        if (a->size != b->size) {
            return a->size > b->size;
        }
        return a->name < b->name;
    });
    return children;
}

// Converts child sizes into rect-area units. When every child is zero-size
// (nothing to weight by), the bounds area is split evenly instead so the
// partition still holds exactly.
std::vector<double> computeChildAreas(const std::vector<const FsNode*>& children,
                                      double boundsArea) {
    long double totalSize = 0.0L;
    for (const FsNode* child : children) {
        totalSize += static_cast<long double>(child->size);
    }

    std::vector<double> areas(children.size());
    if (totalSize == 0) {
        const double share = children.empty() ? 0.0 : boundsArea / static_cast<double>(children.size());
        std::fill(areas.begin(), areas.end(), share);
        return areas;
    }
    for (std::size_t i = 0; i < children.size(); ++i) {
        const long double share = static_cast<long double>(children[i]->size) / totalSize;
        areas[i] = boundsArea * static_cast<double>(share);
    }
    return areas;
}

struct LayoutFrame {
    std::vector<const FsNode*> children;
    std::vector<Rect> rects;
    std::size_t next_child = 0;
    int depth = 0;
};

LayoutFrame makeLayoutFrame(const FsNode& node, const Rect& rect, int depth) {
    LayoutFrame frame;
    frame.children = sortedChildPointers(node);
    const std::vector<double> areas = computeChildAreas(frame.children, rect.w * rect.h);
    frame.rects = squarifyAreas(areas, rect);
    frame.depth = depth;
    return frame;
}

void layoutNode(const FsNode& node,
                const Rect& rect,
                int maxDepth,
                std::vector<Tile>& tiles) {
    if (node.children.empty()) {
        return;
    }

    std::vector<LayoutFrame> stack;
    stack.push_back(makeLayoutFrame(node, rect, 0));
    while (!stack.empty()) {
        LayoutFrame& frame = stack.back();
        if (frame.next_child >= frame.children.size()) {
            stack.pop_back();
            continue;
        }
        const std::size_t index = frame.next_child++;
        const FsNode* child = frame.children[index];
        const Rect childRect = frame.rects[index];
        tiles.push_back(Tile{child, childRect, frame.depth});
        if (!child->children.empty() && (maxDepth < 0 || frame.depth < maxDepth)) {
            stack.push_back(makeLayoutFrame(*child, childRect, frame.depth + 1));
        }
    }
}

} // namespace

std::vector<Tile> squarify(const FsNode& root, const Rect& bounds, int maxDepth) {
    std::vector<Tile> tiles;
    Rect safeBounds = bounds;
    if (!std::isfinite(safeBounds.w) || safeBounds.w < 0.0) {
        safeBounds.w = 0.0;
    }
    if (!std::isfinite(safeBounds.h) || safeBounds.h < 0.0) {
        safeBounds.h = 0.0;
    }
    layoutNode(root, safeBounds, maxDepth, tiles);
    return tiles;
}

} // namespace diskmap
