#include "loglens/gui/timeline_widget.hpp"

#include <QColor>
#include <QPainter>
#include <QPaintEvent>

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

void TimelineWidget::setBuckets(std::vector<loglens::Bucket> buckets) {
    buckets_ = std::move(buckets);
    peak_ = 0;
    for (const loglens::Bucket& bucket : buckets_) {
        peak_ = std::max(peak_, bucketTotal(bucket));
    }
    update();
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
}
