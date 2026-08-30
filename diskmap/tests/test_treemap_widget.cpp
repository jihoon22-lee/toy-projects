#include <QMouseEvent>
#include <QSignalSpy>
#include <QtTest>

#include "diskmap/fs_node.hpp"
#include "fake_fs.hpp"
#include "diskmap/gui/treemap_widget.hpp"

using diskmap_test::makeDirNode;
using diskmap_test::makeFileNode;

namespace {

// The widget renders one level of children and its hit test returns the
// innermost tile, so the node under a point is a grandchild of the root. That
// grandchild has to be a directory here: nodeActivated ignores files, and a
// tree one level shallower puts a file at this point instead.
diskmap::FsNode makeTree() {
    diskmap::FsNode root =
        makeDirNode("root", {
                                makeDirNode("big", {makeDirNode("inner", {makeFileNode("a.bin", 900)})}),
                                makeFileNode("small.txt", 1),
                            });
    diskmap::aggregateSizes(root);
    return root;
}

void sendMouseMove(QWidget& widget, QPoint point) {
    // Sent directly rather than through QTest::mouseMove: the widget is never
    // shown, so there is no window for the test framework to deliver into.
    QMouseEvent move(QEvent::MouseMove, QPointF(point), QPointF(point), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &move);
}

} // namespace

class TestTreemapWidget : public QObject {
    Q_OBJECT

private slots:
    void clearingTheRootLeavesNoCurrentNode();
    void clickInsideADirectoryTileActivatesIt();
    void movingOverATileReportsTheHover();
    void leavingTheWidgetClearsTheHover();
};

// A Q_OBJECT widget links only when moc has run. Before ici 0.6.0 the gate could
// not build this test at all, which is why diskmap's widget had no unit tests.
void TestTreemapWidget::clearingTheRootLeavesNoCurrentNode() {
    TreemapWidget widget;
    widget.resize(400, 300);
    widget.setRoot(nullptr);
    QCOMPARE(widget.currentNode(), nullptr);
}

void TestTreemapWidget::clickInsideADirectoryTileActivatesIt() {
    diskmap::FsNode root = makeTree();
    TreemapWidget widget;
    widget.resize(400, 300);
    widget.setRoot(&root);

    QSignalSpy spy(&widget, &TreemapWidget::nodeActivated);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &press);

    // The hit test maps a point onto a squarified tile using the widget's own
    // size and paint-time layout. None of that is extractable into the Qt-free
    // core, which is why the check has to live in a Qt test.
    QCOMPARE(spy.count(), 1);
}

void TestTreemapWidget::movingOverATileReportsTheHover() {
    diskmap::FsNode root = makeTree();
    TreemapWidget widget;
    widget.resize(400, 300);
    widget.setRoot(&root);

    QSignalSpy spy(&widget, &TreemapWidget::hoveredNodeChanged);
    sendMouseMove(widget, QPoint(10, 10));

    // One signal per change, not per move: the status line would flicker
    // otherwise, and a repaint per mouse event is wasted work.
    QCOMPARE(spy.count(), 1);
    sendMouseMove(widget, QPoint(12, 12));
    QCOMPARE(spy.count(), 1);
}

void TestTreemapWidget::leavingTheWidgetClearsTheHover() {
    diskmap::FsNode root = makeTree();
    TreemapWidget widget;
    widget.resize(400, 300);
    widget.setRoot(&root);

    sendMouseMove(widget, QPoint(10, 10));
    QSignalSpy spy(&widget, &TreemapWidget::hoveredNodeChanged);
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(&widget, &leave);

    // Leaving must clear the highlight; otherwise the last tile stays lit while
    // the pointer is somewhere else entirely.
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<const diskmap::FsNode*>(), nullptr);
}

QTEST_MAIN(TestTreemapWidget)
#include "test_treemap_widget.moc"
