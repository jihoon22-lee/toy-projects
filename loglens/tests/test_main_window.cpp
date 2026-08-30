#include <QAbstractItemModel>
#include <QCheckBox>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include "loglens/gui/main_window.hpp"
#include "loglens/gui/log_model.hpp"
#include "loglens/gui/timeline_widget.hpp"
#include "loglens/log_stats.hpp"

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void openingAFileFillsTheTable();
    void growthIsObservedByTheFollowTimer();
    void truncationResetsStaleRows();
    void readErrorStopsFollowingAndExplainsWhy();
    void failedOpenClearsThePreviousSourceState();
    void controlsHaveStableNamesAndFollowIsSwitchable();
    void timelineRendersEmptyAndPopulatedStates();
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

void writeFile(const QString& path, const QByteArray& bytes, bool append = false) {
    QFile file(path);
    const QIODevice::OpenMode mode = QIODevice::WriteOnly
                                     | (append ? QIODevice::Append : QIODevice::Truncate);
    QVERIFY(file.open(mode));
    QCOMPARE(file.write(bytes), static_cast<qint64>(bytes.size()));
    QVERIFY(file.flush());
    file.close();
}

void pollNow(MainWindow& window) {
    // pollSource is an application slot, so Qt's meta-object provides a
    // synchronous seam without widening MainWindow's public product API.
    QVERIFY(QMetaObject::invokeMethod(&window, "pollSource", Qt::DirectConnection));
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
    QCOMPARE(cell(window, 0, LogModel::ColumnLine), QStringLiteral("1"));
    QCOMPARE(cell(window, 1, LogModel::ColumnLine), QStringLiteral("2"));
    auto* statusLabel = status(window);
    auto* follow = followBox(window);
    auto* timer = pollTimer(window);
    QVERIFY(statusLabel != nullptr);
    QVERIFY(follow != nullptr);
    QVERIFY(timer != nullptr);
    QCOMPARE(statusLabel->text(), QStringLiteral("2 / 2 line(s)"));
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
    QCOMPARE(rowCount(window), 1);
    auto* timer = pollTimer(window);
    QVERIFY(timer != nullptr);
    QVERIFY(timer->isActive());

    writeFile(path, line("ERROR", 2), true);

    // The assertion waits on the real 500 ms timer, not a fixed sleep. This
    // exercises the same connection used by the application while remaining
    // tolerant of a busy CI event loop.
    QTRY_COMPARE_WITH_TIMEOUT(rowCount(window), 2, 2500);
    QCOMPARE(cell(window, 1, LogModel::ColumnLine), QStringLiteral("2"));
    auto* statusLabel = status(window);
    QVERIFY(statusLabel != nullptr);
    QCOMPARE(statusLabel->text(), QStringLiteral("2 / 2 line(s)"));
}

void TestMainWindow::truncationResetsStaleRows() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    writeFile(path, line("INFO", 1) + line("INFO", 2));

    MainWindow window;
    window.openPath(path);
    QCOMPARE(rowCount(window), 2);

    writeFile(path, line("WARN", 9));
    pollNow(window);

    QTRY_COMPARE(rowCount(window), 1);
    QCOMPARE(cell(window, 0, LogModel::ColumnLine), QStringLiteral("1"));
    QCOMPARE(cell(window, 0, LogModel::ColumnLevel), QStringLiteral("WARN"));
    auto* statusLabel = status(window);
    QVERIFY(statusLabel != nullptr);
    QCOMPARE(statusLabel->text(), QStringLiteral("1 / 1 line(s)"));
}

void TestMainWindow::readErrorStopsFollowingAndExplainsWhy() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("app.log"));
    writeFile(path, line("INFO", 1));

    MainWindow window;
    window.openPath(path);
    auto* follow = followBox(window);
    auto* timer = pollTimer(window);
    QVERIFY(follow != nullptr);
    QVERIFY(timer != nullptr);
    QVERIFY(follow->isChecked());
    QVERIFY(timer->isActive());

    QVERIFY(QFile::remove(path));
    pollNow(window);

    QVERIFY(!follow->isChecked());
    QVERIFY(!timer->isActive());
    auto* statusLabel = status(window);
    QVERIFY(statusLabel != nullptr);
    QVERIFY(statusLabel->text().startsWith(QStringLiteral("Follow stopped:")));
    QVERIFY(statusLabel->text().contains(QStringLiteral("cannot stat")));
    // A transient read error stops polling but does not invent a new empty
    // source; the last successfully read record remains visible for recovery.
    QCOMPARE(rowCount(window), 1);
}

void TestMainWindow::failedOpenClearsThePreviousSourceState() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString goodPath = dir.filePath(QStringLiteral("good.log"));
    const QString missingPath = dir.filePath(QStringLiteral("missing.log"));
    writeFile(goodPath, line("INFO", 1));

    MainWindow window;
    window.openPath(goodPath);
    QCOMPARE(rowCount(window), 1);
    auto* follow = followBox(window);
    auto* timer = pollTimer(window);
    QVERIFY(follow != nullptr);
    QVERIFY(timer != nullptr);
    QVERIFY(timer->isActive());

    window.openPath(missingPath);

    QCOMPARE(rowCount(window), 0);
    QVERIFY(!follow->isChecked());
    QVERIFY(!timer->isActive());
    QCOMPARE(window.windowTitle(), QStringLiteral("loglens"));
    auto* statusLabel = status(window);
    QVERIFY(statusLabel != nullptr);
    QVERIFY(statusLabel->text().startsWith(QStringLiteral("Cannot read:")));
}

void TestMainWindow::controlsHaveStableNamesAndFollowIsSwitchable() {
    MainWindow window;
    auto* openButton = window.findChild<QPushButton*>(QStringLiteral("openButton"));
    auto* filterEdit = window.findChild<QLineEdit*>(QStringLiteral("filterEdit"));
    auto* applyButton = window.findChild<QPushButton*>(QStringLiteral("applyFilterButton"));
    auto* follow = followBox(window);
    auto* logTable = table(window);
    auto* timeline = window.findChild<QWidget*>(QStringLiteral("timelineWidget"));
    auto* statusLabel = status(window);
    auto* timer = pollTimer(window);
    QVERIFY(openButton != nullptr);
    QVERIFY(filterEdit != nullptr);
    QVERIFY(applyButton != nullptr);
    QVERIFY(follow != nullptr);
    QVERIFY(logTable != nullptr);
    QVERIFY(timeline != nullptr);
    QVERIFY(statusLabel != nullptr);
    QVERIFY(timer != nullptr);
    QCOMPARE(openButton->accessibleName(),
             QStringLiteral("Open log file"));
    QCOMPARE(filterEdit->accessibleName(), QStringLiteral("Log filter"));
    QCOMPARE(applyButton->accessibleName(),
             QStringLiteral("Apply log filter"));
    QCOMPARE(follow->accessibleName(), QStringLiteral("Follow log file"));
    QCOMPARE(logTable->accessibleName(), QStringLiteral("Log records"));
    QCOMPARE(timeline->accessibleName(), QStringLiteral("Log timeline"));
    QCOMPARE(statusLabel->accessibleName(), QStringLiteral("Log status"));
    QCOMPARE(timer->interval(), 500);

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

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
