#include "buildscope/main_window.hpp"

#include "buildscope/compilation_model.hpp"
#include "buildscope/diff.hpp"
#include "buildscope/diff_model.hpp"
#include "ui_main_window.h"

#include <QHeaderView>
#include <QTableWidgetItem>

namespace buildscope {
namespace {

QTableWidgetItem *diffTableItem(const QString &text) {
    auto *item = new QTableWidgetItem(text);
    item->setToolTip(text);
    return item;
}

QString diffSourceLabel(const DiffSource &source) {
    if (source.before.has_value() && source.after.has_value() &&
        *source.before != *source.after) {
        return *source.before + QStringLiteral(" → ") + *source.after;
    }
    return source.after.has_value() ? *source.after : source.before.value_or(QString());
}

}  // namespace

void MainWindow::setDiffMode(bool enabled) {
    diffMode_ = enabled;
    ui_->pathEdit->setPlaceholderText(
        enabled ? tr("Open a BuildScope configuration diff report")
                : tr("Open a BuildScope snapshot generated from compile_commands.json"));
    ui_->filterEdit->setPlaceholderText(
        enabled ? tr("Source, kind, category, before/after value, suppression…")
                : tr("Source, target, compiler, define, include, status…"));
    for (auto *tab : {ui_->overviewTab, ui_->commandTab, ui_->definesTab,
                      ui_->includesTab, ui_->includeExplanationTab}) {
        ui_->detailTabs->setTabEnabled(ui_->detailTabs->indexOf(tab), !enabled);
    }
    ui_->detailTabs->setTabEnabled(ui_->detailTabs->indexOf(ui_->diagnosticsTab), true);
    ui_->detailTabs->setTabEnabled(ui_->detailTabs->indexOf(ui_->diffTab), enabled);
    ui_->detailTabs->setCurrentWidget(enabled ? ui_->diffTab : ui_->overviewTab);
    ui_->sourceTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    const auto columns = enabled ? static_cast<int>(DiffTreeModel::ColumnCount)
                                 : static_cast<int>(CompilationTreeModel::ColumnCount);
    for (int column = 1; column < columns; ++column) {
        ui_->sourceTree->header()->setSectionResizeMode(column,
                                                        QHeaderView::ResizeToContents);
    }
}

void MainWindow::showDiffUnit(const DiffUnit &unit,
                              std::optional<qsizetype> selectedChange) {
    const auto source = diffSourceLabel(unit.source);
    ui_->selectionLabel->setText(source);
    ui_->diffSummaryLabel->setText(
        tr("%1 · %2 change(s) · %3")
            .arg(unit.kind)
            .arg(unit.changes.size())
            .arg(unit.suppressed ? tr("fully suppressed") : tr("visible drift")));
    ui_->diffChangeTable->setRowCount(unit.changes.size());
    for (qsizetype row = 0; row < unit.changes.size(); ++row) {
        const auto &change = unit.changes.at(row);
        ui_->diffChangeTable->setItem(row, 0, diffTableItem(change.category));
        ui_->diffChangeTable->setItem(row, 1,
                                      diffTableItem(renderDiffValue(change.before)));
        ui_->diffChangeTable->setItem(row, 2,
                                      diffTableItem(renderDiffValue(change.after)));
        ui_->diffChangeTable->setItem(
            row, 3,
            diffTableItem(change.suppression.has_value() ? *change.suppression
                                                         : tr("visible")));
    }
    if (selectedChange.has_value() && *selectedChange < unit.changes.size()) {
        ui_->diffChangeTable->selectRow(*selectedChange);
    } else {
        ui_->diffChangeTable->clearSelection();
    }
    ui_->detailTabs->setCurrentWidget(ui_->diffTab);
}

}  // namespace buildscope
