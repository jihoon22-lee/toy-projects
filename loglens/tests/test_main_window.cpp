#include <QAbstractItemModel>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <QtTest>

#include <algorithm>

#include "loglens/gui/main_window.hpp"
#include "loglens/gui/log_model.hpp"
#include "loglens/gui/timeline_widget.hpp"
#include "loglens/filter_expr.hpp"
#include "loglens/log_stats.hpp"

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void openingAFileFillsTheTable();
    void growthIsObservedByTheFollowTimer();
    void truncationResetsStaleRows();
    void retryableSourceErrorKeepsFollowingAndVisibleRows();
    void sourceReplacementRecoversWithCleanRows();
    void disablingFollowWhileWaitingStopsPolling();
    void unsupportedSourceStopsFollowing();
    void failedOpenClearsThePreviousSourceState();
    void loaderSequenceMismatchStopsFollowing();
    void controlsHaveStableNamesAndFollowIsSwitchable();
    void timelineRendersEmptyAndPopulatedStates();
    void boundedStorageEvictsOldRowsAndReportsWindow();
    void drainsBacklogWithFollowDisabled();
    void searchAndFilterCanChangeDuringBackgroundLoading();
    void loadProgressReportsFinalFromStartState();
    void loadProgressReportsInitialOpenError();
};

namespace {

QTableView* table(MainWindow& window) {
    return window.findChild<QTableView*>(QStringLiteral("logTable"));
}

QAbstractItemModel* tableModel(MainWindow& window) {
    QTableView* view = table(window);
    return view == nullptr ? nullptr : view->model();
}

int rowCount(MainWindow& window) {
    QAbstractItemModel* model = tableModel(window);
    return model == nullptr ? -1 : model->rowCount(QModelIndex());
}

QString cell(MainWindow& window, int row, int column) {
    QAbstractItemModel* model = tableModel(window);
    if (model == nullptr) {
        return QString();
    }
    return model->data(model->index(row, column), Qt::DisplayRole).toString();
}

QCheckBox* followBox(MainWindow& window) {
    return window.findChild<QCheckBox*>(QStringLiteral("followCheckBox"));
}

QLabel* status(MainWindow& window) {
    return window.findChild<QLabel*>(QStringLiteral("statusLabel"));
}

QTimer* pollTimer(MainWindow& window) {
    return window.findChild<QTimer*>(QStringLiteral("followPollTimer"));
}

QByteArray line(const char* level, int number) {
    return QByteArray("2026-08-26T04:15:2") + QByteArray::number(number % 10)
           + QByteArray(".000Z ") + level + QByteArray("  [api] request ")
           + QByteArray::number(number) + '\n';
}

QByteArray lineWithMessage(const char* level, int number, const char* message) {
    return QByteArray("2026-08-26T04:15:2") + QByteArray::number(number % 10)
           + QByteArray(".000Z ") + level + QByteArray("  [api] ") + message + '\n';
}

void writeFile(const QString& path, const QByteArray& bytes, bool append = false) {
    QFile file(path);
    const QIODevice::OpenMode mode = QIODevice::WriteOnly
                                     | (append ? QIODevice::Append : QIODevice::Truncate);
    QVERIFY(file.open(mode));
    QCOMPARE(file.write(bytes), static_cast<qint64>(bytes.size()));
    QVERIFY(file.flush());
    file.close();
}

void recreateFile(const QString& path, const QByteArray& bytes) {
    const QString stagingPath = path + QStringLiteral(".staging");
    writeFile(stagingPath, bytes);
    QVERIFY(!QFile::exists(path));
    QVERIFY(QFile::rename(stagingPath, path));
}

void pollNow(MainWindow& window) {
    // pollSource is an application slot, so Qt's meta-object provides a
    // synchronous seam without widening MainWindow's public product API.
    QVERIFY(QMetaObject::invokeMethod(&window, "pollSource", Qt::DirectConnection));
}

QString statusText(int visible, int retained, int seen, int dropped, int oldest, int newest,
                   int capacity) {
    return QStringLiteral("%1 visible / %2 retained · %3 seen · %4 dropped · lines %5–%6 · cap %7")
        .arg(visible)
        .arg(retained)
        .arg(seen)
        .arg(dropped)
        .arg(oldest)
        .arg(newest)
        .arg(capacity);
}

} // namespace

void TestMainWindow::openingAFileFillsTheTable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    writeFile(path, line("INFO", 1) + line("WARN", 2));

    MainWindow window;
    window.openPath(path);

    QTRY_COMPARE(rowCount(window), 2);
    QTRY_COMPARE(cell(window, 0, LogModel::ColumnLine), QStringLiteral("1"));
    QTRY_COMPARE(cell(window, 1, LogModel::ColumnLine), QStringLiteral("2"));
    auto* statusLabel = status(window);
    auto* follow = followBox(window);
    auto* timer = pollTimer(window);
    QVERIFY(statusLabel != nullptr);
    QVERIFY(follow != nullptr);
    QVERIFY(timer != nullptr);
    QTRY_COMPARE(statusLabel->text(), statusText(2, 2, 2, 0, 1, 2, 8192));
    QVERIFY(follow->isChecked());
    QVERIFY(timer->isActive());
}

void TestMainWindow::growthIsObservedByTheFollowTimer() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    writeFile(path, line("INFO", 1));

    MainWindow window;
    window.openPath(path);
    QTRY_COMPARE(rowCount(window), 1);
    auto* timer = pollTimer(window);
    QVERIFY(timer != nullptr);
    QVERIFY(timer->isActive());

    writeFile(path, line("ERROR", 2), true);

    // The assertion waits on the real 500 ms timer, not a fixed sleep. This
    // exercises the same connection used by the application while remaining
    // tolerant of a busy CI event loop.
    QTRY_COMPARE_WITH_TIMEOUT(rowCount(window), 2, 2500);
    QTRY_COMPARE(cell(window, 1, LogModel::ColumnLine), QStringLiteral("2"));
    auto* statusLabel = status(window);
    QVERIFY(statusLabel != nullptr);
    QTRY_COMPARE(statusLabel->text(), statusText(2, 2, 2, 0, 1, 2, 8192));
}

void TestMainWindow::truncationResetsStaleRows() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    writeFile(path, line("INFO", 1) + line("INFO", 2));

    MainWindow window;
    window.openPath(path);
    QTRY_COMPARE(rowCount(window), 2);

    writeFile(path, line("WARN", 9));
    pollNow(window);

    QTRY_COMPARE(rowCount(window), 1);
    QTRY_COMPARE(cell(window, 0, LogModel::ColumnLine), QStringLiteral("1"));
    QTRY_COMPARE(cell(window, 0, LogModel::ColumnLevel), QStringLiteral("WARN"));
    auto* statusLabel = status(window);
    QVERIFY(statusLabel != nullptr);
    QTRY_COMPARE(statusLabel->text(), statusText(1, 1, 1, 0, 1, 1, 8192));
}

void TestMainWindow::retryableSourceErrorKeepsFollowingAndVisibleRows() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    writeFile(path, line("INFO", 1));

    MainWindow window;
    window.openPath(path);
    QTRY_COMPARE(rowCount(window), 1);
    auto* follow = followBox(window);
    auto* timer = pollTimer(window);
    auto* statusLabel = status(window);
    QVERIFY(follow != nullptr);
    QVERIFY(timer != nullptr);
    QVERIFY(statusLabel != nullptr);
    QVERIFY(follow->isChecked());
    QVERIFY(timer->isActive());

    QVERIFY(QFile::remove(path));
    pollNow(window);

    QTRY_VERIFY_WITH_TIMEOUT(statusLabel->text().contains(QStringLiteral("cannot stat")), 2500);

    // A missing pathname is a transient source failure. Follow remains armed
    // so a file restored by log rotation can be observed without reopening it.
    QVERIFY(follow->isChecked());
    QVERIFY(timer->isActive());
    const QString statusText = statusLabel->text().toLower();
    QVERIFY(statusText.contains(QStringLiteral("wait"))
            || statusText.contains(QStringLiteral("retr")));
    QVERIFY(statusLabel->text().contains(QStringLiteral("cannot stat")));
    // A transient read error does not invent a new empty source; the last
    // successfully read record remains visible for recovery.
    QTRY_COMPARE(rowCount(window), 1);
    QTRY_COMPARE(cell(window, 0, LogModel::ColumnLine), QStringLiteral("1"));
}

void TestMainWindow::sourceReplacementRecoversWithCleanRows() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    writeFile(path, line("INFO", 1) + line("INFO", 2));

    MainWindow window;
    window.openPath(path);
    QTRY_COMPARE(rowCount(window), 2);

    QVERIFY(QFile::remove(path));
    pollNow(window);
    QTRY_VERIFY_WITH_TIMEOUT(status(window)->text().contains(QStringLiteral("cannot stat")), 2500);
    QTRY_COMPARE(rowCount(window), 2);
    QVERIFY(followBox(window)->isChecked());

    // Atomic replacement gives the tailer a new identity even when the new
    // file happens to have the same size as the old one.
    const QByteArray replacement = line("WARN", 9);
    recreateFile(path, replacement);
    QFile restored(path);
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), replacement);
    pollNow(window);

    QTRY_COMPARE_WITH_TIMEOUT(rowCount(window), 1, 2500);
    QTRY_COMPARE(cell(window, 0, LogModel::ColumnLine), QStringLiteral("1"));
    QTRY_COMPARE(cell(window, 0, LogModel::ColumnLevel), QStringLiteral("WARN"));
    QTRY_COMPARE(status(window)->text(), statusText(1, 1, 1, 0, 1, 1, 8192));
    QVERIFY(followBox(window)->isChecked());
    QVERIFY(pollTimer(window)->isActive());
}

void TestMainWindow::disablingFollowWhileWaitingStopsPolling() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    writeFile(path, line("INFO", 1));

    MainWindow window;
    window.openPath(path);
    QTRY_COMPARE(rowCount(window), 1);

    QVERIFY(QFile::remove(path));
    pollNow(window);
    QTRY_VERIFY_WITH_TIMEOUT(status(window)->text().contains(QStringLiteral("cannot stat")), 2500);
    QVERIFY(followBox(window)->isChecked());
    QVERIFY(pollTimer(window)->isActive());

    followBox(window)->setChecked(false);
    QVERIFY(!followBox(window)->isChecked());
    QVERIFY(!pollTimer(window)->isActive());

    // Recreating the file alone must not change the view while Follow is
    // disabled. No timer sleep is needed: processEvents only services work
    // already queued and the stopped timer cannot enqueue a poll.
    recreateFile(path, line("ERROR", 7));
    QCoreApplication::processEvents();
    QCOMPARE(rowCount(window), 1);
    QCOMPARE(cell(window, 0, LogModel::ColumnLine), QStringLiteral("1"));

    // Explicitly resuming Follow makes the same pending replacement visible.
    followBox(window)->setChecked(true);
    QVERIFY(pollTimer(window)->isActive());
    pollNow(window);
    QTRY_COMPARE(rowCount(window), 1);
    QTRY_COMPARE(cell(window, 0, LogModel::ColumnLine), QStringLiteral("1"));
    QTRY_COMPARE(cell(window, 0, LogModel::ColumnLevel), QStringLiteral("ERROR"));
}

void TestMainWindow::unsupportedSourceStopsFollowing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    writeFile(path, line("INFO", 1));

    MainWindow window;
    window.openPath(path);
    QTRY_COMPARE(rowCount(window), 1);
    QVERIFY(QFile::remove(path));
    QDir parent(dir.path());
    QVERIFY(parent.mkdir(QStringLiteral("app.log")));

    pollNow(window);

    QTRY_VERIFY_WITH_TIMEOUT(status(window)->text().contains(QStringLiteral("cannot follow")), 2500);
    QVERIFY(!followBox(window)->isChecked());
    QVERIFY(!pollTimer(window)->isActive());
    QVERIFY(status(window)->text().contains(QStringLiteral("cannot follow")));
    // An unsupported replacement is not a reason to erase the last usable
    // view; the user can choose another file from the same window.
    QCOMPARE(rowCount(window), 1);
    QCOMPARE(cell(window, 0, LogModel::ColumnLine), QStringLiteral("1"));
}

void TestMainWindow::failedOpenClearsThePreviousSourceState() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString goodPath = dir.filePath(QStringLiteral("good.log"));
    const QString missingPath = dir.filePath(QStringLiteral("missing.log"));
    writeFile(goodPath, line("INFO", 1));

    MainWindow window;
    window.openPath(goodPath);
    QTRY_COMPARE(rowCount(window), 1);
    auto* follow = followBox(window);
    auto* timer = pollTimer(window);
    QVERIFY(follow != nullptr);
    QVERIFY(timer != nullptr);
    QVERIFY(timer->isActive());

    window.openPath(missingPath);

    QTRY_VERIFY_WITH_TIMEOUT(status(window)->text().startsWith(QStringLiteral("Cannot read:")), 2500);
    QCOMPARE(rowCount(window), 0);
    QVERIFY(!follow->isChecked());
    QVERIFY(!timer->isActive());
    QCOMPARE(window.windowTitle(), QStringLiteral("loglens"));
    auto* statusLabel = status(window);
    QVERIFY(statusLabel != nullptr);
    QVERIFY(!follow->isEnabled());
    QTRY_VERIFY(statusLabel->text().startsWith(QStringLiteral("Cannot read:")));
}

void TestMainWindow::loaderSequenceMismatchStopsFollowing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    writeFile(path, line("INFO", 1));

    MainWindow window;
    window.openPath(path);
    QTRY_COMPARE(rowCount(window), 1);
    QTRY_COMPARE(status(window)->text(), statusText(1, 1, 1, 0, 1, 1, 8192));

    QCheckBox* follow = followBox(window);
    QTimer* timer = pollTimer(window);
    QLabel* statusLabel = status(window);
    QVERIFY(follow != nullptr);
    QVERIFY(timer != nullptr);
    QVERIFY(statusLabel != nullptr);
    QVERIFY(follow->isChecked());
    QVERIFY(timer->isActive());

    // The first open is job 1 and its first accepted batch has sequence 0.
    // Injecting a later sequence directly exercises the GUI's protocol guard
    // without depending on timing or a second source request.
    loglens::LoadBatch malformed;
    malformed.job_id = 1;
    malformed.sequence = 99;
    QVERIFY(QMetaObject::invokeMethod(&window, "handleLoadBatch", Qt::DirectConnection,
                                      Q_ARG(loglens::LoadBatch, malformed)));

    QTRY_VERIFY_WITH_TIMEOUT(statusLabel->text().contains(QStringLiteral("sequence mismatch")),
                             2500);
    QVERIFY(!follow->isChecked());
    QVERIFY(!timer->isActive());
    QCOMPARE(rowCount(window), 1);
}

void TestMainWindow::controlsHaveStableNamesAndFollowIsSwitchable() {
    MainWindow window;
    auto* openButton = window.findChild<QPushButton*>(QStringLiteral("openButton"));
    auto* filterEdit = window.findChild<QLineEdit*>(QStringLiteral("filterEdit"));
    auto* applyButton = window.findChild<QPushButton*>(QStringLiteral("applyFilterButton"));
    auto* loadMode = window.findChild<QComboBox*>(QStringLiteral("loadModeComboBox"));
    auto* tailRecords = window.findChild<QSpinBox*>(QStringLiteral("tailRecordsSpinBox"));
    auto* follow = followBox(window);
    auto* logTable = table(window);
    auto* timeline = window.findChild<QWidget*>(QStringLiteral("timelineWidget"));
    auto* statusLabel = status(window);
    auto* timer = pollTimer(window);
    auto* loaderThread = window.findChild<QThread*>(QStringLiteral("logLoadThread"));
    QVERIFY(openButton != nullptr);
    QVERIFY(filterEdit != nullptr);
    QVERIFY(applyButton != nullptr);
    QVERIFY(loadMode != nullptr);
    QVERIFY(tailRecords != nullptr);
    QVERIFY(follow != nullptr);
    QVERIFY(logTable != nullptr);
    QVERIFY(timeline != nullptr);
    QVERIFY(statusLabel != nullptr);
    QVERIFY(timer != nullptr);
    QVERIFY(loaderThread != nullptr);
    QCOMPARE(openButton->accessibleName(),
             QStringLiteral("Open log file"));
    QCOMPARE(filterEdit->accessibleName(), QStringLiteral("Log filter"));
    QCOMPARE(applyButton->accessibleName(),
             QStringLiteral("Apply log filter"));
    QCOMPARE(loadMode->accessibleName(), QStringLiteral("Initial load mode"));
    QCOMPARE(loadMode->count(), 2);
    QCOMPARE(loadMode->currentIndex(), 0);
    QCOMPARE(loadMode->currentText(), QStringLiteral("Latest records"));
    QCOMPARE(tailRecords->accessibleName(), QStringLiteral("Latest record count"));
    QCOMPARE(tailRecords->minimum(), 1);
    QCOMPARE(tailRecords->maximum(), 8192);
    QCOMPARE(tailRecords->value(), 8192);
    QVERIFY(tailRecords->isEnabled());
    QCOMPARE(follow->accessibleName(), QStringLiteral("Follow log file"));
    QCOMPARE(logTable->accessibleName(), QStringLiteral("Log records"));
    QCOMPARE(timeline->accessibleName(), QStringLiteral("Log timeline"));
    QCOMPARE(statusLabel->accessibleName(), QStringLiteral("Log status"));
    QCOMPARE(timer->interval(), 500);
    QTRY_VERIFY_WITH_TIMEOUT(loaderThread->isRunning(), 2500);

    loadMode->setCurrentIndex(1);
    QCOMPARE(loadMode->currentText(), QStringLiteral("From start"));
    QVERIFY(!tailRecords->isEnabled());
    loadMode->setCurrentIndex(0);
    QVERIFY(tailRecords->isEnabled());

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    writeFile(path, line("INFO", 1));
    window.openPath(path);

    follow->setChecked(false);
    QVERIFY(!timer->isActive());
    follow->setChecked(true);
    QVERIFY(timer->isActive());
}

void TestMainWindow::timelineRendersEmptyAndPopulatedStates() {
    TimelineWidget timeline;
    timeline.resize(200, 80);
    timeline.show();
    QCoreApplication::processEvents();
    timeline.repaint();
    QCoreApplication::processEvents();

    QImage empty(timeline.size(), QImage::Format_ARGB32_Premultiplied);
    empty.fill(Qt::black);
    timeline.setBuckets({});
    timeline.render(&empty);
    QCOMPARE(empty.pixelColor(0, 0), QColor(24, 26, 30));

    loglens::Bucket bucket;
    bucket.level_counts[static_cast<std::size_t>(loglens::Level::Error)] = 1;
    QImage populated(timeline.size(), QImage::Format_ARGB32_Premultiplied);
    populated.fill(Qt::black);
    timeline.setBuckets({bucket});
    timeline.render(&populated);
    QCOMPARE(populated.pixelColor(100, 40), QColor("#e0645a"));
}

void TestMainWindow::boundedStorageEvictsOldRowsAndReportsWindow() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("bounded.log"));
    writeFile(path, line("INFO", 1) + line("WARN", 2) + line("ERROR", 3)
                        + line("FATAL", 4));

    MainWindow window(nullptr, 2);
    window.openPath(path, loglens::InitialLoadMode::FromStart, 2);

    QTRY_COMPARE(rowCount(window), 2);
    QTRY_COMPARE(cell(window, 0, LogModel::ColumnLine), QStringLiteral("3"));
    QTRY_COMPARE(cell(window, 1, LogModel::ColumnLine), QStringLiteral("4"));
    QTRY_COMPARE(status(window)->text(), statusText(2, 2, 4, 2, 3, 4, 2));

    QLineEdit* filter = window.findChild<QLineEdit*>(QStringLiteral("filterEdit"));
    QVERIFY(filter != nullptr);
    filter->setText(QStringLiteral("level >= error"));
    QVERIFY(QMetaObject::invokeMethod(&window, "applyFilter", Qt::DirectConnection));
    QCOMPARE(rowCount(window), 2);

    writeFile(path, line("INFO", 5), true);
    pollNow(window);

    // The filtered ERROR row was evicted, the new INFO row stays hidden, and
    // the retained FATAL row keeps its stable logical identity after wrap.
    QTRY_COMPARE(rowCount(window), 1);
    QTRY_COMPARE(cell(window, 0, LogModel::ColumnLine), QStringLiteral("4"));
    QTRY_COMPARE(status(window)->text(), statusText(1, 2, 5, 3, 4, 5, 2));
}

void TestMainWindow::drainsBacklogWithFollowDisabled() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("backlog.log"));
    constexpr int lineCount = 12;
    QByteArray contents;
    for (int number = 1; number <= lineCount; ++number) {
        contents += line("INFO", number);
    }
    writeFile(path, contents);

    MainWindow window(nullptr, 64, 17);
    QCheckBox* follow = followBox(window);
    QTimer* timer = pollTimer(window);
    QVERIFY(follow != nullptr);
    QVERIFY(timer != nullptr);
    follow->setChecked(false);

    window.openPath(path);

    // The initial read is intentionally too small for the complete file. The
    // queued zero-delay polls must drain the rest of the source even though
    // the user has disabled the normal follow timer.
    QTRY_COMPARE_WITH_TIMEOUT(rowCount(window), lineCount, 2500);
    QVERIFY(!follow->isChecked());
    QVERIFY(!timer->isActive());
    QCOMPARE(cell(window, 0, LogModel::ColumnLine), QStringLiteral("1"));
    QCOMPARE(cell(window, lineCount - 1, LogModel::ColumnLine),
             QString::number(lineCount));
}

void TestMainWindow::searchAndFilterCanChangeDuringBackgroundLoading() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("search.log"));
    QByteArray contents;
    contents += lineWithMessage("INFO", 1, "NEEDLE bootstrap");
    contents += lineWithMessage("ERROR", 2, "needle second");
    contents += lineWithMessage("WARN", 3, "NEEDLE warning");
    contents += lineWithMessage("FATAL", 4, "needle fatal");
    contents += lineWithMessage("ERROR", 5, "ordinary error");
    contents += lineWithMessage("ERROR", 6, "NEEDLE final");
    writeFile(path, contents);

    MainWindow window(nullptr, 64, 5);
    auto* model = qobject_cast<LogModel*>(tableModel(window));
    QVERIFY(model != nullptr);
    loglens::ParseError parseError;
    const auto parsed = loglens::Filter::parse("level >= error", parseError);
    QVERIFY(parsed.has_value());
    const loglens::Filter structuredFilter = *parsed;

    bool changedDuringLoad = false;
    QObject::connect(&window, &MainWindow::acknowledgeRequested, &window,
                     [&](quint64, quint64) {
                         if (changedDuringLoad) {
                             return;
                         }
                         changedDuringLoad = true;
                         model->setSearch(QStringLiteral("NeEdLe"));
                         model->setFilter(&structuredFilter);
                     });

    // The tiny source chunks force multiple worker batches. The first direct
    // acknowledgement changes both predicates while the worker is paused on
    // its bounded backpressure gate, before the remaining records arrive.
    window.openPath(path, loglens::InitialLoadMode::FromStart, 64);

    QTRY_VERIFY_WITH_TIMEOUT(changedDuringLoad, 2500);
    QTRY_COMPARE_WITH_TIMEOUT(rowCount(window), 3, 2500);
    QTRY_COMPARE(cell(window, 0, LogModel::ColumnLine), QStringLiteral("2"));
    QTRY_COMPARE(cell(window, 1, LogModel::ColumnLine), QStringLiteral("4"));
    QTRY_COMPARE(cell(window, 2, LogModel::ColumnLine), QStringLiteral("6"));
    QTRY_COMPARE(status(window)->text(), statusText(3, 6, 6, 0, 1, 6, 64));
}

void TestMainWindow::loadProgressReportsFinalFromStartState() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("progress.log"));
    const QByteArray contents = line("INFO", 1) + line("WARN", 2) + line("ERROR", 3);
    writeFile(path, contents);

    MainWindow window(nullptr, 64, 5);
    QSignalSpy progress(&window, &MainWindow::loadProgress);
    window.openPath(path, loglens::InitialLoadMode::FromStart, 64);

    // The source is intentionally split into tiny chunks. QTRY services the
    // real queued worker-to-GUI delivery without making the test depend on a
    // fixed sleep or on how many intermediate progress signals are emitted.
    QTRY_VERIFY_WITH_TIMEOUT(
        std::any_of(progress.cbegin(), progress.cend(), [](const QList<QVariant>& arguments) {
            return arguments.size() == 4 && arguments.at(2).toBool();
        }),
        2500);

    int finalIndex = -1;
    for (int index = 0; index < progress.count(); ++index) {
        const QList<QVariant> arguments = progress.at(index);
        if (arguments.size() == 4 && arguments.at(2).toBool()) {
            finalIndex = index;
        }
    }
    QVERIFY(finalIndex >= 0);
    const QList<QVariant> final = progress.at(finalIndex);
    QCOMPARE(final.at(0).toULongLong(), static_cast<qulonglong>(3));
    QCOMPARE(final.at(1).toULongLong(), static_cast<qulonglong>(3));
    QVERIFY(final.at(2).toBool());
    QVERIFY(final.at(3).toString().isEmpty());
}

void TestMainWindow::loadProgressReportsInitialOpenError() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString missing = dir.filePath(QStringLiteral("missing.log"));

    MainWindow window(nullptr, 64, 5);
    QSignalSpy progress(&window, &MainWindow::loadProgress);
    window.openPath(missing, loglens::InitialLoadMode::FromStart, 64);

    QTRY_VERIFY_WITH_TIMEOUT(
        std::any_of(progress.cbegin(), progress.cend(), [](const QList<QVariant>& arguments) {
            return arguments.size() == 4 && !arguments.at(3).toString().isEmpty();
        }),
        2500);

    int errorIndex = -1;
    for (int index = 0; index < progress.count(); ++index) {
        const QList<QVariant> arguments = progress.at(index);
        if (arguments.size() == 4 && !arguments.at(3).toString().isEmpty()) {
            errorIndex = index;
        }
    }
    QVERIFY(errorIndex >= 0);
    const QList<QVariant> error = progress.at(errorIndex);
    QCOMPARE(error.at(0).toULongLong(), static_cast<qulonglong>(0));
    QCOMPARE(error.at(1).toULongLong(), static_cast<qulonglong>(0));
    QVERIFY(!error.at(2).toBool());
    QVERIFY(error.at(3).toString().contains(QStringLiteral("cannot stat")));
}

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
