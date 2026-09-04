#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QImage>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPainter>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QStyleOptionViewItem>
#include <QtTest>

#include "loglens/gui/log_model.hpp"
#include "loglens/gui/highlight_delegate.hpp"
#include "loglens/gui/main_window.hpp"
#include "loglens/gui/timeline_widget.hpp"
#include "loglens/triage.hpp"

namespace {

void writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(bytes), static_cast<qint64>(bytes.size()));
    QVERIFY(file.flush());
}

QByteArray evidenceLog() {
    return "2026-08-26T04:15:00.000Z INFO [api] ready request_id=base\n"
           "2026-08-26T04:16:01.000Z ERROR [worker] timeout job 10 request_id=req-7\n"
           "2026-08-26T04:16:02.000Z ERROR [worker] timeout job 11 request_id=req-7\n";
}

} // namespace

class TestGuiTriage : public QObject {
    Q_OBJECT

private slots:
    void timelinePublishesAndClearsHalfOpenRanges();
    void delegatePaintsHighlightedAndFallbackCells();
    void investigationWorkflowPersistsExportsAndNavigates();
    void emptyAndInvalidInvestigationActionsStaySafe();
    void diagnosticsAreRenderedAndExported();
    void startupTriageLoadReportsMalformedAndLegacyStores();
};

void TestGuiTriage::timelinePublishesAndClearsHalfOpenRanges() {
    TimelineWidget timeline;
    loglens::Bucket first;
    first.start_ms = 1000;
    first.level_counts[static_cast<std::size_t>(loglens::Level::Info)] = 1;
    loglens::Bucket second;
    second.start_ms = 61000;
    second.level_counts[static_cast<std::size_t>(loglens::Level::Error)] = 2;
    timeline.setBuckets({first, second}, 60000);
    timeline.resize(200, 80);
    timeline.show();

    QSignalSpy selected(&timeline, &TimelineWidget::rangeSelected);
    QSignalSpy cleared(&timeline, &TimelineWidget::rangeCleared);
    QTest::mouseClick(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(25, 40));
    QCOMPARE(selected.count(), 1);
    QCOMPARE(selected.at(0).at(0).toULongLong(), static_cast<qulonglong>(1000));
    QCOMPARE(selected.at(0).at(1).toULongLong(), static_cast<qulonglong>(61000));

    QTest::mouseClick(&timeline, Qt::RightButton, Qt::NoModifier, QPoint(25, 40));
    QCOMPARE(cleared.count(), 1);

    // A streaming refresh may replace the histogram between mouse press and
    // release. It must cancel stale indexes instead of indexing new buckets.
    timeline.setBuckets({first, second}, 60000);
    QTest::mousePress(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(175, 40));
    timeline.setBuckets({first}, 60000);
    QTest::mouseRelease(&timeline, Qt::LeftButton, Qt::NoModifier, QPoint(175, 40));
    QCOMPARE(cleared.count(), 2);
    QCOMPARE(selected.count(), 1);
}

void TestGuiTriage::delegatePaintsHighlightedAndFallbackCells() {
    LogModel model;
    loglens::LogRecord record;
    record.level = loglens::Level::Info;
    record.line_number = 7;
    record.message = u8"prefix 💥 timeout\nstack trace";
    record.raw = record.message;
    model.setRecords({record});
    loglens::TriageState triage;
    triage.rules.push_back({"emoji", {u8"💥", false, 20, "#111111"}});
    triage.rules.push_back({"timeout", {"timeout", false, 10, "#ffff00"}});
    model.setTriageState(triage, QStringLiteral("service.log"));

    QTableView view;
    view.setModel(&model);
    view.resize(420, 80);
    QStyleOptionViewItem option;
    option.initFrom(&view);
    option.rect = QRect(0, 0, 420, 40);
    option.widget = &view;
    HighlightDelegate delegate;
    QImage image(420, 80, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    QVERIFY(painter.isActive());

    // Non-message columns use the standard delegate. A light and a dark span
    // then exercise both readable foreground choices and UTF-8 -> UTF-16
    // offset conversion in the custom path.
    delegate.paint(&painter, option, model.index(0, LogModel::ColumnLine));
    delegate.paint(&painter, option, model.index(0, LogModel::ColumnMessage));
    option.widget = nullptr;
    delegate.paint(&painter, option, model.index(0, LogModel::ColumnMessage));

    record.message = std::string("broken-") + static_cast<char>(0xff) + " timeout";
    model.setRecords({record});
    model.setTriageState(triage, QStringLiteral("service.log"));
    QVERIFY(!model.highlightSpansAt(0).empty());
    // Malformed UTF-8 uses the standard delegate instead of applying a span
    // at an ambiguous byte-to-UTF-16 position.
    delegate.paint(&painter, option, model.index(0, LogModel::ColumnMessage));
    painter.end();
    QVERIFY(!image.isNull());
}

void TestGuiTriage::investigationWorkflowPersistsExportsAndNavigates() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString logPath = directory.filePath(QStringLiteral("service.log"));
    const QString triagePath = directory.filePath(QStringLiteral("triage.json"));
    const QString exportPath = directory.filePath(QStringLiteral("selection.json"));
    writeFile(logPath, evidenceLog());

    MainWindowOptions options;
    options.recordCapacity = 64;
    options.sourceProfilesPath = directory.filePath(QStringLiteral("profiles.json"));
    options.savedQueriesPath = directory.filePath(QStringLiteral("queries.json"));
    options.triagePath = triagePath;
    MainWindow window(nullptr, options);
    auto* table = window.findChild<QTableView*>(QStringLiteral("logTable"));
    auto* model = qobject_cast<LogModel*>(table == nullptr ? nullptr : table->model());
    QVERIFY(table != nullptr);
    QVERIFY(model != nullptr);
    window.openPath(logPath, loglens::InitialLoadMode::FromStart, 64);
    QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 3, 2500);

    auto* ruleName = window.findChild<QComboBox*>(QStringLiteral("highlightRuleComboBox"));
    auto* pattern = window.findChild<QLineEdit*>(QStringLiteral("highlightPatternEdit"));
    auto* color = window.findChild<QLineEdit*>(QStringLiteral("highlightStyleEdit"));
    auto* priority = window.findChild<QSpinBox*>(QStringLiteral("highlightPrioritySpinBox"));
    QVERIFY(ruleName != nullptr);
    QVERIFY(pattern != nullptr);
    QVERIFY(color != nullptr);
    QVERIFY(priority != nullptr);
    ruleName->setEditText(QStringLiteral("Timeout"));
    pattern->setText(QStringLiteral("timeout"));
    color->setText(QStringLiteral("#ffcc00"));
    priority->setValue(40);
    QVERIFY(QMetaObject::invokeMethod(&window, "saveHighlightRule", Qt::DirectConnection));
    const auto storedRule = loglens::loadTriageState(triagePath.toStdString());
    QVERIFY(storedRule.ok());
    QCOMPARE(storedRule.state.rules.size(), static_cast<std::size_t>(1));
    QCOMPARE(model->highlightSpansAt(1).size(), static_cast<std::size_t>(1));

    color->setText(QStringLiteral("red;url(x)"));
    QVERIFY(QMetaObject::invokeMethod(&window, "saveHighlightRule", Qt::DirectConnection));
    const auto stillValid = loglens::loadTriageState(triagePath.toStdString());
    QVERIFY(stillValid.ok());
    QCOMPARE(stillValid.state.rules.front().rule.style, std::string("#ffcc00"));

    table->selectRow(1);
    QCoreApplication::processEvents();
    auto* detail = window.findChild<QPlainTextEdit*>(QStringLiteral("recordDetail"));
    auto* bookmark = window.findChild<QCheckBox*>(QStringLiteral("bookmarkCheckBox"));
    auto* annotation = window.findChild<QLineEdit*>(QStringLiteral("annotationEdit"));
    QVERIFY(detail != nullptr);
    QVERIFY(bookmark != nullptr);
    QVERIFY(annotation != nullptr);
    QVERIFY(detail->toPlainText().contains(logPath + QStringLiteral(":2")));
    QVERIFY(detail->toPlainText().contains(QStringLiteral("Raw evidence")));
    bookmark->setChecked(true);
    annotation->setText(QStringLiteral("check upstream retry"));
    QVERIFY(QMetaObject::invokeMethod(&window, "saveRecordTriage", Qt::DirectConnection));
    const auto storedEntry = loglens::loadTriageState(triagePath.toStdString());
    QVERIFY(storedEntry.ok());
    QCOMPARE(storedEntry.state.entries.size(), static_cast<std::size_t>(1));
    QVERIFY(model->bookmarkedAt(1));

    table->selectionModel()->select(
        model->index(2, LogModel::ColumnLine),
        QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QVERIFY(window.exportSelectedRows(exportPath));
    QFile exported(exportPath);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QJsonParseError parseError;
    const QJsonDocument exportDocument =
        QJsonDocument::fromJson(exported.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QCOMPARE(exportDocument.object().value(QStringLiteral("schema")).toString(),
             QStringLiteral("loglens.selection/v1"));
    const QJsonArray exportedRecords =
        exportDocument.object().value(QStringLiteral("records")).toArray();
    QCOMPARE(exportedRecords.size(), 2);
    QCOMPARE(exportedRecords.at(0).toObject().value(QStringLiteral("annotation")).toString(),
             QStringLiteral("check upstream retry"));
    QVERIFY(QByteArray::fromBase64(
        exportedRecords.at(0).toObject().value(QStringLiteral("raw_base64"))
            .toString().toLatin1()).contains("timeout job 10"));
    QCOMPARE(QByteArray::fromBase64(
                 exportedRecords.at(0).toObject()
                     .value(QStringLiteral("message_base64")).toString().toLatin1()),
             QByteArray("timeout job 10 request_id=req-7"));
    QVERIFY(!window.exportSelectedRows(logPath));

    const loglens::LogRecord* baselineRecord = model->recordAt(0);
    QVERIFY(baselineRecord != nullptr);
    const qulonglong baselineBegin = baselineRecord->timestamp_ms;
    QVERIFY(QMetaObject::invokeMethod(&window, "selectTimelineRange", Qt::DirectConnection,
                                      Q_ARG(qulonglong, baselineBegin),
                                      Q_ARG(qulonglong, baselineBegin + 60000)));
    QCOMPARE(model->rowCount(), 1);
    QVERIFY(QMetaObject::invokeMethod(&window, "setBaselineWindow", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&window, "selectTimelineRange", Qt::DirectConnection,
                                      Q_ARG(qulonglong, baselineBegin + 60000),
                                      Q_ARG(qulonglong, baselineBegin + 120000)));
    QCOMPARE(model->rowCount(), 2);
    QVERIFY(QMetaObject::invokeMethod(&window, "setComparisonWindow", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&window, "runWindowAnalysis", Qt::DirectConnection));
    auto* results = window.findChild<QTreeWidget*>(QStringLiteral("windowAnalysisTree"));
    QVERIFY(results != nullptr);
    QVERIFY(results->topLevelItemCount() > 0);
    QTreeWidgetItem* evidence = results->topLevelItem(0);
    QVERIFY(QMetaObject::invokeMethod(&window, "navigateAnalysisItem", Qt::DirectConnection,
                                      Q_ARG(QTreeWidgetItem*, evidence), Q_ARG(int, 0)));
    QVERIFY(table->currentIndex().isValid());
    QCOMPARE(model->recordAt(table->currentIndex().row())->line_number,
             static_cast<std::size_t>(2));
}

void TestGuiTriage::emptyAndInvalidInvestigationActionsStaySafe() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MainWindowOptions options;
    options.sourceProfilesPath = directory.filePath(QStringLiteral("profiles.json"));
    options.savedQueriesPath = directory.filePath(QStringLiteral("queries.json"));
    options.triagePath = directory.filePath(QStringLiteral("triage.json"));
    MainWindow window(nullptr, options);
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusLabel"));
    auto* ruleName = window.findChild<QComboBox*>(QStringLiteral("highlightRuleComboBox"));
    auto* pattern = window.findChild<QLineEdit*>(QStringLiteral("highlightPatternEdit"));
    auto* style = window.findChild<QLineEdit*>(QStringLiteral("highlightStyleEdit"));
    QVERIFY(status != nullptr);
    QVERIFY(ruleName != nullptr);
    QVERIFY(pattern != nullptr);
    QVERIFY(style != nullptr);

    QVERIFY(QMetaObject::invokeMethod(&window, "saveRecordTriage", Qt::DirectConnection));
    QVERIFY(status->text().contains(QStringLiteral("Select a source record")));
    QVERIFY(!window.exportSelectedRows(directory.filePath(QStringLiteral("empty.json"))));
    QVERIFY(status->text().contains(QStringLiteral("Select at least one record")));
    QVERIFY(QMetaObject::invokeMethod(&window, "setBaselineWindow", Qt::DirectConnection));
    QVERIFY(status->text().contains(QStringLiteral("Select a timeline range")));
    QVERIFY(QMetaObject::invokeMethod(&window, "setComparisonWindow", Qt::DirectConnection));
    QVERIFY(status->text().contains(QStringLiteral("Select a timeline range")));
    QVERIFY(QMetaObject::invokeMethod(&window, "runWindowAnalysis", Qt::DirectConnection));
    QVERIFY(status->text().contains(QStringLiteral("Set both baseline")));
    QVERIFY(QMetaObject::invokeMethod(&window, "navigateAnalysisItem", Qt::DirectConnection,
                                      Q_ARG(QTreeWidgetItem*, nullptr), Q_ARG(int, 0)));
    QTreeWidgetItem outside;
    outside.setData(0, Qt::UserRole, QVariant::fromValue<qulonglong>(999));
    QVERIFY(QMetaObject::invokeMethod(&window, "navigateAnalysisItem", Qt::DirectConnection,
                                      Q_ARG(QTreeWidgetItem*, &outside), Q_ARG(int, 0)));
    QVERIFY(status->text().contains(QStringLiteral("outside the current visible range")));
    QVERIFY(QMetaObject::invokeMethod(&window, "deleteHighlightRule", Qt::DirectConnection));
    QVERIFY(status->text().contains(QStringLiteral("delete highlight rule")));
    QVERIFY(QMetaObject::invokeMethod(&window, "moveHighlightRuleUp", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&window, "moveHighlightRuleDown", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&window, "selectHighlightRule", Qt::DirectConnection,
                                      Q_ARG(int, -1)));

    // Build two valid rules through the same controls as the user, then cover
    // both reorder directions and deletion. The operations are persisted and
    // the final state is checked from disk rather than private widget state.
    ruleName->setEditText(QStringLiteral("First"));
    pattern->setText(QStringLiteral("first"));
    style->setText(QStringLiteral("#ffee00"));
    QVERIFY(QMetaObject::invokeMethod(&window, "saveHighlightRule", Qt::DirectConnection));
    ruleName->setEditText(QStringLiteral("Second"));
    pattern->setText(QStringLiteral("second"));
    style->setText(QStringLiteral("#101010"));
    QVERIFY(QMetaObject::invokeMethod(&window, "saveHighlightRule", Qt::DirectConnection));
    QCOMPARE(ruleName->count(), 2);
    ruleName->setCurrentIndex(1);
    QVERIFY(QMetaObject::invokeMethod(&window, "moveHighlightRuleUp", Qt::DirectConnection));
    ruleName->setCurrentIndex(0);
    QVERIFY(QMetaObject::invokeMethod(&window, "moveHighlightRuleUp", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&window, "moveHighlightRuleDown", Qt::DirectConnection));
    ruleName->setCurrentIndex(ruleName->count() - 1);
    QVERIFY(QMetaObject::invokeMethod(&window, "moveHighlightRuleDown", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&window, "deleteHighlightRule", Qt::DirectConnection));
    const auto stored = loglens::loadTriageState(options.triagePath.toStdString());
    QVERIFY(stored.ok());
    QCOMPARE(stored.state.rules.size(), static_cast<std::size_t>(1));
}

void TestGuiTriage::diagnosticsAreRenderedAndExported() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString logPath = directory.filePath(QStringLiteral("malformed.log"));
    const QByteArray malformed =
        "{\"level\":\"info\",\"msg\":\"unterminated}\n"
        "2026-08-26T04:15:00.000Z INFO [api] ready\n";
    writeFile(logPath, malformed);
    MainWindowOptions options;
    options.sourceProfilesPath = directory.filePath(QStringLiteral("profiles.json"));
    options.savedQueriesPath = directory.filePath(QStringLiteral("queries.json"));
    options.triagePath = directory.filePath(QStringLiteral("triage.json"));
    MainWindow window(nullptr, options);
    auto* table = window.findChild<QTableView*>(QStringLiteral("logTable"));
    auto* model = qobject_cast<LogModel*>(table == nullptr ? nullptr : table->model());
    auto* detail = window.findChild<QPlainTextEdit*>(QStringLiteral("recordDetail"));
    QVERIFY(table != nullptr);
    QVERIFY(model != nullptr);
    QVERIFY(detail != nullptr);
    window.openPath(logPath, loglens::InitialLoadMode::FromStart, 64);
    QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 2, 2500);
    table->selectRow(0);
    QCoreApplication::processEvents();
    QVERIFY(detail->toPlainText().contains(QStringLiteral("Diagnostic:")));
    const QString exportPath = directory.filePath(QStringLiteral("diagnostics.json"));
    QVERIFY(window.exportSelectedRows(exportPath));
    QFile exported(exportPath);
    QVERIFY(exported.open(QIODevice::ReadOnly));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(exported.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    const QJsonArray records = document.object().value(QStringLiteral("records")).toArray();
    QCOMPARE(records.size(), 1);
    const QJsonArray diagnostics = records.at(0).toObject()
                                       .value(QStringLiteral("diagnostics"))
                                       .toArray();
    QVERIFY(!diagnostics.isEmpty());
    QVERIFY(diagnostics.at(0).toObject().contains(QStringLiteral("code")));
    QCOMPARE(QByteArray::fromBase64(
                 records.at(0).toObject().value(QStringLiteral("source_base64"))
                     .toString().toLatin1()),
             QByteArray());
    QVERIFY(!window.exportSelectedRows(directory.path()));
}

void TestGuiTriage::startupTriageLoadReportsMalformedAndLegacyStores() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    MainWindowOptions options;
    options.sourceProfilesPath = directory.filePath(QStringLiteral("profiles.json"));
    options.savedQueriesPath = directory.filePath(QStringLiteral("queries.json"));
    options.triagePath = directory.filePath(QStringLiteral("malformed-triage.json"));
    writeFile(options.triagePath,
              "{\"schema\":\"loglens.triage/v1\",\"rules\":[],"
              "\"entries\":[],\"unexpected\":true}\n");
    {
        MainWindow window(nullptr, options);
        auto* status = window.findChild<QLabel*>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QVERIFY(status->text().contains(QStringLiteral("load triage workflow")));
    }

    options.triagePath = directory.filePath(QStringLiteral("legacy-triage.json"));
    writeFile(options.triagePath,
              "{\"schema\":\"loglens.triage/v0\",\"rules\":["
              "{\"pattern\":\"panic\",\"color\":\"#ff0000\"}]}\n");
    MainWindow window(nullptr, options);
    auto* status = window.findChild<QLabel*>(QStringLiteral("statusLabel"));
    auto* rules = window.findChild<QComboBox*>(QStringLiteral("highlightRuleComboBox"));
    QVERIFY(status != nullptr);
    QVERIFY(rules != nullptr);
    QVERIFY(status->text().contains(QStringLiteral("legacy highlight rules")));
    QCOMPARE(rules->count(), 1);
    QCOMPARE(rules->itemText(0), QStringLiteral("Migrated rule 1"));
}

QTEST_MAIN(TestGuiTriage)
#include "test_gui_triage.moc"
