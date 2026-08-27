#include "treemap_widget.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>

#include "diskmap/format.hpp"

namespace {

// Only the top level is drawn as filled tiles; one level of children is drawn
// as an outline so the structure reads without the display turning to noise.
constexpr int kRenderDepth = 1;
constexpr double kMinLabelWidth = 60.0;
constexpr double kMinLabelHeight = 18.0;

QRectF toRect(const diskmap::Rect& rect) {
    return QRectF(rect.x, rect.y, rect.w, rect.h);
}

// Distinct but stable per name, so a tile keeps its colour across rescans.
QColor tileColor(const diskmap::FsNode& node, bool hovered) {
    std::size_t hash = 0;
    for (char c : node.name) {
        hash = hash * 131 + static_cast<unsigned char>(c);
    }
    const int hue = static_cast<int>(hash % 360);
    const int saturation = node.is_dir ? 90 : 140;
    const int value = hovered ? 235 : 190;
    return QColor::fromHsv(hue, saturation, value);
}

} // namespace

TreemapWidget::TreemapWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(320, 240);
}

void TreemapWidget::setRoot(const diskmap::FsNode* root) {
    root_ = root;
    hovered_ = nullptr;
    rebuildTiles();
    update();
}

const diskmap::FsNode* TreemapWidget::currentNode() const { return root_; }

void TreemapWidget::rebuildTiles() {
    tiles_.clear();
    if (root_ == nullptr) {
        return;
    }
    const diskmap::Rect bounds{0.0, 0.0, static_cast<double>(width()),
                               static_cast<double>(height())};
    tiles_ = diskmap::squarify(*root_, bounds, kRenderDepth);
}

const diskmap::Tile* TreemapWidget::tileAt(const QPointF& point) const {
    // Later tiles are deeper, so scanning backwards finds the innermost hit.
    for (auto it = tiles_.rbegin(); it != tiles_.rend(); ++it) {
        if (toRect(it->rect).contains(point)) {
            return &(*it);
        }
    }
    return nullptr;
}

void TreemapWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(event->rect(), QColor(28, 30, 34));

    if (tiles_.empty()) {
        painter.setPen(QColor(150, 150, 150));
        painter.drawText(rect(), Qt::AlignCenter, tr("Choose a folder to scan"));
        return;
    }

    for (const diskmap::Tile& tile : tiles_) {
        if (tile.node == nullptr || tile.depth != 0) {
            continue;
        }
        const QRectF box = toRect(tile.rect);
        painter.fillRect(box, tileColor(*tile.node, tile.node == hovered_));
        painter.setPen(QColor(20, 20, 24));
        painter.drawRect(box);
        if (box.width() < kMinLabelWidth || box.height() < kMinLabelHeight) {
            continue;
        }
        painter.setPen(QColor(20, 20, 24));
        painter.drawText(box.adjusted(4, 2, -4, -2), Qt::AlignLeft | Qt::AlignTop,
                         QString::fromStdString(tile.node->name));
        painter.drawText(box.adjusted(4, 2, -4, -2), Qt::AlignRight | Qt::AlignBottom,
                         QString::fromStdString(diskmap::humanBytes(tile.node->size)));
    }

    painter.setPen(QColor(255, 255, 255, 60));
    for (const diskmap::Tile& tile : tiles_) {
        if (tile.depth == 1) {
            painter.drawRect(toRect(tile.rect));
        }
    }
}

void TreemapWidget::mouseMoveEvent(QMouseEvent* event) {
    const diskmap::Tile* tile = tileAt(event->position());
    const diskmap::FsNode* node = tile == nullptr ? nullptr : tile->node;
    if (node == hovered_) {
        return;
    }
    hovered_ = node;
    emit hoveredNodeChanged(node);
    update();
}

void TreemapWidget::mousePressEvent(QMouseEvent* event) {
    const diskmap::Tile* tile = tileAt(event->position());
    if (tile == nullptr || tile->node == nullptr || !tile->node->is_dir) {
        return;
    }
    emit nodeActivated(tile->node);
}

void TreemapWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    rebuildTiles();
}

void TreemapWidget::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    if (hovered_ == nullptr) {
        return;
    }
    hovered_ = nullptr;
    emit hoveredNodeChanged(nullptr);
    update();
}
