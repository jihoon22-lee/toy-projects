#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <atomic>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#include "diskmap/cleanup.hpp"
#include "diskmap/format.hpp"
#include "diskmap/gui/main_window.hpp"
#include "diskmap/gui/treemap_widget.hpp"
#include "diskmap/snapshot.hpp"
#include "diskmap/trash.hpp"

namespace fs = std::filesystem;
namespace {
void write(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << contents;
}
diskmap::ScanResult scan(const fs::path& path, diskmap::ScanOptions options = {}) {
    diskmap::RealFsSource source;
    return diskmap::scan(source, path, options);
}
QPushButton* button(MainWindow& w, const char* name) { return w.findChild<QPushButton*>(name); }
class EnvironmentGuard {
    QByteArray before_ = qgetenv("XDG_DATA_HOME");
public:
    explicit EnvironmentGuard(const fs::path& home) { qputenv("XDG_DATA_HOME", QByteArray::fromStdString(home.string())); }
    ~EnvironmentGuard() { if (before_.isNull()) qunsetenv("XDG_DATA_HOME"); else qputenv("XDG_DATA_HOME", before_); }
};
}

class TestPortfolioFeatures : public QObject {
    Q_OBJECT
private slots:
    void humanSizesAndBudgets();
    void rawByteSnapshotAndCancellation();
    void keeperPolicyAndAncestorProtection();
    void durableTrashAndRestartRecovery();
    void asyncWorkbenchAndFilters();
    void trashCancellationPreservesCompletedAudit();
};

void TestPortfolioFeatures::humanSizesAndBudgets() {
    QCOMPARE(*diskmap::parseHumanBytes("500 MB"), std::uint64_t{500000000});
    QCOMPARE(*diskmap::parseHumanBytes("1.5 GiB"), std::uint64_t{1610612736});
    QVERIFY(!diskmap::parseHumanBytes("18446744073709551616"));
    QVERIFY(!diskmap::parseHumanBytes("0.1 B"));
    QVERIFY(!diskmap::parseHumanBytes("-1 MB"));
    QVERIFY(!diskmap::parseHumanBytes(std::string(100000, '9')));
    QTemporaryDir temporary;
    const fs::path root(temporary.path().toStdString());
    for (int i = 0; i < 10; ++i) write(root / std::to_string(i), "data");
    diskmap::ScanOptions options; options.max_nodes = 3;
    const auto result = scan(root, options);
    QCOMPARE(diskmap::countNodes(result.root), std::size_t{3});
    QVERIFY(result.budget_exhausted);
    QVERIFY(!result.root.complete);
    QVERIFY(result.fatal_error.empty());
    QVERIFY(!diskmap::snapshotFromNode(result.root).complete);
    diskmap::ScanOptions filtered;
    filtered.exclude_patterns = {"1"};
    const auto filteredScan = scan(root, filtered);
    QVERIFY(filteredScan.totals_filtered);
    QVERIFY(!diskmap::snapshotFromNode(filteredScan.root).complete);
    options.max_nodes = 100;
    options.max_memory_bytes = sizeof(diskmap::FsNode) + root.native().size() * 3 + 1;
    const auto tiny = scan(root, options);
    QVERIFY(tiny.budget_exhausted);
    QCOMPARE(diskmap::countNodes(tiny.root), std::size_t{1});
}

void TestPortfolioFeatures::rawByteSnapshotAndCancellation() {
    QTemporaryDir temporary;
    const fs::path root(temporary.path().toStdString());
    const auto raw = root / std::string("raw-\xff", 5);
    write(raw, "payload");
    const auto result = scan(root);
    const auto snapshot = diskmap::snapshotFromNode(result.root);
    QCOMPARE(snapshot.schema_version, std::string(diskmap::kSnapshotSchemaV2));
    const auto encoded = diskmap::serializeSnapshot(snapshot);
    const auto parsed = diskmap::parseSnapshot(encoded);
    QCOMPARE(parsed.root.children[0].path.native(), raw.native());
    QCOMPARE(parsed.root.children[0].metadata.changed_ns, snapshot.root.children[0].metadata.changed_ns);
    QVERIFY(parsed.root.children[0].metadata.changed_time_known);
    QCOMPARE(diskmap::serializeSnapshot(parsed), encoded);
    auto document = QJsonDocument::fromJson(QByteArray::fromStdString(encoded));
    auto object = document.object();
    auto node = object["root"].toObject(); node["path_bytes"] = "00"; object["root"] = node;
    QVERIFY_EXCEPTION_THROWN(diskmap::parseSnapshot(QJsonDocument(object).toJson().toStdString()), diskmap::SnapshotError);
    diskmap::SnapshotLimits limits; limits.cancelled = [] { return true; };
    QVERIFY_EXCEPTION_THROWN(diskmap::snapshotFromNode(result.root, limits), diskmap::SnapshotError);
    QVERIFY_EXCEPTION_THROWN(diskmap::parseSnapshot(encoded, limits), diskmap::SnapshotError);
    diskmap::SnapshotDiffOptions diffOptions; diffOptions.cancelled = [] { return true; };
    QVERIFY_EXCEPTION_THROWN(diskmap::diffSnapshots(snapshot, snapshot, diffOptions), diskmap::SnapshotError);
    QVERIFY_EXCEPTION_THROWN(diskmap::writeSnapshotAtomically(snapshot, raw), diskmap::SnapshotError);
    const auto alias = root / "alias";
    fs::create_hard_link(raw, alias);
    QVERIFY_EXCEPTION_THROWN(diskmap::writeSnapshotAtomically(snapshot, alias), diskmap::SnapshotError);
    fs::remove(alias);
    const auto output = root / "snapshot.json";
    write(output, "existing");
    QVERIFY_EXCEPTION_THROWN(diskmap::writeSnapshotAtomically(snapshot, output, limits), diskmap::SnapshotError);
    std::ifstream stream(output); std::string contents; stream >> contents;
    QCOMPARE(contents, std::string("existing"));
    fs::remove(raw);
    const auto clean = scan(root);
    auto legacy = diskmap::snapshotFromNode(clean.root); legacy.schema_version = diskmap::kSnapshotSchemaV1;
    QCOMPARE(diskmap::parseSnapshot(diskmap::serializeSnapshot(legacy)).schema_version, std::string(diskmap::kSnapshotSchemaV1));
}

void TestPortfolioFeatures::keeperPolicyAndAncestorProtection() {
    QTemporaryDir temporary;
    const fs::path root(temporary.path().toStdString());
    write(root / "keep" / "one", "equal"); write(root / "other" / "two", "equal");
    fs::last_write_time(root / "keep" / "one", fs::file_time_type::clock::now() - std::chrono::hours(2));
    auto result = scan(root);
    const auto analysis = diskmap::analyzeDuplicates(result);
    QCOMPARE(analysis.groups.size(), std::size_t{1});
    const auto keeper = diskmap::chooseDuplicateKeeper(analysis.groups[0], result,
        diskmap::DuplicateKeeperPolicy::PreferredDirectory, root / "keep");
    QVERIFY(keeper);
    const auto newest = diskmap::chooseDuplicateKeeper(analysis.groups[0], result, diskmap::DuplicateKeeperPolicy::Newest);
    const auto oldest = diskmap::chooseDuplicateKeeper(analysis.groups[0], result, diskmap::DuplicateKeeperPolicy::Oldest);
    QVERIFY(newest); QVERIFY(oldest);
    QCOMPARE(newest->normalized_path, (root / "other" / "two").string());
    QCOMPARE(oldest->normalized_path, (root / "keep" / "one").string());
    QCOMPARE(keeper->normalized_path, (root / "keep" / "one").string());
    diskmap::CleanupPolicy policy; policy.protected_roots = {root / "keep" / "one"};
    const auto* parent = diskmap::findChild(result.root, "keep"); QVERIFY(parent);
    const auto plan = diskmap::planCleanup(result, {diskmap::nodeKey(*parent)}, policy, &analysis);
    QVERIFY(plan.targets.empty()); QVERIFY(!plan.rejected.empty());
    std::vector<diskmap::NodeKey> all;
    for (const auto& entry : analysis.groups[0].entries) all.push_back(entry.key);
    QVERIFY(diskmap::planCleanup(result, all, {}, &analysis).targets.empty());
}

void TestPortfolioFeatures::durableTrashAndRestartRecovery() {
    QTemporaryDir temporary;
    const fs::path base(temporary.path().toStdString());
    const auto root = base / "files";
    write(root / "a", "equal"); write(root / "b", "equal");
    const auto result = scan(root);
    const auto analysis = diskmap::analyzeDuplicates(result);
    QVERIFY(!analysis.groups.empty());
    const auto target = analysis.groups[0].entries[1];
    const auto plan = diskmap::planCleanup(result, {target.key}, {}, &analysis);
    diskmap::TrashOptions options; options.data_home = base / "data";
    const auto moved = diskmap::movePlanToTrash(plan, options);
    QCOMPARE(moved.size(), std::size_t{1}); QCOMPARE(moved[0].status, diskmap::TrashStatus::Moved);
    QVERIFY(!fs::exists(target.path));
    std::string error;
    auto history = diskmap::listTrashHistory(options, error);
    QVERIFY2(error.empty(), error.c_str());
    QCOMPARE(history.size(), std::size_t{1}); QCOMPARE(history[0].restore_token, moved[0].restore_token);
    const auto infoPath = options.data_home / "Trash" / "info" / (moved[0].restore_token + ".trashinfo");
    std::ifstream originalInfo(infoPath, std::ios::binary);
    const std::string metadata((std::istreambuf_iterator<char>(originalInfo)), std::istreambuf_iterator<char>());
    originalInfo.close();
    write(infoPath, "invalid metadata");
    const auto invalidHistory = diskmap::listTrashHistory(options, error);
    QVERIFY(std::all_of(invalidHistory.begin(), invalidHistory.end(), [](const auto& entry) { return entry.restore_token.empty(); }));
    write(infoPath, metadata);
    const auto receiptFile = fs::directory_iterator(options.data_home / "Trash" / ".diskmap-receipts")->path();
    std::ifstream originalReceipt(receiptFile, std::ios::binary);
    const std::string movedRecord((std::istreambuf_iterator<char>(originalReceipt)), std::istreambuf_iterator<char>());
    originalReceipt.close();
    // The standard metadata is sufficient even after losing the supplementary audit record.
    fs::remove_all(options.data_home / "Trash" / ".diskmap-receipts");
    history = diskmap::listTrashHistory(options, error);
    QCOMPARE(history.size(), std::size_t{1}); QVERIFY(!history[0].restore_token.empty());
    QCOMPARE(diskmap::restoreFromTrash(history[0].restore_token, options).status, diskmap::TrashStatus::Restored);
    QVERIFY(fs::exists(target.path));
    // A lower PID or arbitrary filesystem ordering can list restored before moved.
    write(options.data_home / "Trash" / ".diskmap-receipts" / "z-old.receipt", movedRecord);
    history = diskmap::listTrashHistory(options, error);
    QCOMPARE(history.size(), std::size_t{1}); QCOMPARE(history[0].status, diskmap::TrashStatus::Restored);
    QVERIFY(history[0].restore_token.empty());
}

void TestPortfolioFeatures::asyncWorkbenchAndFilters() {
    QTemporaryDir temporary;
    const fs::path base(temporary.path().toStdString());
    EnvironmentGuard environment(base / "data");
    const auto root = base / "files";
    write(root / "a.bin", "equal"); write(root / "b.bin", "equal");
    MainWindow::CleanupServices services;
    services.confirm = [](const auto&) { return true; };
    MainWindow window(nullptr, {}, services);
    window.resize(1180, 800); window.show(); window.scanPath(QString::fromStdString(root.string()));
    QTRY_VERIFY(button(window, "saveSnapshotButton")->isEnabled());
    auto* tabs = window.findChild<QTabWidget*>("workbenchTabs"); QVERIFY(tabs); QCOMPARE(tabs->count(), 4);
    const auto snapshotPath = QString::fromStdString((base / "before.json").string());
    window.saveSnapshotPath(snapshotPath);
    QVERIFY(button(window, "cancelScanButton")->isEnabled());
    QTRY_VERIFY(button(window, "saveSnapshotButton")->isEnabled());
    write(root / "b.bin", "changed-and-grown"); window.scanPath(QString::fromStdString(root.string()));
    QTRY_VERIFY(button(window, "saveSnapshotButton")->isEnabled());
    window.compareSnapshotPath(snapshotPath);
    auto* changes = window.findChild<QTableWidget*>("snapshotChangesTable");
    QTRY_VERIFY(changes->rowCount() > 0);
    auto* kind = window.findChild<QComboBox*>("diffKindCombo");
    kind->setCurrentIndex(1); QCOMPARE(changes->rowCount(), 0); // no added files
    kind->setCurrentIndex(0); QVERIFY(changes->rowCount() > 0);
    QVERIFY(window.findChild<QLabel*>("folderChangeSummary")->text().contains("bytes"));
    auto* table = window.findChild<QTableView*>("nodeTable");
    table->setCurrentIndex(table->model()->index(0, 0));
    QVERIFY(window.findChild<TreemapWidget*>("treemap")->selectedKey());
    window.findChild<QComboBox*>("treemapColorCombo")->setCurrentIndex(3);
    if (const auto screenshot = qgetenv("DISKMAP_SCREENSHOT"); !screenshot.isEmpty()) {
        tabs->setCurrentIndex(0); QTest::qWait(30); QVERIFY(window.grab().save(QString::fromUtf8(screenshot)));
    }
    write(root / "b.bin", "equal"); window.scanPath(QString::fromStdString(root.string()));
    QTRY_VERIFY(button(window, "saveSnapshotButton")->isEnabled());
    window.analyzeDuplicatesNow();
    auto* evidence = window.findChild<QTableWidget*>("duplicateEvidenceTable");
    QTRY_COMPARE(evidence->rowCount(), 2);
    evidence->setCurrentCell(1, 0); button(window, "keepSelectedDuplicateButton")->click();
    button(window, "stageDuplicatesButton")->click();
    auto* review = window.findChild<QTableWidget*>("cleanupReviewTable");
    QCOMPARE(review->rowCount(), 1); QVERIFY(review->item(0, 1)->text().endsWith("a.bin"));
    button(window, "executeCleanupButton")->click();
    QTRY_VERIFY(button(window, "saveSnapshotButton")->isEnabled());
    QVERIFY(!fs::exists(root / "a.bin")); QVERIFY(fs::exists(root / "b.bin"));
    MainWindow restarted;
    auto* restartTabs = restarted.findChild<QTabWidget*>("workbenchTabs"); restartTabs->setCurrentIndex(3);
    auto* tokens = restarted.findChild<QComboBox*>("restoreTokenCombo");
    QTRY_COMPARE(tokens->count(), 1);
    button(restarted, "restoreTrashButton")->click();
    QTRY_COMPARE(tokens->count(), 0); QVERIFY(fs::exists(root / "a.bin"));
}

void TestPortfolioFeatures::trashCancellationPreservesCompletedAudit() {
    QTemporaryDir temporary;
    const fs::path root(temporary.path().toStdString());
    write(root / "a", "first"); write(root / "b", "second");
    auto entered = std::make_shared<std::atomic<bool>>(false);
    auto release = std::make_shared<std::atomic<bool>>(false);
    auto calls = std::make_shared<std::atomic<int>>(0);
    MainWindow::CleanupServices services;
    services.confirm = [](const auto&) { return true; };
    services.move = [entered, release, calls](const auto& plan) {
        ++*calls; entered->store(true);
        for (int i = 0; i < 2000 && !release->load(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        diskmap::TrashReceipt receipt; receipt.status = diskmap::TrashStatus::Moved;
        receipt.original_path = plan.targets[0].path; receipt.restore_token = "completed-token";
        return std::vector<diskmap::TrashReceipt>{receipt};
    };
    MainWindow window(nullptr, {}, services); window.scanPath(QString::fromStdString(root.string()));
    QTRY_VERIFY(button(window, "saveSnapshotButton")->isEnabled());
    auto* table = window.findChild<QTableView*>("nodeTable"); table->selectAll();
    button(window, "stageCleanupButton")->click(); button(window, "executeCleanupButton")->click();
    QTRY_VERIFY(entered->load());
    // The GUI timer remains responsive while the injected backend is blocked.
    bool ticked = false; QTimer::singleShot(0, &window, [&] { ticked = true; }); QTRY_VERIFY(ticked);
    window.cancelScan(); release->store(true);
    auto* audit = window.findChild<QTableWidget*>("cleanupAuditTable");
    QTRY_COMPARE(audit->rowCount(), 2);
    QCOMPARE(calls->load(), 1);
    QCOMPARE(audit->item(0, 0)->text(), QString("moved"));
    QCOMPARE(audit->item(1, 0)->text(), QString("cancelled"));
    QCOMPARE(window.findChild<QComboBox*>("restoreTokenCombo")->count(), 1);
}

QTEST_MAIN(TestPortfolioFeatures)
#include "test_portfolio_features.moc"
