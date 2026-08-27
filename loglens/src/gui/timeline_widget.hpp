#pragma once

#include <QWidget>

#include <vector>

#include "loglens/log_stats.hpp"

// Stacked per-level histogram over time — the "where did it go wrong" strip.
class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    void setBuckets(std::vector<loglens::Bucket> buckets);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<loglens::Bucket> buckets_;
    std::size_t peak_ = 0;
};
