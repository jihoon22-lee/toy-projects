#pragma once

#include <QWidget>

#include <memory>
#include <vector>

#include "diskmap/gui/node_key_metatype.hpp"
#include "diskmap/scanner.hpp"
#include "diskmap/treemap.hpp"

// Renders one level of a squarified treemap and lets the user drill into it.
//
// Production projections retain one shared immutable scan document: the tree
// can be large and must not be copied, while every rendered node pointer must
// remain valid across queued paint and input events. setRoot() is a deliberately
// non-owning compatibility boundary for small, synchronous widget fixtures.
class TreemapWidget : public QWidget {
    Q_OBJECT

public:
    explicit TreemapWidget(QWidget* parent = nullptr);

    void setProjection(std::shared_ptr<const diskmap::ScanResult> document,
                       const diskmap::NodeKey& root,
                       const diskmap::ViewFilter& filter,
                       const diskmap::SortSpec& sort);
    // Compatibility boundary for small widget fixtures. Production callers
    // use setProjection(), which owns the immutable scan document.
    void setRoot(const diskmap::FsNode* root);
    const diskmap::FsNode* currentNode() const;
    bool projectionComplete() const;

signals:
    void nodeActivated(diskmap::NodeKey key);
    void nodeHovered(diskmap::NodeKey key);
    void hoverCleared();
    void projectionStatusChanged(bool complete);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    std::shared_ptr<const diskmap::ScanResult> document_;
    const diskmap::FsNode* root_ = nullptr;
    diskmap::ViewFilter filter_;
    diskmap::SortSpec sort_;
    bool projected_ = false;
    bool projectionComplete_ = true;
    std::vector<diskmap::Tile> tiles_;
    const diskmap::FsNode* hovered_ = nullptr;

    void rebuildTiles();
    const diskmap::Tile* tileAt(const QPointF& point) const;
};
