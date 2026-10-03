#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTableView>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QThread>
#include <QSignalSpy>
#include <QStatusBar>
#include <QtTest>

#include "loglens/gui/main_window.hpp"
#include "loglens/gui/log_model.hpp"
#include "loglens/gui/timeline_widget.hpp"
#include "loglens/persistence.hpp"
#include "loglens/triage.hpp"

namespace {
void put(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
    file.close();
}
MainWindowOptions options(const QTemporaryDir &directory, std::size_t capacity = 1000) {
    MainWindowOptions value;
    value.recordCapacity = capacity;
    value.sourceProfilesPath = directory.filePath("profiles.json");
    value.savedQueriesPath = directory.filePath("queries.json");
    value.triagePath = directory.filePath("triage.json");
    return value;
}
LogModel *model(MainWindow &window) {
    return qobject_cast<LogModel *>(window.findChild<QTableView *>("logTable")->model());
}
} // namespace
class TestGuiExtensions : public QObject {
    Q_OBJECT
private slots:
    void replacingSourceArchivesNotes();
    void sessionRestoresPluginInvestigationAndLayout();
    void wholeFileSearchIsSeparateAndCancellable();
    void compactLayoutAndThemeKeepTextReadable();
};

void TestGuiExtensions::replacingSourceArchivesNotes() {
    QTemporaryDir directory;
    const auto path = directory.filePath("app.log");
    put(path, "old first\nold second\n");
    MainWindow window(nullptr, options(directory));
    window.show();
    window.openPath(path, loglens::InitialLoadMode::FromStart, 100);
    QTRY_COMPARE(model(window)->rowCount(), 2);
    window.findChild<QTableView *>("logTable")->selectRow(0);
    window.findChild<QCheckBox *>("bookmarkCheckBox")->setChecked(true);
    window.findChild<QLineEdit *>("annotationEdit")->setText("old note");
    QVERIFY(QMetaObject::invokeMethod(&window, "saveRecordTriage"));
    QVERIFY(model(window)->bookmarkedAt(0));
    QVERIFY(QFile::rename(path, path + ".old"));
    put(path, "new first\nnew second\n");
    QVERIFY(QMetaObject::invokeMethod(&window, "pollSource"));
    QTRY_VERIFY(model(window)->recordAt(0) && model(window)->recordAt(0)->message == "new first");
    QVERIFY(!model(window)->bookmarkedAt(0));
    const auto persisted =
        loglens::loadTriageState(directory.filePath("triage.json").toStdString());
    QVERIFY(persisted.ok());
    QCOMPARE(persisted.state.entries.size(), std::size_t(1));
    QTRY_COMPARE(window.findChild<QTreeWidget *>("archivedTriageTree")->topLevelItemCount(), 1);
    QVERIFY(window.findChild<QTreeWidget *>("archivedTriageTree")
                ->topLevelItem(0)
                ->text(2)
                .contains("Archived"));
}

void TestGuiExtensions::sessionRestoresPluginInvestigationAndLayout() {
    QTemporaryDir directory;
    const auto path = directory.filePath("app.log");
    const auto plugin = directory.filePath("parser.json");
    const auto session = directory.filePath("investigation.session.json");
    put(plugin,
        R"json({"kind":"loglens.format/v1","name":"pipe","pattern":"^(\\S+) ([A-Z]+)\\|(.*)$","fields":{"timestamp":1,"level":2,"message":3}})json");
    put(path, "2026-01-01T00:00:00Z INFO|ordinary\n2026-01-01T00:01:00Z ERROR|needle\n");
    MainWindow window(nullptr, options(directory));
    window.show();
    QVERIFY(window.setFormatPluginPath(plugin));
    window.findChild<QCheckBox *>("followCheckBox")->setChecked(false);
    window.openPath(path, loglens::InitialLoadMode::FromStart, 100);
    QTRY_COMPARE(model(window)->rowCount(), 2);
    const auto start = model(window)->recordAt(0)->timestamp_ms;
    window.findChild<QLineEdit *>("searchEdit")->setText("needle");
    window.findChild<QLineEdit *>("filterEdit")->setText("level>=ERROR");
    QVERIFY(QMetaObject::invokeMethod(&window, "applyFilter"));
    QCOMPARE(model(window)->rowCount(), 1);
    window.findChild<QTableView *>("logTable")->selectRow(0);
    window.findChild<QCheckBox *>("bookmarkCheckBox")->setChecked(true);
    window.findChild<QLineEdit *>("annotationEdit")->setText("investigate request");
    QVERIFY(QMetaObject::invokeMethod(&window, "saveRecordTriage"));
    auto *timeline = window.findChild<TimelineWidget *>("timelineWidget");
    timeline->setSelection(start, start + 60000);
    QVERIFY(QMetaObject::invokeMethod(&window, "setBaselineWindow"));
    timeline->setSelection(start + 60000, start + 120000);
    QVERIFY(QMetaObject::invokeMethod(&window, "setComparisonWindow"));
    window.findChild<QTableView *>("logTable")->setColumnWidth(0, 133);
    QVERIFY(window.saveSessionTo(session));
    const auto saved = loglens::loadSession(session.toStdString());
    QVERIFY2(saved.ok(), saved.error.message.c_str());
    QCOMPARE(saved.state.search, std::string("needle"));
    QVERIFY(!saved.state.plugin_fingerprint.empty());
    QVERIFY(saved.state.baseline_window);
    QVERIFY(saved.state.comparison_window);
    QVERIFY(!saved.state.layout.empty());
    window.findChild<QLineEdit *>("searchEdit")->setText("different");
    QVERIFY(window.setFormatPluginPath(QString()));
    window.findChild<QCheckBox *>("followCheckBox")->setChecked(true);
    QVERIFY(window.openSession(session));
    QTRY_COMPARE(model(window)->rowCount(), 1);
    QCOMPARE(window.findChild<QLineEdit *>("searchEdit")->text(), QStringLiteral("needle"));
    QVERIFY(!window.findChild<QCheckBox *>("followCheckBox")->isChecked());
    QVERIFY(window.findChild<QLabel *>("formatPluginLabel")->text().contains("pipe"));
    QVERIFY(model(window)->bookmarkedAt(0));
    QCOMPARE(window.findChild<QTableView *>("logTable")->columnWidth(0), 133);
    QVERIFY(window.findChild<QLabel *>("comparisonWindowLabel")
                ->text()
                .contains(QString::number(start + 60000)));
    window.findChild<QTableView *>("logTable")->selectRow(0);
    QTest::qWait(75);
    const auto screenshot = qEnvironmentVariable("LOGLENS_GUI_SCREENSHOT");
    if (!screenshot.isEmpty())
        QVERIFY(window.grab().save(screenshot + ".workflow.png"));
    QFile parserFile(plugin);
    QVERIFY(parserFile.open(QIODevice::ReadOnly));
    auto changed = parserFile.readAll(); parserFile.close();
    changed.replace("pipe", "changed");
    put(plugin, changed);
    QVERIFY(!window.saveSessionTo(session));
    QCOMPARE(loglens::loadSession(session.toStdString()).state.plugin_fingerprint,
             saved.state.plugin_fingerprint);
}

void TestGuiExtensions::wholeFileSearchIsSeparateAndCancellable() {
    QTemporaryDir directory;
    const auto path = directory.filePath("app.log");
    QByteArray bytes = "old needle outside ring\n";
    for (int i = 0; i < 100; ++i)
        bytes += "ordinary line\n";
    put(path, bytes);
    MainWindow window(nullptr, options(directory, 2));
    window.findChild<QCheckBox *>("followCheckBox")->setChecked(false);
    window.openPath(path);
    QTRY_COMPARE(model(window)->rowCount(), 2);
    QVERIFY(model(window)->recordAt(0)->line_number > 1);
    window.findChild<QLineEdit *>("wholeFileSearchEdit")->setText("needle");
    window.startWholeFileSearch();
    auto *results = window.findChild<QTreeWidget *>("wholeFileSearchResults");
    QTRY_COMPARE(results->topLevelItemCount(), 1);
    QCOMPARE(results->topLevelItem(0)->text(0), QStringLiteral("1"));
    QCOMPARE(model(window)->rowCount(), 2);
    results->setCurrentItem(results->topLevelItem(0));
    QVERIFY(window.findChild<QPlainTextEdit *>("wholeFileSearchEvidence")
                ->toPlainText()
                .contains("SHA-256"));
    QTRY_VERIFY(window.findChild<QThread *>("wholeFileSearchThread") == nullptr);
    put(path, QByteArray(2 * 1024 * 1024, 'x'));
    window.startWholeFileSearch();
    window.cancelWholeFileSearch();
    QTRY_VERIFY(window.findChild<QLabel *>("wholeFileSearchStatus")->text().contains("cancelled"));
}

void TestGuiExtensions::compactLayoutAndThemeKeepTextReadable() {
    QTemporaryDir directory;
    MainWindow window(nullptr, options(directory));
    window.resize(1000, 700);
    window.show();
    QTest::qWait(20);
    QVERIFY2(window.width() <= 1000, qPrintable(QString::number(window.width())));
    QVERIFY(!window.findChild<QWidget *>("sourceSettingsPanel")->isVisible());
    QVERIFY(window.statusBar()->height() <= 32);
    loglens::LogRecord record;
    record.level = loglens::Level::Info;
    record.message = "readable";
    model(window)->setRecords({record});
    QCOMPARE(model(window)
                 ->data(model(window)->index(0, LogModel::ColumnMessage), Qt::ForegroundRole)
                 .value<QBrush>()
                 .color(),
             QApplication::palette().color(QPalette::Text));
    const auto screenshot = qEnvironmentVariable("LOGLENS_GUI_SCREENSHOT");
    if (!screenshot.isEmpty())
        QVERIFY(window.grab().save(screenshot));
}

QTEST_MAIN(TestGuiExtensions)
#include "test_gui_extensions.moc"
