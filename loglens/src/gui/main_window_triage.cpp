#include "loglens/gui/main_window.hpp"

#include "loglens/gui/log_model.hpp"
#include "loglens/gui/timeline_widget.hpp"

#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableView>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <cstddef>
#include <string>

namespace {

QString utf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

std::string bytes(const QString& value) {
    const QByteArray encoded = value.toUtf8();
    return std::string(encoded.constData(), static_cast<std::size_t>(encoded.size()));
}

} // namespace

void MainWindow::setupInvestigationDock() {
    auto* dock = new QDockWidget(tr("Investigation"), this);
    dock->setObjectName(QStringLiteral("investigationDock"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* tabs = new QTabWidget(dock);
    tabs->setObjectName(QStringLiteral("investigationTabs"));

    auto* recordTab = new QWidget(tabs);
    auto* recordLayout = new QVBoxLayout(recordTab);
    recordDetail_ = new QPlainTextEdit(recordTab);
    recordDetail_->setObjectName(QStringLiteral("recordDetail"));
    recordDetail_->setAccessibleName(tr("Selected log record details"));
    recordDetail_->setReadOnly(true);
    recordDetail_->setPlaceholderText(tr("Select a record to inspect parsed fields and raw evidence."));
    recordLayout->addWidget(recordDetail_, 1);
    bookmarkBox_ = new QCheckBox(tr("Bookmark this source line"), recordTab);
    bookmarkBox_->setObjectName(QStringLiteral("bookmarkCheckBox"));
    recordLayout->addWidget(bookmarkBox_);
    annotationEdit_ = new QLineEdit(recordTab);
    annotationEdit_->setObjectName(QStringLiteral("annotationEdit"));
    annotationEdit_->setAccessibleName(tr("Source line annotation"));
    annotationEdit_->setMaxLength(static_cast<int>(loglens::kMaxAnnotationBytes));
    annotationEdit_->setPlaceholderText(tr("Investigation note"));
    recordLayout->addWidget(annotationEdit_);
    auto* recordButtons = new QHBoxLayout();
    auto* saveRecord = new QPushButton(tr("Save note"), recordTab);
    saveRecord->setObjectName(QStringLiteral("saveRecordTriageButton"));
    auto* exportRows = new QPushButton(tr("Export selected…"), recordTab);
    exportRows->setObjectName(QStringLiteral("exportSelectedRowsButton"));
    recordButtons->addWidget(saveRecord);
    recordButtons->addWidget(exportRows);
    recordLayout->addLayout(recordButtons);
    tabs->addTab(recordTab, tr("Record"));

    auto* rulesTab = new QWidget(tabs);
    auto* rulesLayout = new QVBoxLayout(rulesTab);
    auto* rulesForm = new QFormLayout();
    highlightRule_ = new QComboBox(rulesTab);
    highlightRule_->setObjectName(QStringLiteral("highlightRuleComboBox"));
    highlightRule_->setAccessibleName(tr("Highlight rule name"));
    highlightRule_->setEditable(true);
    highlightRule_->setInsertPolicy(QComboBox::NoInsert);
    highlightRule_->setMaxCount(static_cast<int>(loglens::kMaxHighlightRules));
    highlightRule_->lineEdit()->setMaxLength(
        static_cast<int>(loglens::kMaxPersistedNameBytes));
    rulesForm->addRow(tr("Name"), highlightRule_);
    highlightPattern_ = new QLineEdit(rulesTab);
    highlightPattern_->setObjectName(QStringLiteral("highlightPatternEdit"));
    highlightPattern_->setMaxLength(static_cast<int>(loglens::kMaxHighlightPatternBytes));
    rulesForm->addRow(tr("Literal pattern"), highlightPattern_);
    highlightStyle_ = new QLineEdit(QStringLiteral("#ffd54f"), rulesTab);
    highlightStyle_->setObjectName(QStringLiteral("highlightStyleEdit"));
    highlightStyle_->setMaxLength(64);
    rulesForm->addRow(tr("Color"), highlightStyle_);
    highlightPriority_ = new QSpinBox(rulesTab);
    highlightPriority_->setObjectName(QStringLiteral("highlightPrioritySpinBox"));
    highlightPriority_->setRange(-1000000, 1000000);
    rulesForm->addRow(tr("Priority"), highlightPriority_);
    highlightWholeLine_ = new QCheckBox(tr("Highlight the whole row"), rulesTab);
    highlightWholeLine_->setObjectName(QStringLiteral("highlightWholeLineCheckBox"));
    rulesForm->addRow(QString(), highlightWholeLine_);
    rulesLayout->addLayout(rulesForm);
    highlightPreview_ = new QLabel(tr("Preview: timeout while contacting upstream"), rulesTab);
    highlightPreview_->setObjectName(QStringLiteral("highlightPreview"));
    highlightPreview_->setAutoFillBackground(true);
    highlightPreview_->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    rulesLayout->addWidget(highlightPreview_);
    auto* ruleButtons = new QHBoxLayout();
    auto* saveRule = new QPushButton(tr("Save"), rulesTab);
    saveRule->setObjectName(QStringLiteral("saveHighlightRuleButton"));
    auto* deleteRule = new QPushButton(tr("Delete"), rulesTab);
    deleteRule->setObjectName(QStringLiteral("deleteHighlightRuleButton"));
    auto* moveUp = new QPushButton(tr("Up"), rulesTab);
    moveUp->setObjectName(QStringLiteral("moveHighlightRuleUpButton"));
    auto* moveDown = new QPushButton(tr("Down"), rulesTab);
    moveDown->setObjectName(QStringLiteral("moveHighlightRuleDownButton"));
    ruleButtons->addWidget(saveRule);
    ruleButtons->addWidget(deleteRule);
    ruleButtons->addWidget(moveUp);
    ruleButtons->addWidget(moveDown);
    rulesLayout->addLayout(ruleButtons);
    rulesLayout->addStretch();
    tabs->addTab(rulesTab, tr("Highlights"));

    auto* analysisTab = new QWidget(tabs);
    auto* analysisLayout = new QVBoxLayout(analysisTab);
    selectedWindowLabel_ = new QLabel(tr("Selected: not set"), analysisTab);
    selectedWindowLabel_->setObjectName(QStringLiteral("selectedWindowLabel"));
    baselineWindowLabel_ = new QLabel(tr("Baseline: not set"), analysisTab);
    baselineWindowLabel_->setObjectName(QStringLiteral("baselineWindowLabel"));
    comparisonWindowLabel_ = new QLabel(tr("Comparison: not set"), analysisTab);
    comparisonWindowLabel_->setObjectName(QStringLiteral("comparisonWindowLabel"));
    analysisLayout->addWidget(selectedWindowLabel_);
    analysisLayout->addWidget(baselineWindowLabel_);
    analysisLayout->addWidget(comparisonWindowLabel_);
    auto* windowButtons = new QHBoxLayout();
    auto* setBaseline = new QPushButton(tr("Use as baseline"), analysisTab);
    setBaseline->setObjectName(QStringLiteral("setBaselineWindowButton"));
    auto* setComparison = new QPushButton(tr("Use as comparison"), analysisTab);
    setComparison->setObjectName(QStringLiteral("setComparisonWindowButton"));
    auto* analyze = new QPushButton(tr("Compare"), analysisTab);
    analyze->setObjectName(QStringLiteral("runWindowAnalysisButton"));
    auto* clear = new QPushButton(tr("Clear range"), analysisTab);
    clear->setObjectName(QStringLiteral("clearTimelineRangeButton"));
    windowButtons->addWidget(setBaseline);
    windowButtons->addWidget(setComparison);
    windowButtons->addWidget(analyze);
    windowButtons->addWidget(clear);
    analysisLayout->addLayout(windowButtons);
    analysisTree_ = new QTreeWidget(analysisTab);
    analysisTree_->setObjectName(QStringLiteral("windowAnalysisTree"));
    analysisTree_->setAccessibleName(tr("Window comparison signals"));
    analysisTree_->setColumnCount(2);
    analysisTree_->setHeaderLabels({tr("Signal"), tr("Evidence")});
    analysisTree_->setRootIsDecorated(false);
    analysisLayout->addWidget(analysisTree_, 1);
    tabs->addTab(analysisTab, tr("Compare"));

    dock->setWidget(tabs);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    connect(highlightRule_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &MainWindow::selectHighlightRule);
    connect(highlightPattern_, &QLineEdit::textChanged, this,
            &MainWindow::updateHighlightPreview);
    connect(highlightStyle_, &QLineEdit::textChanged, this,
            &MainWindow::updateHighlightPreview);
    connect(highlightWholeLine_, &QCheckBox::toggled, this,
            &MainWindow::updateHighlightPreview);
    connect(saveRule, &QPushButton::clicked, this, &MainWindow::saveHighlightRule);
    connect(deleteRule, &QPushButton::clicked, this, &MainWindow::deleteHighlightRule);
    connect(moveUp, &QPushButton::clicked, this, &MainWindow::moveHighlightRuleUp);
    connect(moveDown, &QPushButton::clicked, this, &MainWindow::moveHighlightRuleDown);
    connect(saveRecord, &QPushButton::clicked, this, &MainWindow::saveRecordTriage);
    connect(exportRows, &QPushButton::clicked, this, &MainWindow::chooseExportPath);
    connect(setBaseline, &QPushButton::clicked, this, &MainWindow::setBaselineWindow);
    connect(setComparison, &QPushButton::clicked, this,
            &MainWindow::setComparisonWindow);
    connect(analyze, &QPushButton::clicked, this, &MainWindow::runWindowAnalysis);
    connect(clear, &QPushButton::clicked, timeline_, &TimelineWidget::clearSelection);
    connect(analysisTree_, &QTreeWidget::itemActivated, this,
            &MainWindow::navigateAnalysisItem);
    refreshRecordDetail();
    updateHighlightPreview();
}

void MainWindow::loadTriageWorkflow() {
    const loglens::TriageLoadResult loaded =
        loglens::loadTriageState(triagePath_.toStdString());
    if (!loaded.ok()) {
        showPersistenceError(tr("load triage workflow"), loaded.error);
    } else {
        triageState_ = loaded.state;
        if (loaded.migrated) {
            updateStatus(tr("Loaded legacy highlight rules; the next save will upgrade them"));
        }
    }
    rebuildHighlightRules();
    model_->setTriageState(triageState_, currentPath_);
}

void MainWindow::rebuildHighlightRules(const QString& selectedName) {
    const QString requested = selectedName.isEmpty() ? highlightRule_->currentText()
                                                      : selectedName;
    const QSignalBlocker blocker(highlightRule_);
    highlightRule_->clear();
    for (const auto& item : triageState_.rules) highlightRule_->addItem(utf8(item.name));
    if (triageState_.rules.empty()) {
        highlightRule_->setEditText(requested);
        return;
    }
    int selected = highlightRule_->findText(requested, Qt::MatchExactly);
    if (selected < 0) selected = 0;
    highlightRule_->setCurrentIndex(selected);
    selectHighlightRule(selected);
}

void MainWindow::selectHighlightRule(int index) {
    if (index < 0 || index >= static_cast<int>(triageState_.rules.size())) return;
    const auto& selected = triageState_.rules[static_cast<std::size_t>(index)];
    const QSignalBlocker patternBlocker(highlightPattern_);
    const QSignalBlocker styleBlocker(highlightStyle_);
    const QSignalBlocker wholeLineBlocker(highlightWholeLine_);
    const QSignalBlocker priorityBlocker(highlightPriority_);
    highlightPattern_->setText(utf8(selected.rule.pattern));
    highlightStyle_->setText(utf8(selected.rule.style));
    highlightWholeLine_->setChecked(selected.rule.whole_line);
    highlightPriority_->setValue(selected.rule.priority);
    updateHighlightPreview();
}

void MainWindow::updateHighlightPreview() {
    QColor color(highlightStyle_->text());
    QPalette palette = highlightPreview_->palette();
    palette.setColor(QPalette::Window, color.isValid() ? color : QColor("#5c1f1f"));
    palette.setColor(QPalette::WindowText,
                     color.isValid() && color.lightnessF() > 0.52 ? Qt::black : Qt::white);
    highlightPreview_->setPalette(palette);
    const QString sample = highlightPattern_->text().isEmpty()
                               ? tr("timeout while contacting upstream")
                               : highlightPattern_->text();
    highlightPreview_->setText(color.isValid() ? tr("Preview: %1").arg(sample)
                                               : tr("Invalid color: %1").arg(sample));
}

bool MainWindow::prepareTriageStoreDirectory() {
    if (!triagePathIsDefault_) return true;
    const QDir parent(QFileInfo(triagePath_).absolutePath());
    if (parent.exists() || QDir().mkpath(parent.absolutePath())) return true;
    loglens::PersistenceError error;
    error.code = loglens::PersistenceErrorCode::Io;
    error.message = "cannot create the default triage directory";
    showPersistenceError(tr("prepare triage store"), error);
    return false;
}

bool MainWindow::writeTriageWorkflow(const loglens::TriageState& state,
                                     const QString& action) {
    if (!prepareTriageStoreDirectory()) return false;
    loglens::PersistenceError error;
    if (!loglens::saveTriageState(triagePath_.toStdString(), state, error)) {
        showPersistenceError(action, error);
        return false;
    }
    return true;
}

void MainWindow::saveHighlightRule() {
    const QString name = highlightRule_->currentText().trimmed();
    loglens::NamedHighlightRule candidate;
    candidate.name = bytes(name);
    candidate.rule.pattern = bytes(highlightPattern_->text());
    candidate.rule.whole_line = highlightWholeLine_->isChecked();
    candidate.rule.priority = highlightPriority_->value();
    candidate.rule.style = bytes(highlightStyle_->text());
    loglens::TriageState next = triageState_;
    loglens::PersistenceError error;
    if (!loglens::upsertHighlightRule(next, std::move(candidate), error)) {
        showPersistenceError(tr("save highlight rule"), error);
        return;
    }
    if (!writeTriageWorkflow(next, tr("save highlight rule"))) return;
    triageState_ = std::move(next);
    rebuildHighlightRules(name);
    model_->setTriageState(triageState_, currentPath_);
    updateStatus(tr("Saved highlight rule '%1'").arg(name));
}

void MainWindow::deleteHighlightRule() {
    const QString name = highlightRule_->currentText();
    loglens::TriageState next = triageState_;
    loglens::PersistenceError error;
    if (!loglens::removeHighlightRule(next, bytes(name), error)) {
        showPersistenceError(tr("delete highlight rule"), error);
        return;
    }
    if (!writeTriageWorkflow(next, tr("delete highlight rule"))) return;
    triageState_ = std::move(next);
    rebuildHighlightRules();
    model_->setTriageState(triageState_, currentPath_);
    updateStatus(tr("Deleted highlight rule '%1'").arg(name));
}

void MainWindow::moveHighlightRuleUp() {
    const int index = highlightRule_->currentIndex();
    if (index <= 0) return;
    loglens::TriageState next = triageState_;
    loglens::PersistenceError error;
    if (!loglens::moveHighlightRule(next, static_cast<std::size_t>(index),
                                    static_cast<std::size_t>(index - 1), error)
        || !writeTriageWorkflow(next, tr("reorder highlight rules"))) return;
    const QString name = highlightRule_->currentText();
    triageState_ = std::move(next);
    rebuildHighlightRules(name);
    model_->setTriageState(triageState_, currentPath_);
}

void MainWindow::moveHighlightRuleDown() {
    const int index = highlightRule_->currentIndex();
    if (index < 0 || index + 1 >= static_cast<int>(triageState_.rules.size())) return;
    loglens::TriageState next = triageState_;
    loglens::PersistenceError error;
    if (!loglens::moveHighlightRule(next, static_cast<std::size_t>(index),
                                    static_cast<std::size_t>(index + 1), error)
        || !writeTriageWorkflow(next, tr("reorder highlight rules"))) return;
    const QString name = highlightRule_->currentText();
    triageState_ = std::move(next);
    rebuildHighlightRules(name);
    model_->setTriageState(triageState_, currentPath_);
}
