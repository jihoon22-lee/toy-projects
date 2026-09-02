#include "diskmap/gui/treemap_widget.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QResizeEvent>
#include <QtGlobal>

#include "explorer_text.hpp"

namespace {

// Only the top level is drawn as filled tiles; one level of children is drawn
// as an outline so the structure reads without the display turning to noise.
constexpr int kRenderDepth = 1;
constexpr double kMinLabelWidth = 60.0;
constexpr double kMinLabelHeight = 18.0;

QRectF toRect(const diskmap::Rect& rect) {
    return QRectF(rect.x, rect.y, rect.w, rect.h);
}

// QMouseEvent::position() arrived in Qt 6; Qt 5 spells the same local widget
// coordinate localPos(). Keeping the compatibility at this boundary lets the
// rest of the widget and its tests use one coordinate contract.
QPointF eventPos(const QMouseEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
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

bool uncertain(const diskmap::Tile& tile) {
    return !tile.metric.known || tile.issue != diskmap::NodeIssue::None;
}

} // namespace

TreemapWidget::TreemapWidget(QWidget* parent) : QWidget(parent) {
    qRegisterMetaType<diskmap::NodeKey>("diskmap::NodeKey");
    setAccessibleName(tr("Disk usage treemap"));
    setAccessibleDescription(tr(
        "Visual overview of the current projection. Use the adjacent filesystem "
        "entries table for keyboard navigation and detailed evidence."));
    setMouseTracking(true);
    setMinimumSize(320, 240);
}

void TreemapWidget::setProjection(
    std::shared_ptr<const diskmap::ScanResult> document,
    const diskmap::NodeKey& root,
    const diskmap::ViewFilter& filter,
    const diskmap::SortSpec& sort) {
    document_ = std::move(document);
    filter_ = filter;
    sort_ = sort;
    root_ = document_ == nullptr
                ? nullptr
                : diskmap::findNodeByKey(document_->root, root);
    hovered_ = nullptr;
    rebuildTiles();
    update();
    emit hoverCleared();
    emit projectionStatusChanged(projectionComplete_);
}

void TreemapWidget::clear() {
    document_.reset();
    root_ = nullptr;
    projectionComplete_ = true;
    hovered_ = nullptr;
    rebuildTiles();
    update();
    emit hoverCleared();
    emit projectionStatusChanged(projectionComplete_);
}

const diskmap::FsNode* TreemapWidget::currentNode() const { return root_; }

bool TreemapWidget::projectionComplete() const { return projectionComplete_; }

void TreemapWidget::rebuildTiles() {
    tiles_.clear();
    if (root_ == nullptr) {
        projectionComplete_ = document_ == nullptr;
        return;
    }
    const diskmap::Rect bounds{0.0, 0.0, static_cast<double>(width()),
                               static_cast<double>(height())};
    tiles_ = diskmap::squarify(*root_, bounds, kRenderDepth, filter_, sort_);
    projectionComplete_ =
        diskmap::metricValue(*root_, sort_.metric, filter_.scanner_totals_filtered).known;
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
        const QString emptyText = root_ == nullptr ? tr("Choose a folder to scan")
                                                   : tr("No items match the active filters");
        painter.drawText(rect(), Qt::AlignCenter, emptyText);
        return;
    }

    for (const diskmap::Tile& tile : tiles_) {
        if (tile.node == nullptr || tile.depth != 0) {
            continue;
        }
        const QRectF box = toRect(tile.rect);
        painter.fillRect(box, tileColor(*tile.node, tile.node == hovered_));
        if (uncertain(tile)) {
            painter.fillRect(box, QBrush(QColor(255, 255, 255, 70), Qt::BDiagPattern));
        }
        QPen border(uncertain(tile) ? QColor(255, 193, 92) : QColor(20, 20, 24));
        border.setStyle(uncertain(tile) ? Qt::DashLine : Qt::SolidLine);
        painter.setPen(border);
        painter.drawRect(box);
        if (box.width() < kMinLabelWidth || box.height() < kMinLabelHeight) {
            continue;
        }
        painter.setPen(QColor(20, 20, 24));
        painter.drawText(box.adjusted(4, 2, -4, -2), Qt::AlignLeft | Qt::AlignTop,
                         QString::fromStdString(tile.node->name));
        painter.drawText(box.adjusted(4, 2, -4, -2), Qt::AlignRight | Qt::AlignBottom,
                         diskmap_gui_text::metricValueText(tile.metric));
    }

    painter.setPen(QColor(255, 255, 255, 60));
    for (const diskmap::Tile& tile : tiles_) {
        if (tile.depth == 1) {
            painter.drawRect(toRect(tile.rect));
        }
    }
}

void TreemapWidget::mouseMoveEvent(QMouseEvent* event) {
    const diskmap::Tile* tile = tileAt(eventPos(event));
    const diskmap::FsNode* node = tile == nullptr ? nullptr : tile->node;
    if (node == hovered_) {
        return;
    }
    hovered_ = node;
    if (node == nullptr) {
        emit hoverCleared();
    } else {
        emit nodeHovered(diskmap::nodeKey(*node));
    }
    update();
}

void TreemapWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const diskmap::Tile* tile = tileAt(eventPos(event));
    if (tile == nullptr || tile->node == nullptr || !tile->node->is_dir) {
        return;
    }
    emit nodeActivated(diskmap::nodeKey(*tile->node));
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
    emit hoverCleared();
    update();
}
