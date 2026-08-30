#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QtTest>

#include "diskmap/gui/main_window.hpp"
#include "diskmap/gui/treemap_widget.hpp"

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void scanningDisplaysTheRootResult();
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
