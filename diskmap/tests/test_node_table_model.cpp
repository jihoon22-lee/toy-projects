#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QStringList>
#include <QtTest>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "diskmap/fs_node.hpp"
#include "diskmap/gui/node_table_model.hpp"
#include "diskmap/view.hpp"

namespace {

using diskmap::FileIdentity;
using diskmap::FsKind;
using diskmap::FsNode;
using diskmap::NodeIssue;
using diskmap::NodeKey;
using diskmap::ScanResult;
using diskmap::SizeMetric;
using diskmap::SortSpec;
using diskmap::ViewFilter;

// The model owns the ScanResult through one shared_ptr, but it deliberately
// does not copy any FsNode. These small builders keep the ownership and
// metadata facts explicit in each fixture.
FsNode makeDirectory(std::string name,
                     std::filesystem::path path,
                     std::vector<FsNode> children = {}) {
    FsNode node;
    node.name = std::move(name);
    node.path = std::move(path);
    node.is_dir = true;
    node.metadata.kind = FsKind::Directory;
    node.metadata.complete = true;
    node.complete = true;
    node.children = std::move(children);
    return node;
}

FsNode makeFile(std::string name,
                std::filesystem::path path,
                std::uint64_t logical,
                std::uint64_t allocated,
                std::uint64_t reclaimable,
                std::int64_t modified,
                FileIdentity identity = {}) {
    FsNode node;
    node.name = std::move(name);
    node.path = std::move(path);
    node.is_dir = false;
    node.size = logical;
    node.metadata.kind = FsKind::RegularFile;
    node.metadata.logical_size = logical;
    node.metadata.allocated_size = allocated;
    node.metadata.allocated_size_known = true;
    node.metadata.hard_link_count = 1;
    node.metadata.hard_link_count_known = true;
    node.metadata.modified_ns = modified;
    node.metadata.modified_time_known = true;
    node.metadata.complete = true;
    node.complete = true;
    node.allocated_size = allocated;
    node.allocated_size_known = true;
    node.reclaimable_size = reclaimable;
    node.reclaimable_size_known = true;
    node.metadata.identity = identity;
    return node;
}

FsNode makeSymlink(std::string name,
                   std::filesystem::path path,
                   std::uint64_t logical,
                   FileIdentity identity) {
    FsNode node = makeFile(std::move(name), std::move(path), logical, logical, logical,
                           0, identity);
    node.metadata.kind = FsKind::Symlink;
    node.followed = true;
    node.has_target_metadata = true;
    node.target_metadata.kind = FsKind::RegularFile;
    node.target_metadata.logical_size = logical;
    node.target_metadata.allocated_size = logical;
    node.target_metadata.allocated_size_known = true;
    node.target_metadata.hard_link_count = 1;
    node.target_metadata.hard_link_count_known = true;
    node.target_metadata.modified_time_known = true;
    node.target_metadata.complete = true;
    return node;
}

std::shared_ptr<ScanResult> makeChildrenDocument() {
    auto document = std::make_shared<ScanResult>();
    document->generation = 42;

    FsNode unknown = makeFile("unknown.bin", "/scan/root/unknown.bin", 99, 0, 0, 103);
    unknown.logical_size_known = false;
    unknown.metadata.allocated_size_known = false;
    unknown.allocated_size_known = false;
    unknown.reclaimable_size_known = false;

    FsNode incomplete = makeFile("incomplete.bin", "/scan/root/incomplete.bin", 88, 0, 0, 104);
    incomplete.complete = false;
    incomplete.error = "permission denied";

    FsNode nested = makeDirectory(
        "subdir", "/scan/root/subdir", {makeFile("nested.bin", "/scan/root/subdir/nested.bin",
                                                    15, 7, 7, 102)});
    document->root = makeDirectory(
        "root", "/scan/root",
        {makeFile("beta.bin", "/scan/root/beta.bin", 20, 30, 30, 11,
                  FileIdentity{7, 2, true}),
         makeFile("alpha.txt", "/scan/root/./alpha.txt", 20, 10, 10, 101,
                  FileIdentity{7, 1, true}),
         makeFile("small.txt", "/scan/root/small.txt", 5, 5, 5, 12),
         std::move(nested),
         std::move(unknown),
         std::move(incomplete)});
    document->root.scan_generation = document->generation;
    diskmap::aggregateSizes(document->root);
    diskmap::aggregateStorage(document->root);
    return document;
}

std::shared_ptr<ScanResult> makeLargestDocument(bool incompleteBranch) {
    auto document = std::make_shared<ScanResult>();
    document->generation = 77;
    FsNode subtree = makeDirectory(
        "sub", "/scan/largest/sub",
        {makeFile("huge.bin", "/scan/largest/sub/huge.bin", 100, 100, 100, 1),
         makeFile("mid.bin", "/scan/largest/sub/mid.bin", 60, 60, 60, 2),
         // A followed symlink is still a Symlink entry and must not appear in
         // the regular-file largest-files projection.
         makeSymlink("alias.bin", "/scan/largest/sub/alias.bin", 120,
                     FileIdentity{8, 88, true})});
    std::vector<FsNode> children{
        std::move(subtree),
        makeFile("root.bin", "/scan/largest/root.bin", 80, 80, 80, 3),
        makeFile("small.bin", "/scan/largest/small.bin", 10, 10, 10, 4),
    };
    if (incompleteBranch) {
        FsNode blocked = makeDirectory(
            "blocked", "/scan/largest/blocked",
            {makeFile("hidden.bin", "/scan/largest/blocked/hidden.bin", 1000, 1000, 1000,
                      5)});
        blocked.complete = false;
        blocked.error = "cannot open directory";
        children.push_back(std::move(blocked));
    }
    document->root = makeDirectory("largest", "/scan/largest", std::move(children));
    document->root.scan_generation = document->generation;
    diskmap::aggregateSizes(document->root);
    diskmap::aggregateStorage(document->root);
    return document;
}

const FsNode* nodeNamed(const NodeTableModel& model, const char* name) {
    for (int row = 0; row < model.rowCount(); ++row) {
        const FsNode* node = model.nodeAt(row);
        if (node != nullptr && node->name == name) {
            return node;
        }
    }
    return nullptr;
}

QModelIndex indexNamed(const NodeTableModel& model, const char* name) {
    const FsNode* node = nodeNamed(model, name);
    return node == nullptr ? QModelIndex() : model.indexForKey(diskmap::nodeKey(*node));
}

QStringList rowNames(const NodeTableModel& model) {
    QStringList names;
    for (int row = 0; row < model.rowCount(); ++row) {
        const FsNode* node = model.nodeAt(row);
        names << (node == nullptr ? QString() : QString::fromStdString(node->name));
    }
    return names;
}

} // namespace

class TestNodeTableModel : public QObject {
    Q_OBJECT

private slots:
    void modelContractAndRoles();
    void childrenProjectionIsDeterministicAndKeyed();
    void metricAndMetadataRolesExposeKnownness();
    void unknownAndIncompleteRowsRenderStatus();
    void largestFilesHonourLimitAndCompleteness();
    void sharedScanResultKeepsRowsAlive();
    void clearAndMissingRootAreSafe();
    void filteringAndReprojectionReplaceRows();
    void sortReprojectsMetricOrder();
};

void TestNodeTableModel::modelContractAndRoles() {
    const std::shared_ptr<ScanResult> document = makeChildrenDocument();
    NodeTableModel model;
    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    const NodeKey rootKey = diskmap::nodeKey(document->root);
    model.setProjection(document, rootKey, {}, SortSpec{SizeMetric::Logical, true});

    QCOMPARE(model.rowCount(), 6);
    QCOMPARE(model.columnCount(), static_cast<int>(NodeTableModel::ColumnCount));
    const QModelIndex cell = model.index(0, 0);
    QVERIFY(cell.isValid());
    QCOMPARE(model.rowCount(cell), 0);
    QCOMPARE(model.columnCount(cell), 0);
    QVERIFY(!model.data(QModelIndex(), Qt::DisplayRole).isValid());
    QVERIFY(!model.data(model.index(0, NodeTableModel::ColumnCount), Qt::DisplayRole)
                 .isValid());

    const QStringList headers{QStringLiteral("Name"),
                              QStringLiteral("Path"),
                              QStringLiteral("Type"),
                              QStringLiteral("Logical"),
                              QStringLiteral("Allocated"),
                              QStringLiteral("Reclaimable"),
                              QStringLiteral("Modified"),
                              QStringLiteral("State")};
    for (int column = 0; column < NodeTableModel::ColumnCount; ++column) {
        QCOMPARE(model.headerData(column, Qt::Horizontal, Qt::DisplayRole),
                 QVariant(headers.at(column)));
    }
    QVERIFY(!model.headerData(0, Qt::Vertical, Qt::DisplayRole).isValid());
    QVERIFY(!model.headerData(-1, Qt::Horizontal, Qt::DisplayRole).isValid());
    QVERIFY(!model.headerData(NodeTableModel::ColumnCount, Qt::Horizontal,
                              Qt::DisplayRole)
                 .isValid());

    const QHash<int, QByteArray> roles = model.roleNames();
    const QList<QPair<int, QByteArray>> expectedRoles{
        {NodeTableModel::NodeKeyRole, QByteArrayLiteral("nodeKey")},
        {NodeTableModel::NodeKindRole, QByteArrayLiteral("nodeKind")},
        {NodeTableModel::NodeIssueRole, QByteArrayLiteral("nodeIssue")},
        {NodeTableModel::NodeErrorRole, QByteArrayLiteral("nodeError")},
        {NodeTableModel::CompleteRole, QByteArrayLiteral("complete")},
        {NodeTableModel::FollowedRole, QByteArrayLiteral("followed")},
        {NodeTableModel::IdentityRole, QByteArrayLiteral("identity")},
        {NodeTableModel::LogicalBytesRole, QByteArrayLiteral("logicalBytes")},
        {NodeTableModel::LogicalKnownRole, QByteArrayLiteral("logicalKnown")},
        {NodeTableModel::LogicalAdditiveRole, QByteArrayLiteral("logicalAdditive")},
        {NodeTableModel::AllocatedBytesRole, QByteArrayLiteral("allocatedBytes")},
        {NodeTableModel::AllocatedKnownRole, QByteArrayLiteral("allocatedKnown")},
        {NodeTableModel::AllocatedAdditiveRole, QByteArrayLiteral("allocatedAdditive")},
        {NodeTableModel::ReclaimableBytesRole, QByteArrayLiteral("reclaimableBytes")},
        {NodeTableModel::ReclaimableKnownRole, QByteArrayLiteral("reclaimableKnown")},
        {NodeTableModel::ReclaimableAdditiveRole,
         QByteArrayLiteral("reclaimableAdditive")},
        {NodeTableModel::ModifiedNsRole, QByteArrayLiteral("modifiedNs")},
        {NodeTableModel::ModifiedKnownRole, QByteArrayLiteral("modifiedKnown")},
    };
    QVERIFY(roles.size() >= expectedRoles.size());
    for (const auto& role : expectedRoles) {
        QCOMPARE(roles.value(role.first), role.second);
    }
}

void TestNodeTableModel::childrenProjectionIsDeterministicAndKeyed() {
    const std::shared_ptr<ScanResult> document = makeChildrenDocument();
    NodeTableModel model;
    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    model.setProjection(document, diskmap::nodeKey(document->root), {},
                        SortSpec{SizeMetric::Logical, true});
    QCOMPARE((rowNames(model)),
             (QStringList{QStringLiteral("alpha.txt"), QStringLiteral("beta.bin"),
                          QStringLiteral("subdir"), QStringLiteral("small.txt"),
                          QStringLiteral("incomplete.bin"), QStringLiteral("unknown.bin")}));
    QCOMPARE(model.mode(), NodeTableModel::ChildrenMode);
    QCOMPARE(model.rootNode(), &document->root);
    QVERIFY(!model.projectionComplete());
    QCOMPARE(model.projectionIssue(), NodeIssue::Incomplete);

    for (int row = 0; row < model.rowCount(); ++row) {
        const FsNode* node = model.nodeAt(row);
        QVERIFY(node != nullptr);
        const std::optional<NodeKey> key = model.keyAt(row);
        QVERIFY(key.has_value());
        if (node == nullptr || !key.has_value()) {
            continue;
        }
        QVERIFY(*key == diskmap::nodeKey(*node));
        const QModelIndex index = model.index(row, NodeTableModel::NameColumn);
        QVERIFY(index.isValid());
        QVERIFY(model.indexForKey(*key) == index);
        const QVariant keyValue = model.data(index, NodeTableModel::NodeKeyRole);
        QVERIFY(keyValue.canConvert<NodeKey>());
        QVERIFY(keyValue.value<NodeKey>() == *key);
    }
    QVERIFY(model.nodeAt(-1) == nullptr);
    QVERIFY(!model.keyAt(-1).has_value());
    QVERIFY(!model.indexForKey(NodeKey{"/scan/root/missing", FsKind::RegularFile, false,
                                       std::nullopt})
                  .isValid());
}

void TestNodeTableModel::metricAndMetadataRolesExposeKnownness() {
    const std::shared_ptr<ScanResult> document = makeChildrenDocument();
    NodeTableModel model;
    model.setProjection(document, diskmap::nodeKey(document->root), {},
                        SortSpec{SizeMetric::Logical, true});
    const QModelIndex alpha = indexNamed(model, "alpha.txt");
    QVERIFY(alpha.isValid());

    QCOMPARE(model.data(alpha, NodeTableModel::LogicalBytesRole).toULongLong(),
             static_cast<qulonglong>(20));
    QCOMPARE(model.data(alpha, NodeTableModel::LogicalKnownRole).toBool(), true);
    QCOMPARE(model.data(alpha, NodeTableModel::LogicalAdditiveRole).toBool(), true);
    QCOMPARE(model.data(alpha, NodeTableModel::AllocatedBytesRole).toULongLong(),
             static_cast<qulonglong>(10));
    QCOMPARE(model.data(alpha, NodeTableModel::AllocatedKnownRole).toBool(), true);
    QCOMPARE(model.data(alpha, NodeTableModel::AllocatedAdditiveRole).toBool(), false);
    QCOMPARE(model.data(alpha, NodeTableModel::ReclaimableBytesRole).toULongLong(),
             static_cast<qulonglong>(10));
    QCOMPARE(model.data(alpha, NodeTableModel::ReclaimableKnownRole).toBool(), true);
    QCOMPARE(model.data(alpha, NodeTableModel::ReclaimableAdditiveRole).toBool(), false);
    QCOMPARE(model.data(alpha, NodeTableModel::ModifiedNsRole).toLongLong(),
             static_cast<qlonglong>(101));
    QCOMPARE(model.data(alpha, NodeTableModel::ModifiedKnownRole).toBool(), true);
    QCOMPARE(model.data(alpha, NodeTableModel::CompleteRole).toBool(), true);
    QCOMPARE(model.data(alpha, NodeTableModel::FollowedRole).toBool(), false);
    QCOMPARE(model.data(alpha, NodeTableModel::NodeKindRole).toInt(),
             static_cast<int>(FsKind::RegularFile));
    QCOMPARE(model.data(alpha, NodeTableModel::NodeIssueRole).toInt(),
             static_cast<int>(NodeIssue::None));

    // Display values use the normalized path and a human-readable byte value,
    // while the custom roles retain machine-readable exact values.
    QCOMPARE(model.data(alpha, Qt::DisplayRole).toString(), QStringLiteral("alpha.txt"));
    QCOMPARE(model.data(model.index(alpha.row(), NodeTableModel::PathColumn),
                        Qt::DisplayRole)
                 .toString(),
             QStringLiteral("/scan/root/alpha.txt"));
    QCOMPARE(model.data(model.index(alpha.row(), NodeTableModel::LogicalColumn),
                        Qt::DisplayRole)
                 .toString(),
             QStringLiteral("20 B"));
}

void TestNodeTableModel::unknownAndIncompleteRowsRenderStatus() {
    const std::shared_ptr<ScanResult> document = makeChildrenDocument();
    NodeTableModel model;
    model.setProjection(document, diskmap::nodeKey(document->root), {},
                        SortSpec{SizeMetric::Logical, true});

    const QModelIndex unknown = indexNamed(model, "unknown.bin");
    QVERIFY(unknown.isValid());
    QCOMPARE(model.data(unknown, NodeTableModel::LogicalKnownRole).toBool(), false);
    QCOMPARE(model.data(unknown, NodeTableModel::AllocatedKnownRole).toBool(), false);
    QCOMPARE(model.data(unknown, NodeTableModel::NodeIssueRole).toInt(),
             static_cast<int>(NodeIssue::None));
    const QString unknownLogical =
        model.data(model.index(unknown.row(), NodeTableModel::LogicalColumn),
                   Qt::DisplayRole)
            .toString();
    // A numeric value with known=false must not be presented as an exact byte
    // count. The model may choose words or a compact marker for uncertainty.
    QVERIFY(unknownLogical != QStringLiteral("99 B"));
    QVERIFY(unknownLogical.contains(QStringLiteral("At least"))
            || unknownLogical.contains(QStringLiteral("Unknown")));

    const QModelIndex incomplete = indexNamed(model, "incomplete.bin");
    QVERIFY(incomplete.isValid());
    QCOMPARE(model.data(incomplete, NodeTableModel::CompleteRole).toBool(), false);
    QCOMPARE(model.data(incomplete, NodeTableModel::NodeIssueRole).toInt(),
             static_cast<int>(NodeIssue::Incomplete));
    QCOMPARE(model.data(incomplete, NodeTableModel::NodeErrorRole).toString(),
             QStringLiteral("permission denied"));
    const QString incompleteState =
        model.data(model.index(incomplete.row(), NodeTableModel::StateColumn),
                   Qt::DisplayRole)
            .toString()
            .toLower();
    QVERIFY(incompleteState.contains(QStringLiteral("incomplete")));

    auto incompleteRoot = makeChildrenDocument();
    incompleteRoot->root.complete = false;
    incompleteRoot->root.error = "root listing failed";
    QSignalSpy status(&model, &NodeTableModel::projectionStatusChanged);
    model.setProjection(incompleteRoot, diskmap::nodeKey(incompleteRoot->root), {},
                        SortSpec{SizeMetric::Logical, true});
    QCOMPARE(model.projectionComplete(), false);
    QCOMPARE(model.projectionIssue(), NodeIssue::Incomplete);
    QCOMPARE(status.count(), 1);
    QCOMPARE(status.at(0).at(0).toBool(), false);
    QCOMPARE(status.at(0).at(1).toInt(), static_cast<int>(NodeIssue::Incomplete));
}

void TestNodeTableModel::largestFilesHonourLimitAndCompleteness() {
    const std::shared_ptr<ScanResult> complete = makeLargestDocument(false);
    NodeTableModel model;
    QAbstractItemModelTester tester(
        &model, QAbstractItemModelTester::FailureReportingMode::Fatal);
    model.setLargestLimit(2);
    model.setProjection(complete, diskmap::nodeKey(complete->root), {},
                        SortSpec{SizeMetric::Logical, true}, NodeTableModel::LargestFilesMode);

    QCOMPARE(model.mode(), NodeTableModel::LargestFilesMode);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE((rowNames(model)),
             (QStringList{QStringLiteral("huge.bin"), QStringLiteral("root.bin")}));
    QVERIFY(model.projectionComplete());
    QVERIFY(nodeNamed(model, "alias.bin") == nullptr);

    model.setLargestLimit(3);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE((rowNames(model)), (QStringList{QStringLiteral("huge.bin"),
                                             QStringLiteral("root.bin"),
                                             QStringLiteral("mid.bin")}));

    const std::shared_ptr<ScanResult> partial = makeLargestDocument(true);
    model.setLargestLimit(2);
    model.setProjection(partial, diskmap::nodeKey(partial->root), {},
                        SortSpec{SizeMetric::Logical, true}, NodeTableModel::LargestFilesMode);
    QCOMPARE((rowNames(model)),
             (QStringList{QStringLiteral("huge.bin"), QStringLiteral("root.bin")}));
    QCOMPARE(model.projectionComplete(), false);
    QVERIFY(nodeNamed(model, "hidden.bin") == nullptr);
}

void TestNodeTableModel::sharedScanResultKeepsRowsAlive() {
    NodeTableModel model;
    std::weak_ptr<const ScanResult> weak;
    NodeKey rootKey;
    {
        std::shared_ptr<ScanResult> document = makeChildrenDocument();
        rootKey = diskmap::nodeKey(document->root);
        weak = document;
        model.setProjection(document, rootKey, {}, SortSpec{SizeMetric::Logical, true});
        document.reset();
        QVERIFY(!weak.expired());
        QVERIFY(model.rootNode() != nullptr);
        QCOMPARE(model.rootNode()->name, std::string("root"));
        QCOMPARE(model.rowCount(), 6);
    }

    // The external owner is gone, but the model's shared ownership keeps all
    // borrowed row pointers valid until the next reset.
    QVERIFY(!weak.expired());
    QCOMPARE(model.nodeAt(0)->name, std::string("alpha.txt"));
    model.clear();
    QVERIFY(weak.expired());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.rootNode() == nullptr);
}

void TestNodeTableModel::clearAndMissingRootAreSafe() {
    const std::shared_ptr<ScanResult> document = makeChildrenDocument();
    NodeTableModel model;
    model.setProjection(document, diskmap::nodeKey(document->root), {},
                        SortSpec{SizeMetric::Logical, true});
    model.clear();
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), static_cast<int>(NodeTableModel::ColumnCount));
    QVERIFY(model.rootNode() == nullptr);
    QVERIFY(model.projectionComplete());
    QCOMPARE(model.projectionIssue(), NodeIssue::None);

    NodeKey missing;
    missing.normalized_path = "/scan/root/does-not-exist";
    missing.kind = FsKind::Directory;
    QSignalSpy status(&model, &NodeTableModel::projectionStatusChanged);
    model.setProjection(document, missing, {}, SortSpec{SizeMetric::Logical, true});
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.rootNode() == nullptr);
    QCOMPARE(model.projectionComplete(), false);
    QCOMPARE(model.projectionIssue(), NodeIssue::Error);
    QCOMPARE(status.count(), 1);
    QCOMPARE(status.at(0).at(0).toBool(), false);
    QCOMPARE(status.at(0).at(1).toInt(), static_cast<int>(NodeIssue::Error));
}

void TestNodeTableModel::filteringAndReprojectionReplaceRows() {
    const std::shared_ptr<ScanResult> document = makeChildrenDocument();
    NodeTableModel model;
    const NodeKey rootKey = diskmap::nodeKey(document->root);
    const SortSpec logicalDescending{SizeMetric::Logical, true};

    ViewFilter search;
    search.search = "ALPHA";
    model.setProjection(document, rootKey, search, logicalDescending);
    QCOMPARE((rowNames(model)), (QStringList{QStringLiteral("alpha.txt")}));

    ViewFilter physical;
    physical.metric = SizeMetric::Allocated;
    physical.min_size = 9;
    model.setProjection(document, rootKey, physical,
                        SortSpec{SizeMetric::Allocated, true});
    QCOMPARE((rowNames(model)), (QStringList{QStringLiteral("beta.bin"),
                                             QStringLiteral("alpha.txt")}));

    const FsNode* subdir = document->root.children.at(3).is_dir
                               ? &document->root.children.at(3)
                               : nullptr;
    QVERIFY(subdir != nullptr);
    if (subdir != nullptr) {
        model.setProjection(document, diskmap::nodeKey(*subdir), {}, logicalDescending);
        QCOMPARE((rowNames(model)), (QStringList{QStringLiteral("nested.bin")}));
        QCOMPARE(model.rootNode(), subdir);
        QCOMPARE(model.rowCount(), 1);
    }
}

void TestNodeTableModel::sortReprojectsMetricOrder() {
    const std::shared_ptr<ScanResult> document = makeChildrenDocument();
    NodeTableModel model;
    model.setProjection(document, diskmap::nodeKey(document->root), {},
                        SortSpec{SizeMetric::Logical, true});

    model.sort(NodeTableModel::AllocatedColumn, Qt::AscendingOrder);
    QCOMPARE((rowNames(model)),
             (QStringList{QStringLiteral("small.txt"), QStringLiteral("subdir"),
                          QStringLiteral("alpha.txt"), QStringLiteral("beta.bin"),
                          QStringLiteral("incomplete.bin"), QStringLiteral("unknown.bin")}));
    model.sort(NodeTableModel::AllocatedColumn, Qt::DescendingOrder);
    QCOMPARE((rowNames(model)),
             (QStringList{QStringLiteral("beta.bin"), QStringLiteral("alpha.txt"),
                          QStringLiteral("subdir"), QStringLiteral("small.txt"),
                          QStringLiteral("incomplete.bin"), QStringLiteral("unknown.bin")}));
}

QTEST_MAIN(TestNodeTableModel)
#include "test_node_table_model.moc"
