#include <QFileInfo>
#include <QTableView>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QLabel>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "diskmap/duplicates.hpp"
#include "diskmap/fs_node.hpp"
#include "diskmap/gui/main_window.hpp"
#include "diskmap/gui/treemap_widget.hpp"

class TestStorageWorkbench final : public QObject {
    Q_OBJECT

private slots:
    void snapshotControlsSaveLoadReadOnlyEvidence();
    void snapshotComparisonUsesVisibleEvidence();
    void duplicateEvidenceStagesOnlyThroughCleanupReview();
};

namespace {

using diskmap::FileIdentity;
using diskmap::FsKind;
using diskmap::FsNode;
using diskmap::ScanResult;

FsNode makeFile(const std::filesystem::path& path,
                std::uint64_t size,
                std::uint64_t identity,
                std::uint64_t generation) {
    FsNode node;
    node.name = path.filename().string();
    node.path = path;
    node.size = size;
    node.logical_size_known = true;
    node.scan_generation = generation;
    node.metadata.kind = FsKind::RegularFile;
    node.metadata.identity = FileIdentity{31, identity, true};
    node.metadata.logical_size = size;
    node.metadata.allocated_size = size;
    node.metadata.allocated_size_known = true;
    node.metadata.hard_link_count = 1;
    node.metadata.hard_link_count_known = true;
    node.metadata.modified_ns = 100;
    node.metadata.modified_time_known = true;
    node.metadata.complete = true;
    node.complete = true;
    node.allocated_size = size;
    node.allocated_size_known = true;
    node.reclaimable_size = size;
    node.reclaimable_size_known = true;
    return node;
}

ScanResult makeScan(const QString& input,
                    const diskmap::ScanOptions& options,
                    std::uint64_t fileSize = 4,
                    bool duplicateFiles = false) {
    const std::filesystem::path rootPath(input.toStdString());
    ScanResult result;
    result.generation = options.generation;
    result.dirs_scanned = 1;
    result.files_scanned = duplicateFiles ? 2 : 1;
    result.root.name = rootPath.filename().string();
    result.root.path = rootPath;
    result.root.is_dir = true;
    result.root.scan_generation = options.generation;
    result.root.metadata.kind = FsKind::Directory;
    result.root.metadata.identity = FileIdentity{31, 1, true};
    result.root.metadata.complete = true;
    result.root.complete = true;
    result.root.children.push_back(
        makeFile(rootPath / "first.bin", fileSize, 2, options.generation));
    if (duplicateFiles) {
        result.root.children.push_back(
            makeFile(rootPath / "second.bin", fileSize, 3, options.generation));
    }
    diskmap::aggregateSizes(result.root);
    diskmap::aggregateStorage(result.root);
    return result;
}

MainWindow::ScanRunner scanRunner(std::uint64_t fileSize = 4,
                                  bool duplicateFiles = false) {
    return [fileSize, duplicateFiles](const QString& path,
                                       const diskmap::ScanOptions& options,
                                       const std::shared_ptr<diskmap::ScanCancellationToken>&,
                                       const diskmap::ProgressFn&) {
        return makeScan(path, options, fileSize, duplicateFiles);
    };
}

MainWindow::ScanRunner changingScanRunner() {
    const auto callCount = std::make_shared<int>(0);
    return [callCount](const QString& path,
                       const diskmap::ScanOptions& options,
                       const std::shared_ptr<diskmap::ScanCancellationToken>&,
                       const diskmap::ProgressFn&) {
        ++*callCount;
        return makeScan(path, options, *callCount == 1 ? 4 : 8);
    };
}

MainWindow::DuplicateRunner duplicateRunner() {
    return [](const ScanResult& scan,
              const diskmap::DuplicateAnalysisOptions&,
              const std::shared_ptr<diskmap::ScanCancellationToken>&,
              const diskmap::DuplicateProgressFn&) {
        diskmap::DuplicateAnalysis analysis;
        analysis.candidates_seen = 2;
        analysis.candidates_retained = 2;
        analysis.files_hashed = 2;
        analysis.bytes_read = 8;
        diskmap::DuplicateGroup group;
        group.size = 4;
        group.partial_fingerprint = "partial";
        group.content_hash = "sha256-evidence";
        group.certain = true;
        group.reclaimable = true;
        group.reclaimable_bytes = 4;
        for (const FsNode& node : scan.root.children) {
            diskmap::DuplicateEntry entry;
            entry.key = diskmap::nodeKey(node);
            entry.path = node.path;
            entry.size = node.size;
            entry.identity = node.metadata.identity;
            entry.hard_link_count = 1;
            entry.hard_link_count_known = true;
            entry.partial_fingerprint = group.partial_fingerprint;
            entry.content_hash = group.content_hash;
            entry.certain = true;
            group.entries.push_back(std::move(entry));
        }
        analysis.groups.push_back(std::move(group));
        return analysis;
    };
}

QPushButton* button(MainWindow& window, const QString& name) {
    return window.findChild<QPushButton*>(name);
}

QTableWidget* table(MainWindow& window, const QString& name) {
    return window.findChild<QTableWidget*>(name);
}

QLabel* label(MainWindow& window, const QString& name) {
    return window.findChild<QLabel*>(name);
}

void waitForRootRows(MainWindow& window, int expected) {
    QTRY_VERIFY(window.findChild<QTableView*>(QStringLiteral("nodeTable")) != nullptr);
    QTableView* nodeTable = window.findChild<QTableView*>(QStringLiteral("nodeTable"));
    QTRY_COMPARE(nodeTable->model()->rowCount(), expected);
}

void waitForRootSize(MainWindow& window, std::uint64_t expected) {
    QTRY_VERIFY(window.findChild<TreemapWidget*>(QStringLiteral("treemap")) != nullptr);
    TreemapWidget* widget =
        window.findChild<TreemapWidget*>(QStringLiteral("treemap"));
    QTRY_VERIFY(widget->currentNode() != nullptr);
    QTRY_COMPARE(widget->currentNode()->size, expected);
}

} // namespace

void TestStorageWorkbench::snapshotControlsSaveLoadReadOnlyEvidence() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString rootPath = temporary.filePath(QStringLiteral("scan-root"));
    const QString snapshotPath = temporary.filePath(QStringLiteral("before.json"));

    MainWindow window(nullptr, scanRunner());
    window.scanPath(rootPath);
    waitForRootRows(window, 1);

    QVERIFY(button(window, QStringLiteral("saveSnapshotButton")) != nullptr);
    QVERIFY(button(window, QStringLiteral("loadSnapshotButton")) != nullptr);
    QVERIFY(button(window, QStringLiteral("compareSnapshotButton")) != nullptr);
    QVERIFY(table(window, QStringLiteral("snapshotChangesTable")) != nullptr);
    QVERIFY(table(window, QStringLiteral("duplicateEvidenceTable")) != nullptr);

    window.saveSnapshotPath(snapshotPath);
    QVERIFY(QFileInfo::exists(snapshotPath));
    window.loadSnapshotPath(snapshotPath);

    QLabel* status = label(window, QStringLiteral("status"));
    QLabel* banner = label(window, QStringLiteral("partialBanner"));
    QVERIFY(status != nullptr);
    QVERIFY(banner != nullptr);
    QTRY_VERIFY(status->text().contains(QStringLiteral("read-only snapshot")));
    QVERIFY(!banner->isHidden());
    QVERIFY(banner->text().contains(QStringLiteral("read-only snapshot")));
    QVERIFY(!button(window, QStringLiteral("rescanButton"))->isEnabled());
    QVERIFY(!button(window, QStringLiteral("stageCleanupButton"))->isEnabled());
    QVERIFY(!button(window, QStringLiteral("executeCleanupButton"))->isEnabled());
}

void TestStorageWorkbench::snapshotComparisonUsesVisibleEvidence() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString rootPath = temporary.filePath(QStringLiteral("scan-root"));
    const QString snapshotPath = temporary.filePath(QStringLiteral("before.json"));

    MainWindow window(nullptr, changingScanRunner());
    window.scanPath(rootPath);
    waitForRootRows(window, 1);
    window.saveSnapshotPath(snapshotPath);

    window.scanPath(rootPath);
    waitForRootSize(window, 8);
    window.compareSnapshotPath(snapshotPath);

    QTableWidget* changes = table(window, QStringLiteral("snapshotChangesTable"));
    QLabel* summary = label(window, QStringLiteral("snapshotSummary"));
    QVERIFY(changes != nullptr);
    QVERIFY(summary != nullptr);
    QVERIFY(changes->rowCount() >= 1);
    QVERIFY(summary->text().contains(QStringLiteral("change")));
    bool sawGrown = false;
    for (int row = 0; row < changes->rowCount(); ++row) {
        if (changes->item(row, 0) != nullptr
            && changes->item(row, 0)->text() == QStringLiteral("Grown")) {
            sawGrown = true;
        }
    }
    QVERIFY(sawGrown);
}

void TestStorageWorkbench::duplicateEvidenceStagesOnlyThroughCleanupReview() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString rootPath = temporary.filePath(QStringLiteral("duplicate-root"));
    const QString snapshotPath = temporary.filePath(QStringLiteral("duplicates.json"));

    MainWindow window(nullptr, scanRunner(4, true), {}, duplicateRunner());
    window.scanPath(rootPath);
    waitForRootRows(window, 2);
    window.analyzeDuplicatesNow();

    QTableWidget* evidence = table(window, QStringLiteral("duplicateEvidenceTable"));
    QVERIFY(evidence != nullptr);
    QTRY_COMPARE(evidence->rowCount(), 2);
    QVERIFY(button(window, QStringLiteral("stageDuplicatesButton"))->isEnabled());

    button(window, QStringLiteral("stageDuplicatesButton"))->click();
    QTableWidget* review = table(window, QStringLiteral("cleanupReviewTable"));
    QLabel* summary = label(window, QStringLiteral("cleanupSummary"));
    QVERIFY(review != nullptr);
    QVERIFY(summary != nullptr);
    QCOMPARE(review->rowCount(), 1);
    QCOMPARE(review->item(0, 0)->text(), QStringLiteral("Ready"));
    QVERIFY(summary->text().contains(QStringLiteral("nothing has moved")));

    window.saveSnapshotPath(snapshotPath);
    window.loadSnapshotPath(snapshotPath);
    QVERIFY(!button(window, QStringLiteral("stageDuplicatesButton"))->isEnabled());
    QVERIFY(!button(window, QStringLiteral("executeCleanupButton"))->isEnabled());
}

QTEST_MAIN(TestStorageWorkbench)
#include "test_storage_workbench.moc"
