#include "loglens/gui/main_window.hpp"

#include "loglens/gui/log_model.hpp"

#include <QAbstractItemView>
#include <QLabel>
#include <QTableView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace {

QString utf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

QString windowText(const std::optional<loglens::TimeWindow>& window) {
    if (!window) return QObject::tr("not set");
    return QStringLiteral("[%1, %2) ms")
        .arg(static_cast<qulonglong>(window->begin_ms))
        .arg(static_cast<qulonglong>(window->end_ms));
}

} // namespace

void MainWindow::selectTimelineRange(qulonglong beginMs, qulonglong endMs) {
    selectedWindow_ = loglens::TimeWindow{static_cast<std::uint64_t>(beginMs),
                                          static_cast<std::uint64_t>(endMs)};
    model_->setTimeWindow(selectedWindow_);
    selectedWindowLabel_->setText(tr("Selected: %1").arg(windowText(selectedWindow_)));
    updateStatus(tr("Timeline range selected"));
    refreshRecordDetail();
}

void MainWindow::clearTimelineRange() {
    selectedWindow_.reset();
    model_->setTimeWindow(std::nullopt);
    selectedWindowLabel_->setText(tr("Selected: not set"));
    updateStatus(tr("Timeline range cleared"));
    refreshRecordDetail();
}

void MainWindow::setBaselineWindow() {
    if (!selectedWindow_) {
        updateStatus(tr("Select a timeline range before setting the baseline"));
        return;
    }
    baselineWindow_ = selectedWindow_;
    baselineWindowLabel_->setText(tr("Baseline: %1").arg(windowText(baselineWindow_)));
}

void MainWindow::setComparisonWindow() {
    if (!selectedWindow_) {
        updateStatus(tr("Select a timeline range before setting the comparison"));
        return;
    }
    comparisonWindow_ = selectedWindow_;
    comparisonWindowLabel_->setText(
        tr("Comparison: %1").arg(windowText(comparisonWindow_)));
}

void MainWindow::runWindowAnalysis() {
    analysisTree_->clear();
    if (!baselineWindow_ || !comparisonWindow_) {
        updateStatus(tr("Set both baseline and comparison ranges before comparing"));
        return;
    }
    const loglens::WindowAnalysis analysis = loglens::compareWindows(
        model_->visibleRecords(false), *baselineWindow_, *comparisonWindow_);
    for (const auto& signal : analysis.findings) {
        auto* item = new QTreeWidgetItem(analysisTree_);
        item->setText(0, QStringLiteral("%1 · %2=%3")
                             .arg(QString::fromLatin1(loglens::signalKindName(signal.kind)),
                                  utf8(signal.dimension), utf8(signal.key)));
        item->setText(1, QStringLiteral("score %1 · %2 · lines %3–%4")
                             .arg(signal.score, 0, 'f', 2)
                             .arg(utf8(signal.explanation))
                             .arg(static_cast<qulonglong>(signal.first_line))
                             .arg(static_cast<qulonglong>(signal.last_line)));
        item->setData(0, Qt::UserRole,
                      QVariant::fromValue(static_cast<qulonglong>(signal.first_line)));
    }
    for (const auto& group : analysis.correlations) {
        auto* item = new QTreeWidgetItem(analysisTree_);
        item->setText(0, tr("correlation · %1=%2")
                             .arg(utf8(group.field), utf8(group.value)));
        item->setText(1, tr("%1 record(s) · lines %2–%3")
                             .arg(static_cast<qulonglong>(group.count))
                             .arg(static_cast<qulonglong>(group.first_line))
                             .arg(static_cast<qulonglong>(group.last_line)));
        item->setData(0, Qt::UserRole,
                      QVariant::fromValue(static_cast<qulonglong>(group.first_line)));
    }
    updateStatus(tr("Compared %1 baseline and %2 comparison record(s); %3 result(s)")
                     .arg(static_cast<qulonglong>(analysis.baseline_records))
                     .arg(static_cast<qulonglong>(analysis.comparison_records))
                     .arg(analysisTree_->topLevelItemCount()));
}

void MainWindow::navigateAnalysisItem(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (item == nullptr) return;
    const std::size_t line = static_cast<std::size_t>(
        item->data(0, Qt::UserRole).toULongLong());
    const int row = model_->rowForLine(line);
    if (row < 0) {
        updateStatus(tr("Evidence line %1 is outside the current visible range")
                         .arg(static_cast<qulonglong>(line)));
        return;
    }
    table_->selectRow(row);
    table_->scrollTo(model_->index(row, LogModel::ColumnLine),
                     QAbstractItemView::PositionAtCenter);
    refreshRecordDetail();
}
