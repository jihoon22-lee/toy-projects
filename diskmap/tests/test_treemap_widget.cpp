#include <QImage>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QtTest>

#include <cstdint>
#include <memory>

#include "diskmap/fs_node.hpp"
#include "fake_fs.hpp"
#include "diskmap/gui/main_window.hpp"
#include "diskmap/gui/treemap_widget.hpp"

using diskmap::FsNode;
using diskmap::NodeIssue;
using diskmap::NodeKey;
using diskmap::ScanResult;
using diskmap::SizeMetric;
using diskmap::SortSpec;
using diskmap::ViewFilter;
using diskmap_test::makeDirNode;
using diskmap_test::makeFileNode;

namespace {

// The widget renders one level of children and its hit test returns the
// innermost tile, so the node under a point is a grandchild of the root. That
// grandchild has to be a directory here: nodeActivated ignores files, and a
// tree one level shallower puts a file at this point instead.
FsNode makeTree() {
    FsNode root =
        makeDirNode("root", {
                                makeDirNode("big", {makeDirNode("inner", {makeFileNode("a.bin", 900)})}),
                                makeFileNode("small.txt", 1),
                            });
    diskmap::aggregateSizes(root);
    return root;
}

std::shared_ptr<ScanResult> makeTreeDocument() {
    auto document = std::make_shared<ScanResult>();
    document->root = makeTree();
    return document;
}

FsNode makeMetricFile(const char* name, std::uint64_t logical, std::uint64_t allocated) {
    FsNode node = makeFileNode(name, logical);
    node.metadata.allocated_size = allocated;
    node.metadata.allocated_size_known = true;
    node.metadata.hard_link_count = 1;
    node.metadata.hard_link_count_known = true;
    return node;
}

std::shared_ptr<ScanResult> makeProjectionDocument() {
    auto document = std::make_shared<ScanResult>();
    document->root = makeDirNode(
        "root",
        {makeDirNode("keep-dir",
                     {makeDirNode("keep-inner",
                                  {makeMetricFile("keep-big", 10, 80),
                                   makeMetricFile("drop-deep", 1, 0)}),
                      makeMetricFile("drop-child", 1, 0)}),
         makeMetricFile("keep-file", 100, 20),
         makeDirNode("drop-dir", {makeMetricFile("keep-hidden", 500, 500)})});
    diskmap::aggregateSizes(document->root);
    diskmap::aggregateStorage(document->root);
    return document;
}

QImage renderWidget(QWidget& widget) {
    QImage image(widget.size(), QImage::Format_ARGB32);
    image.fill(Qt::magenta);
    widget.render(&image);
    return image;
}

bool hasPaintedPixel(const QImage& image) {
    const QColor background(28, 30, 34);
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixelColor(x, y) != background) {
                return true;
            }
        }
    }
    return false;
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
    void clearingTheProjectionLeavesNoCurrentNode();
    void clickInsideADirectoryTileActivatesIt();
    void rightClickInsideADirectoryTileDoesNotActivateIt();
    void movingOverATileReportsItsKey();
    void leavingTheWidgetClearsTheHover();
    void projectionOwnsDocumentsAcrossReplacement();
    void projectionFiltersAndUsesSelectedMetric();
    void projectionReportsUncertaintyAndRendersIt();
    void missingProjectionRootIsSafe();
    void accessibilityAndEmptyStateRenderAreStable();
};

// A Q_OBJECT widget links only when moc has run. Before ici 0.6.0 the gate could
// not build this test at all, which is why diskmap's widget had no unit tests.
void TestTreemapWidget::clearingTheProjectionLeavesNoCurrentNode() {
    const std::shared_ptr<ScanResult> document = makeTreeDocument();
    TreemapWidget widget;
    widget.resize(400, 300);
    widget.setProjection(document, diskmap::nodeKey(document->root), {}, {});
    QSignalSpy cleared(&widget, &TreemapWidget::hoverCleared);
    QSignalSpy status(&widget, &TreemapWidget::projectionStatusChanged);
    widget.clear();
    QCOMPARE(widget.currentNode(), nullptr);
    QCOMPARE(cleared.count(), 1);
    QCOMPARE(status.count(), 1);
    QVERIFY(status.at(0).at(0).toBool());
}

void TestTreemapWidget::clickInsideADirectoryTileActivatesIt() {
    const std::shared_ptr<ScanResult> document = makeTreeDocument();
    TreemapWidget widget;
    widget.resize(400, 300);
    widget.setProjection(document, diskmap::nodeKey(document->root), {}, {});

    QSignalSpy spy(&widget, &TreemapWidget::nodeActivated);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &press);

    // The hit test maps a point onto a squarified tile using the widget's own
    // size and paint-time layout. None of that is extractable into the Qt-free
    // core, which is why the check has to live in a Qt test.
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).value<NodeKey>() ==
            diskmap::nodeKey(document->root.children[0].children[0]));
}

void TestTreemapWidget::rightClickInsideADirectoryTileDoesNotActivateIt() {
    const std::shared_ptr<ScanResult> document = makeTreeDocument();
    TreemapWidget widget;
    widget.resize(400, 300);
    widget.setProjection(document, diskmap::nodeKey(document->root), {}, {});

    QSignalSpy spy(&widget, &TreemapWidget::nodeActivated);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &press);

    QCOMPARE(spy.count(), 0);
}

void TestTreemapWidget::movingOverATileReportsItsKey() {
    const std::shared_ptr<ScanResult> document = makeTreeDocument();
    TreemapWidget widget;
    widget.resize(400, 300);
    widget.setProjection(document, diskmap::nodeKey(document->root), {}, {});

    QSignalSpy spy(&widget, &TreemapWidget::nodeHovered);
    sendMouseMove(widget, QPoint(10, 10));

    // One signal per change, not per move: the status line would flicker
    // otherwise, and a repaint per mouse event is wasted work.
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).value<NodeKey>() ==
            diskmap::nodeKey(document->root.children[0].children[0]));
    sendMouseMove(widget, QPoint(12, 12));
    QCOMPARE(spy.count(), 1);
}

void TestTreemapWidget::leavingTheWidgetClearsTheHover() {
    const std::shared_ptr<ScanResult> document = makeTreeDocument();
    TreemapWidget widget;
    widget.resize(400, 300);
    widget.setProjection(document, diskmap::nodeKey(document->root), {}, {});

    sendMouseMove(widget, QPoint(10, 10));
    QSignalSpy spy(&widget, &TreemapWidget::hoverCleared);
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(&widget, &leave);

    // Leaving must clear the highlight; otherwise the last tile stays lit while
    // the pointer is somewhere else entirely.
    QCOMPARE(spy.count(), 1);
}

void TestTreemapWidget::projectionOwnsDocumentsAcrossReplacement() {
    TreemapWidget widget;
    widget.resize(400, 300);
    std::weak_ptr<const ScanResult> firstWeak;
    std::weak_ptr<const ScanResult> replacementWeak;
    {
        std::shared_ptr<ScanResult> first = makeProjectionDocument();
        firstWeak = first;
        const NodeKey firstKey = diskmap::nodeKey(first->root);
        widget.setProjection(first, firstKey, {}, SortSpec{SizeMetric::Logical, true});
        first.reset();

        QVERIFY(!firstWeak.expired());
        QVERIFY(widget.currentNode() != nullptr);
        QCOMPARE(widget.currentNode()->name, std::string("root"));
        QVERIFY(widget.projectionComplete());
        QVERIFY(hasPaintedPixel(renderWidget(widget)));

        std::shared_ptr<ScanResult> replacement = makeProjectionDocument();
        replacement->root.name = "replacement";
        replacementWeak = replacement;
        const NodeKey replacementKey = diskmap::nodeKey(replacement->root);
        widget.setProjection(replacement, replacementKey, {},
                              SortSpec{SizeMetric::Logical, true});
        replacement.reset();

        QVERIFY(firstWeak.expired());
        QVERIFY(!replacementWeak.expired());
        QVERIFY(widget.currentNode() != nullptr);
        QCOMPARE(widget.currentNode()->name, std::string("replacement"));
    }

    // Replacing a projection releases the old document while the active
    // projection remains owned until the widget is cleared or destroyed.
    QVERIFY(!replacementWeak.expired());
    widget.clear();
    QVERIFY(replacementWeak.expired());
    QVERIFY(widget.currentNode() == nullptr);
}

void TestTreemapWidget::projectionFiltersAndUsesSelectedMetric() {
    const std::shared_ptr<ScanResult> document = makeProjectionDocument();
    ViewFilter filter;
    filter.search = "keep";
    filter.metric = SizeMetric::Allocated;
    SortSpec sort{SizeMetric::Allocated, true};

    TreemapWidget widget;
    widget.resize(400, 300);
    QSignalSpy status(&widget, &TreemapWidget::projectionStatusChanged);
    widget.setProjection(document, diskmap::nodeKey(document->root), filter, sort);
    QCOMPARE(widget.currentNode(), &document->root);
    QVERIFY(widget.projectionComplete());
    QCOMPARE(status.count(), 1);
    QVERIFY(status.at(0).at(0).toBool());

    // Allocated bytes put keep-dir (80) before keep-file (20), despite the
    // opposite logical ordering. The filter is applied again below keep-dir,
    // leaving keep-inner as the innermost tile under the first point.
    QSignalSpy hover(&widget, &TreemapWidget::nodeHovered);
    sendMouseMove(widget, QPoint(10, 10));
    QCOMPARE(hover.count(), 1);
    QVERIFY(hover.at(0).at(0).value<NodeKey>() ==
            diskmap::nodeKey(document->root.children[0].children[0]));
    sendMouseMove(widget, QPoint(399, 10));
    QCOMPARE(hover.count(), 2);
    QVERIFY(hover.at(1).at(0).value<NodeKey>() ==
            diskmap::nodeKey(document->root.children[1]));

    QSignalSpy activated(&widget, &TreemapWidget::nodeActivated);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(10, 10), QPointF(10, 10),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&widget, &press);
    QCOMPARE(activated.count(), 1);
    QVERIFY(activated.at(0).at(0).value<NodeKey>() ==
            diskmap::nodeKey(document->root.children[0].children[0]));
}

void TestTreemapWidget::projectionReportsUncertaintyAndRendersIt() {
    const std::shared_ptr<ScanResult> complete = makeProjectionDocument();
    const std::shared_ptr<ScanResult> partial = makeProjectionDocument();
    FsNode& unknown = partial->root.children[1];
    unknown.complete = false;
    unknown.error = "permission denied";
    unknown.metadata.allocated_size_known = false;
    unknown.allocated_size_known = false;

    ViewFilter filter;
    filter.search = "keep";
    filter.metric = SizeMetric::Allocated;
    const SortSpec sort{SizeMetric::Allocated, true};
    TreemapWidget widget;
    widget.resize(400, 300);
    QSignalSpy status(&widget, &TreemapWidget::projectionStatusChanged);
    widget.setProjection(complete, diskmap::nodeKey(complete->root), filter, sort);
    const QImage exact = renderWidget(widget);
    widget.setProjection(partial, diskmap::nodeKey(partial->root), filter, sort);
    const QImage uncertain = renderWidget(widget);

    QVERIFY(widget.currentNode() == &partial->root);
    QVERIFY(!widget.projectionComplete());
    QCOMPARE(status.count(), 2);
    QVERIFY(status.at(0).at(0).toBool());
    QVERIFY(!status.at(1).at(0).toBool());
    QVERIFY(exact != uncertain);
    QVERIFY(hasPaintedPixel(uncertain));

    QSignalSpy hover(&widget, &TreemapWidget::nodeHovered);
    sendMouseMove(widget, QPoint(399, 10));
    QCOMPARE(hover.count(), 1);
    QVERIFY(hover.at(0).at(0).value<NodeKey>() == diskmap::nodeKey(unknown));
}

void TestTreemapWidget::missingProjectionRootIsSafe() {
    const std::shared_ptr<ScanResult> document = makeProjectionDocument();
    TreemapWidget widget;
    widget.resize(400, 300);
    QSignalSpy cleared(&widget, &TreemapWidget::hoverCleared);
    QSignalSpy status(&widget, &TreemapWidget::projectionStatusChanged);

    NodeKey missing;
    missing.normalized_path = "missing";
    widget.setProjection(document, missing, {}, {});
    QCOMPARE(widget.currentNode(), nullptr);
    QVERIFY(!widget.projectionComplete());
    QCOMPARE(cleared.count(), 1);
    QCOMPARE(status.count(), 1);
    QVERIFY(!status.at(0).at(0).toBool());
    QVERIFY(hasPaintedPixel(renderWidget(widget)));

    widget.clear();
    QCOMPARE(widget.currentNode(), nullptr);
    QVERIFY(widget.projectionComplete());
    QCOMPARE(cleared.count(), 2);
    QCOMPARE(status.count(), 2);
    QVERIFY(status.at(1).at(0).toBool());
}

void TestTreemapWidget::accessibilityAndEmptyStateRenderAreStable() {
    MainWindow window;
    TreemapWidget* widget = window.findChild<TreemapWidget*>(QStringLiteral("treemap"));
    QVERIFY(widget != nullptr);
    QCOMPARE(widget->objectName(), QStringLiteral("treemap"));
    QCOMPARE(widget->accessibleName(), QStringLiteral("Disk usage treemap"));
    QVERIFY(widget->accessibleDescription().contains(
        QStringLiteral("filesystem entries table")));
    QVERIFY(widget->hasMouseTracking());
    QVERIFY(widget->minimumSize().width() >= 320);
    QVERIFY(widget->minimumSize().height() >= 240);

    window.show();
    QCoreApplication::processEvents();
    widget->resize(400, 300);
    widget->clear();
    QVERIFY(hasPaintedPixel(renderWidget(*widget)));
}

QTEST_MAIN(TestTreemapWidget)
#include "test_treemap_widget.moc"
