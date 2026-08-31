#include <QLabel>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QPushButton>
#include <QTemporaryDir>
#include <QWaitCondition>
#include <QtTest>

#include <filesystem>
#include <memory>
#include <utility>

#include "diskmap/gui/main_window.hpp"
#include "diskmap/gui/treemap_widget.hpp"

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void scanningDisplaysTheRootResult();
    void rescanCannotDisplayAnOlderGeneration();
    void rescanCancelsPreviousToken();
    void progressAppearsWhileRunnerIsBlocked();
    void cancelDiscardsPartialResultAndPreservesVisibleResult();
    void activatingADirectoryDescendsAndUpdatesBreadcrumb();
    void goingUpReturnsToTheRootAndStopsThere();
    void activatingALeafDoesNothing();
};

namespace {

QLabel* breadcrumb(MainWindow& window) {
    return window.findChild<QLabel*>(QStringLiteral("breadcrumb"));
}

QLabel* status(MainWindow& window) {
    return window.findChild<QLabel*>(QStringLiteral("status"));
}

QPushButton* cancelButton(MainWindow& window) {
    return window.findChild<QPushButton*>(QStringLiteral("cancelScanButton"));
}

QPushButton* upButton(MainWindow& window) {
    return window.findChild<QPushButton*>(QStringLiteral("upButton"));
}

TreemapWidget* treemap(MainWindow& window) {
    return window.findChild<TreemapWidget*>(QStringLiteral("treemap"));
}

bool makeTree(QTemporaryDir& directory) {
    QDir root(directory.path());
    if (!root.mkpath(QStringLiteral("big"))) {
        return false;
    }

    QFile payload(directory.filePath(QStringLiteral("big/payload.bin")));
    if (!payload.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray contents(4096, 'x');
    if (payload.write(contents) != contents.size()) {
        return false;
    }
    payload.close();
    return true;
}

diskmap::ScanResult fakeResult(const QString& path, const diskmap::ScanOptions& options) {
    diskmap::ScanResult result;
    result.generation = options.generation;
    result.dirs_scanned = 1;
    result.files_scanned = 1;
    result.root.name = path.toStdString();
    result.root.path = std::filesystem::path(path.toStdString());
    result.root.is_dir = true;
    result.root.complete = true;
    result.root.scan_generation = options.generation;

    diskmap::FsNode child;
    child.name = "entry-" + path.toStdString();
    child.path = result.root.path / child.name;
    child.size = 4096;
    child.scan_generation = options.generation;
    result.root.children.push_back(std::move(child));
    result.root.size = 4096;
    return result;
}

// The production runner is intentionally asynchronous, so these tests use a
// shared, thread-safe probe to hold a worker at an observable point. This
// makes ordering and cancellation assertions independent of scheduler timing.
struct ScanProbe {
    mutable QMutex mutex;
    QWaitCondition condition;
    bool oldStarted = false;
    bool oldReleased = false;
    bool oldFinished = false;
    bool progressStarted = false;
    bool progressReleased = false;
    bool progressFinished = false;
    bool partialStarted = false;
    bool partialReleased = false;
    bool partialFinished = false;
    std::shared_ptr<diskmap::ScanCancellationToken> oldToken;
    std::shared_ptr<diskmap::ScanCancellationToken> progressToken;
    std::shared_ptr<diskmap::ScanCancellationToken> partialToken;

    diskmap::ScanResult run(const QString& path,
                            const diskmap::ScanOptions& options,
                            const std::shared_ptr<diskmap::ScanCancellationToken>& token,
                            const diskmap::ProgressFn& progress) {
        diskmap::ScanResult result = fakeResult(path, options);
        if (path == QStringLiteral("A")) {
            {
                QMutexLocker locker(&mutex);
                oldToken = token;
                oldStarted = true;
                condition.wakeAll();
                while (!oldReleased) {
                    condition.wait(&mutex);
                }
                oldFinished = true;
                condition.wakeAll();
            }
        } else if (path == QStringLiteral("progress")) {
            {
                QMutexLocker locker(&mutex);
                progressToken = token;
                progressStarted = true;
                condition.wakeAll();
            }
            if (progress) {
                progress(3, 7);
            }
            QMutexLocker locker(&mutex);
            while (!progressReleased) {
                condition.wait(&mutex);
            }
            progressFinished = true;
            condition.wakeAll();
        } else if (path == QStringLiteral("partial")) {
            {
                QMutexLocker locker(&mutex);
                partialToken = token;
                partialStarted = true;
                condition.wakeAll();
            }
            QMutexLocker locker(&mutex);
            while (!partialReleased) {
                condition.wait(&mutex);
            }
            partialFinished = true;
            condition.wakeAll();
            result.root.complete = false;
            result.cancelled = token->isCancelled();
        }
        return result;
    }

    bool isOldStarted() const {
        QMutexLocker locker(&mutex);
        return oldStarted;
    }

    bool isOldFinished() const {
        QMutexLocker locker(&mutex);
        return oldFinished;
    }

    bool isOldCancelled() const {
        QMutexLocker locker(&mutex);
        return oldToken != nullptr && oldToken->isCancelled();
    }

    void releaseOld() {
        QMutexLocker locker(&mutex);
        oldReleased = true;
        condition.wakeAll();
    }

    bool isProgressStarted() const {
        QMutexLocker locker(&mutex);
        return progressStarted;
    }

    bool isProgressFinished() const {
        QMutexLocker locker(&mutex);
        return progressFinished;
    }

    void releaseProgress() {
        QMutexLocker locker(&mutex);
        progressReleased = true;
        condition.wakeAll();
    }

    bool isPartialStarted() const {
        QMutexLocker locker(&mutex);
        return partialStarted;
    }

    bool isPartialFinished() const {
        QMutexLocker locker(&mutex);
        return partialFinished;
    }

    bool isPartialCancelled() const {
        QMutexLocker locker(&mutex);
        return partialToken != nullptr && partialToken->isCancelled();
    }

    void releasePartial() {
        QMutexLocker locker(&mutex);
        partialReleased = true;
        condition.wakeAll();
    }
};

MainWindow::ScanRunner runnerFor(const std::shared_ptr<ScanProbe>& probe) {
    return [probe](const QString& path,
                   const diskmap::ScanOptions& options,
                   const std::shared_ptr<diskmap::ScanCancellationToken>& token,
                   const diskmap::ProgressFn& progress) {
        return probe->run(path, options, token, progress);
    };
}

void waitForScan(MainWindow& window) {
    // scanPath() deliberately uses the production QFutureWatcher path. This
    // waits for the observable result instead of sleeping for an arbitrary
    // worker duration, and therefore exercises the real scan-result handoff.
    QTRY_VERIFY(treemap(window) != nullptr);
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr);
}

} // namespace

void TestMainWindow::scanningDisplaysTheRootResult() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(makeTree(directory));

    MainWindow window;
    window.scanPath(directory.path());
    waitForScan(window);

    QVERIFY(breadcrumb(window) != nullptr);
    QVERIFY(!breadcrumb(window)->text().isEmpty());
    QVERIFY(status(window) != nullptr);
    QVERIFY(status(window)->text().contains(QStringLiteral("dirs")));
    QVERIFY(upButton(window) != nullptr);
    QVERIFY(!upButton(window)->isEnabled());
}

void TestMainWindow::rescanCannotDisplayAnOlderGeneration() {
    const auto probe = std::make_shared<ScanProbe>();
    MainWindow window(nullptr, runnerFor(probe));

    window.scanPath(QStringLiteral("A"));
    QTRY_VERIFY(probe->isOldStarted());

    window.scanPath(QStringLiteral("B"));
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr);
    QCOMPARE(QString::fromStdString(treemap(window)->currentNode()->name), QStringLiteral("B"));
    QCOMPARE(treemap(window)->currentNode()->scan_generation, std::uint64_t(2));

    // Starting B cancels A, but A is still allowed to finish later. Its
    // completion must not replace the already visible newer result.
    QTRY_VERIFY(probe->isOldCancelled());
    probe->releaseOld();
    QTRY_VERIFY(probe->isOldFinished());
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr);
    QCOMPARE(QString::fromStdString(treemap(window)->currentNode()->name), QStringLiteral("B"));
    QCOMPARE(treemap(window)->currentNode()->scan_generation, std::uint64_t(2));
    QVERIFY(!cancelButton(window)->isEnabled());
}

void TestMainWindow::rescanCancelsPreviousToken() {
    const auto probe = std::make_shared<ScanProbe>();
    MainWindow window(nullptr, runnerFor(probe));

    window.scanPath(QStringLiteral("A"));
    QTRY_VERIFY(probe->isOldStarted());
    QVERIFY(!probe->isOldCancelled());

    window.scanPath(QStringLiteral("B"));
    QTRY_VERIFY(probe->isOldCancelled());
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr);
    QCOMPARE(QString::fromStdString(treemap(window)->currentNode()->name), QStringLiteral("B"));

    probe->releaseOld();
    QTRY_VERIFY(probe->isOldFinished());
    QTRY_VERIFY(!cancelButton(window)->isEnabled());
}

void TestMainWindow::progressAppearsWhileRunnerIsBlocked() {
    const auto probe = std::make_shared<ScanProbe>();
    MainWindow window(nullptr, runnerFor(probe));
    QPushButton* cancel = cancelButton(window);
    QVERIFY(cancel != nullptr);
    QVERIFY(!cancel->isEnabled());

    window.scanPath(QStringLiteral("progress"));
    QTRY_VERIFY(probe->isProgressStarted());
    QVERIFY(cancel->isEnabled());
    QTRY_VERIFY(status(window)->text().contains(QStringLiteral("3 dirs, 7 files")));

    probe->releaseProgress();
    QTRY_VERIFY(probe->isProgressFinished());
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr);
    QTRY_VERIFY(!cancel->isEnabled());
    QVERIFY(status(window)->text().contains(QStringLiteral("1 dirs, 1 files")));
}

void TestMainWindow::cancelDiscardsPartialResultAndPreservesVisibleResult() {
    const auto probe = std::make_shared<ScanProbe>();
    MainWindow window(nullptr, runnerFor(probe));
    QPushButton* cancel = cancelButton(window);
    QVERIFY(cancel != nullptr);
    QVERIFY(!cancel->isEnabled());

    window.scanPath(QStringLiteral("prior"));
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr);
    const diskmap::FsNode* prior = treemap(window)->currentNode();
    QCOMPARE(QString::fromStdString(prior->name), QStringLiteral("prior"));
    QVERIFY(!cancel->isEnabled());

    window.scanPath(QStringLiteral("partial"));
    QTRY_VERIFY(probe->isPartialStarted());
    QVERIFY(cancel->isEnabled());
    QCOMPARE(treemap(window)->currentNode(), prior);

    cancel->click();
    QVERIFY(!cancel->isEnabled());
    QVERIFY(status(window)->text().contains(QStringLiteral("Cancelling scan")));
    QTRY_VERIFY(probe->isPartialCancelled());
    probe->releasePartial();

    QTRY_VERIFY(probe->isPartialFinished());
    QTRY_VERIFY(status(window)->text().contains(QStringLiteral("partial result discarded")));
    QTRY_VERIFY(!cancel->isEnabled());
    QCOMPARE(treemap(window)->currentNode(), prior);
    QCOMPARE(QString::fromStdString(treemap(window)->currentNode()->name), QStringLiteral("prior"));
}

void TestMainWindow::activatingADirectoryDescendsAndUpdatesBreadcrumb() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(makeTree(directory));

    MainWindow window;
    window.scanPath(directory.path());
    waitForScan(window);

    TreemapWidget* view = treemap(window);
    QVERIFY(view != nullptr);
    QVERIFY(!view->currentNode()->children.empty());
    const diskmap::FsNode* root = view->currentNode();
    const diskmap::FsNode* child = &root->children.front();
    QVERIFY(child->is_dir);
    const QString rootTrail = breadcrumb(window)->text();

    // nodeActivated is the widget's public navigation contract. Emitting it
    // keeps this test independent of squarified pixel geometry and widget
    // child order while still going through MainWindow's connected slot.
    emit view->nodeActivated(child);

    QVERIFY(view->currentNode() == child);
    QVERIFY(breadcrumb(window)->text() != rootTrail);
    QVERIFY(upButton(window)->isEnabled());
}

void TestMainWindow::goingUpReturnsToTheRootAndStopsThere() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(makeTree(directory));

    MainWindow window;
    window.scanPath(directory.path());
    waitForScan(window);

    TreemapWidget* view = treemap(window);
    QVERIFY(view != nullptr);
    const diskmap::FsNode* root = view->currentNode();
    QVERIFY(!root->children.empty());
    const QString rootTrail = breadcrumb(window)->text();
    emit view->nodeActivated(&root->children.front());
    QVERIFY(upButton(window)->isEnabled());

    // The button is found through its stable objectName and clicked through
    // its public API, rather than invoking MainWindow's private slot.
    upButton(window)->click();
    QCOMPARE(view->currentNode(), root);
    QCOMPARE(breadcrumb(window)->text(), rootTrail);
    QVERIFY(!upButton(window)->isEnabled());

    // A second click at the root is a safe no-op: the trail cannot pop past
    // the scan root and the view remains usable.
    upButton(window)->click();
    QCOMPARE(view->currentNode(), root);
    QCOMPARE(breadcrumb(window)->text(), rootTrail);
}

void TestMainWindow::activatingALeafDoesNothing() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(makeTree(directory));

    MainWindow window;
    window.scanPath(directory.path());
    waitForScan(window);

    TreemapWidget* view = treemap(window);
    QVERIFY(view != nullptr);
    const diskmap::FsNode* root = view->currentNode();
    QVERIFY(!root->children.empty());
    const diskmap::FsNode* directoryNode = &root->children.front();
    QVERIFY(directoryNode->is_dir);
    QVERIFY(!directoryNode->children.empty());
    const diskmap::FsNode* leaf = &directoryNode->children.front();
    QVERIFY(!leaf->is_dir);

    emit view->nodeActivated(directoryNode);
    const QString directoryTrail = breadcrumb(window)->text();
    emit view->nodeActivated(leaf);

    QCOMPARE(view->currentNode(), directoryNode);
    QCOMPARE(breadcrumb(window)->text(), directoryTrail);
}

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
