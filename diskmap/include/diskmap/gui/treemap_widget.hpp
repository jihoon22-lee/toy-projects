#pragma once

#include <QMetaType>
#include <QWidget>

#include <vector>

#include "diskmap/fs_node.hpp"
#include "diskmap/treemap.hpp"

Q_DECLARE_METATYPE(const diskmap::FsNode*)

// Renders one level of a squarified treemap and lets the user drill into it.
//
// Holds only a pointer to a tree owned by MainWindow: the scan can be large and
// copying it per repaint would be wasteful. setRoot(nullptr) clears the view.
class TreemapWidget : public QWidget {
    Q_OBJECT

public:
    explicit TreemapWidget(QWidget* parent = nullptr);

    void setRoot(const diskmap::FsNode* root);
    const diskmap::FsNode* currentNode() const;

signals:
    void nodeActivated(const diskmap::FsNode* node);
    void hoveredNodeChanged(const diskmap::FsNode* node);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    const diskmap::FsNode* root_ = nullptr;
    std::vector<diskmap::Tile> tiles_;
    const diskmap::FsNode* hovered_ = nullptr;

    void rebuildTiles();
    const diskmap::Tile* tileAt(const QPointF& point) const;
};
