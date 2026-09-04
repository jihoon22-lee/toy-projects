#include "loglens/gui/timeline_widget.hpp"

#include <QColor>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>

#include <algorithm>

namespace {

// Same palette as the table, ordered to match Level's enumerators.
const char* const kBarColours[] = {"#8a8f98", "#7aa2c8", "#cbd5df",
                                   "#e0b341", "#e0645a", "#ff4d4d", "#4a4f58"};

std::size_t bucketTotal(const loglens::Bucket& bucket) {
    std::size_t total = 0;
    for (std::size_t count : bucket.level_counts) {
        total += count;
    }
    return total;
}

} // namespace

TimelineWidget::TimelineWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(72);
    setMaximumHeight(120);
}

void TimelineWidget::setBuckets(std::vector<loglens::Bucket> buckets,
                                std::uint64_t bucketMs) {
    const bool hadSelection = drag_anchor_.has_value() || drag_current_.has_value();
    drag_anchor_.reset();
    drag_current_.reset();
    buckets_ = std::move(buckets);
    bucket_ms_ = std::max<std::uint64_t>(1, bucketMs);
    peak_ = 0;
    for (const loglens::Bucket& bucket : buckets_) {
        peak_ = std::max(peak_, bucketTotal(bucket));
    }
    update();
    if (hadSelection) emit rangeCleared();
}

void TimelineWidget::clearSelection() {
    drag_anchor_.reset();
    drag_current_.reset();
    update();
    emit rangeCleared();
}

std::optional<std::size_t> TimelineWidget::bucketAt(int x) const {
    if (buckets_.empty() || width() <= 0 || x < 0 || x >= width()) return std::nullopt;
    const std::size_t index = std::min(
        buckets_.size() - 1,
        static_cast<std::size_t>((static_cast<double>(x) / width()) * buckets_.size()));
    return index;
}

void TimelineWidget::publishSelection() {
    if (!drag_anchor_ || !drag_current_ || buckets_.empty()) return;
    const std::size_t first = std::min(*drag_anchor_, *drag_current_);
    const std::size_t last = std::max(*drag_anchor_, *drag_current_);
    const std::uint64_t begin = buckets_[first].start_ms;
    const std::uint64_t end = buckets_[last].start_ms + bucket_ms_;
    emit rangeSelected(static_cast<qulonglong>(begin), static_cast<qulonglong>(end));
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        clearSelection();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    drag_anchor_ = bucketAt(static_cast<int>(event->position().x()));
#else
    drag_anchor_ = bucketAt(event->pos().x());
#endif
    drag_current_ = drag_anchor_;
    update();
    event->accept();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!drag_anchor_ || !(event->buttons() & Qt::LeftButton)) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const auto current = bucketAt(static_cast<int>(event->position().x()));
#else
    const auto current = bucketAt(event->pos().x());
#endif
    if (current) drag_current_ = current;
    update();
    event->accept();
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !drag_anchor_) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const auto current = bucketAt(static_cast<int>(event->position().x()));
#else
    const auto current = bucketAt(event->pos().x());
#endif
    if (current) drag_current_ = current;
    update();
    publishSelection();
    event->accept();
}

void TimelineWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.fillRect(event->rect(), QColor(24, 26, 30));
    if (buckets_.empty() || peak_ == 0) {
        painter.setPen(QColor(120, 124, 132));
        painter.drawText(rect(), Qt::AlignCenter, tr("no timestamped records"));
        return;
    }

    const double slot = static_cast<double>(width()) / static_cast<double>(buckets_.size());
    const double barWidth = std::max(1.0, slot - 1.0);
    for (std::size_t i = 0; i < buckets_.size(); ++i) {
        double y = static_cast<double>(height());
        const double x = static_cast<double>(i) * slot;
        for (std::size_t level = 0; level < loglens::kLevelCount; ++level) {
            const std::size_t count = buckets_[i].level_counts[level];
            if (count == 0) {
                continue;
            }
            const double share = static_cast<double>(count) / static_cast<double>(peak_);
            const double barHeight = share * static_cast<double>(height());
            y -= barHeight;
            painter.fillRect(QRectF(x, y, barWidth, barHeight), QColor(kBarColours[level]));
        }
    }
    if (drag_anchor_ && drag_current_) {
        const std::size_t first = std::min(*drag_anchor_, *drag_current_);
        const std::size_t last = std::max(*drag_anchor_, *drag_current_);
        const qreal left = static_cast<qreal>(first) * slot;
        const qreal right = static_cast<qreal>(last + 1) * slot;
        painter.fillRect(QRectF(left, 0, right - left, height()), QColor(90, 160, 255, 55));
        painter.setPen(QPen(QColor(90, 160, 255), 1));
        painter.drawRect(QRectF(left, 0, right - left, height() - 1));
    }
}
