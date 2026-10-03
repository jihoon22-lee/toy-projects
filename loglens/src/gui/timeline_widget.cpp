#include "loglens/gui/timeline_widget.hpp"

#include <QColor>
#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QDateTime>
#include <QTimeZone>
#include <QPalette>

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
    setMinimumHeight(112);
    setMaximumHeight(160);
    setMouseTracking(true);
}

void TimelineWidget::setBuckets(std::vector<loglens::Bucket> buckets,
                                std::uint64_t bucketMs) {
    const bool interruptedDrag = dragging_;
    dragging_ = false;
    drag_anchor_.reset();
    drag_current_.reset();
    buckets_ = std::move(buckets);
    bucket_ms_ = std::max<std::uint64_t>(1, bucketMs);
    peak_ = 0;
    for (const loglens::Bucket& bucket : buckets_) {
        peak_ = std::max(peak_, bucketTotal(bucket));
    }
    update();
    if (interruptedDrag) {
        selected_range_.reset();
        emit rangeCleared();
    }
}

void TimelineWidget::setSelection(std::uint64_t beginMs, std::uint64_t endMs) {
    if (beginMs >= endMs) {
        clearSelection();
        return;
    }
    selected_range_ = std::make_pair(beginMs, endMs);
    update();
    emit rangeSelected(beginMs, endMs);
}

void TimelineWidget::clearSelection() {
    drag_anchor_.reset();
    drag_current_.reset();
    dragging_ = false;
    selected_range_.reset();
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
    setSelection(begin, end);
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        clearSelection();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    dragging_ = true;
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
    dragging_ = false;
    event->accept();
}

bool TimelineWidget::event(QEvent *event) {
    if (event->type() == QEvent::ToolTip) {
        const auto *help = static_cast<QHelpEvent *>(event);
        const auto index = bucketAt(help->pos().x());
        if (index) {
            const auto &bucket = buckets_[*index];
            QString text =
                QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(bucket.start_ms),
                                               QTimeZone::utc())
                    .toString(Qt::ISODate) +
                tr("\n%1 records / %2 seconds").arg(bucketTotal(bucket)).arg(bucket_ms_ / 1000.0);
            for (std::size_t i = 0; i < loglens::kLevelCount; ++i)
                text += QStringLiteral("\n%1: %2")
                            .arg(QString::fromLatin1(
                                loglens::levelName(static_cast<loglens::Level>(i))))
                            .arg(bucket.level_counts[i]);
            QToolTip::showText(help->globalPos(), text, this);
        } else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

void TimelineWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.fillRect(event->rect(), palette().color(QPalette::Base));
    if (buckets_.empty() || peak_ == 0) {
        painter.setPen(palette().color(QPalette::Text));
        painter.drawText(rect(), Qt::AlignCenter, tr("no timestamped records"));
        return;
    }

    const int topMargin = 22;
    const int bottomMargin = 22;
    const int plotHeight = std::max(1, height() - topMargin - bottomMargin);
    painter.setPen(palette().color(QPalette::Text));
    const QString begin = QDateTime::fromMSecsSinceEpoch(
                              static_cast<qint64>(buckets_.front().start_ms), QTimeZone::utc())
                              .toString(QStringLiteral("MM-dd HH:mm:ss 'UTC'"));
    const QString end =
        QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(buckets_.back().start_ms + bucket_ms_),
                                       QTimeZone::utc())
            .toString(QStringLiteral("HH:mm:ss 'UTC'"));
    painter.drawText(QRect(4, height() - bottomMargin, width() / 2, bottomMargin),
                     Qt::AlignLeft | Qt::AlignVCenter, begin);
    painter.drawText(QRect(width() / 2, height() - bottomMargin, width() / 2 - 4, bottomMargin),
                     Qt::AlignRight | Qt::AlignVCenter, end);
    const int legendSlot =
        std::max(42, std::min(76, width() / static_cast<int>(loglens::kLevelCount)));
    for (std::size_t i = 0; i < loglens::kLevelCount; ++i) {
        const int x = static_cast<int>(i) * legendSlot;
        painter.fillRect(QRect(x + 3, 6, 8, 8), QColor(kBarColours[i]));
        painter.drawText(QRect(x + 14, 0, legendSlot - 14, topMargin),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QString::fromLatin1(loglens::levelName(static_cast<loglens::Level>(i))));
    }
    const double slot = static_cast<double>(width()) / static_cast<double>(buckets_.size());
    const double barWidth = std::max(1.0, slot - 1.0);
    for (std::size_t i = 0; i < buckets_.size(); ++i) {
        double y = static_cast<double>(height() - bottomMargin);
        const double x = static_cast<double>(i) * slot;
        for (std::size_t level = 0; level < loglens::kLevelCount; ++level) {
            const std::size_t count = buckets_[i].level_counts[level];
            if (count == 0) {
                continue;
            }
            const double share = static_cast<double>(count) / static_cast<double>(peak_);
            const double barHeight = share * static_cast<double>(plotHeight);
            y -= barHeight;
            painter.fillRect(QRectF(x, y, barWidth, barHeight), QColor(kBarColours[level]));
        }
    }
    std::optional<std::size_t> selectedFirst;
    std::optional<std::size_t> selectedLast;
    if (selected_range_) {
        for (std::size_t i = 0; i < buckets_.size(); ++i) {
            const auto start = buckets_[i].start_ms;
            if (start < selected_range_->second &&
                (start >= selected_range_->first || selected_range_->first - start < bucket_ms_)) {
                if (!selectedFirst)
                    selectedFirst = i;
                selectedLast = i;
            }
        }
    }
    if (dragging_ && drag_anchor_ && drag_current_) {
        selectedFirst = std::min(*drag_anchor_, *drag_current_);
        selectedLast = std::max(*drag_anchor_, *drag_current_);
    }
    if (selectedFirst && selectedLast) {
        const std::size_t first = *selectedFirst;
        const std::size_t last = *selectedLast;
        const qreal left = static_cast<qreal>(first) * slot;
        const qreal right = static_cast<qreal>(last + 1) * slot;
        painter.fillRect(QRectF(left, 0, right - left, height()), QColor(90, 160, 255, 55));
        painter.setPen(QPen(QColor(90, 160, 255), 1));
        painter.drawRect(QRectF(left, 0, right - left, height() - 1));
    }
}
