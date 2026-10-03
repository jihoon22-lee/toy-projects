#pragma once

#include <QWidget>

#include <memory>
#include <vector>
#include <map>
#include <optional>

#include "diskmap/gui/node_key_metatype.hpp"
#include "diskmap/scanner.hpp"
#include "diskmap/treemap.hpp"

// Renders one level of a squarified treemap and lets the user drill into it.
//
// Projections retain one shared immutable scan document: the tree can be large
// and must not be copied, while every rendered node pointer must remain valid
// across queued paint and input events.
class TreemapWidget : public QWidget {
    Q_OBJECT

public:
    explicit TreemapWidget(QWidget* parent = nullptr);

    void setProjection(std::shared_ptr<const diskmap::ScanResult> document,
                       const diskmap::NodeKey& root,
                       const diskmap::ViewFilter& filter,
                       const diskmap::SortSpec& sort);
    void clear();
    enum class ColorMode { Name, FileType, State, Change };
    void setColorMode(ColorMode mode);
    void setSelectedKey(std::optional<diskmap::NodeKey> key);
    std::optional<diskmap::NodeKey> selectedKey() const { return selected_; }
    void setChangeKinds(std::map<std::string, int> changes);
    const diskmap::FsNode* currentNode() const;
    bool projectionComplete() const;

signals:
    void nodeSelected(diskmap::NodeKey key);
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
    bool projectionComplete_ = true;
    std::vector<diskmap::Tile> tiles_;
    const diskmap::FsNode* hovered_ = nullptr;
    std::optional<diskmap::NodeKey> selected_;
    ColorMode colorMode_ = ColorMode::FileType;
    std::map<std::string, int> changes_;

    void rebuildTiles();
    const diskmap::Tile* tileAt(const QPointF& point) const;
};
