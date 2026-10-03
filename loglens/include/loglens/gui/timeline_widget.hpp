#pragma once

#include <QWidget>

#include <cstdint>
#include <optional>
#include <vector>

#include "loglens/log_stats.hpp"

// Stacked per-level histogram over time — the "where did it go wrong" strip.
class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    void setBuckets(std::vector<loglens::Bucket> buckets,
                    std::uint64_t bucketMs = 60000);
    void clearSelection();
    void setSelection(std::uint64_t beginMs, std::uint64_t endMs);

signals:
    void rangeSelected(qulonglong beginMs, qulonglong endMs);
    void rangeCleared();

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::vector<loglens::Bucket> buckets_;
    std::size_t peak_ = 0;
    std::uint64_t bucket_ms_ = 60000;
    std::optional<std::size_t> drag_anchor_;
    std::optional<std::size_t> drag_current_;
    bool dragging_ = false;
    std::optional<std::pair<std::uint64_t, std::uint64_t>> selected_range_;

    std::optional<std::size_t> bucketAt(int x) const;
    void publishSelection();
};
