#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMutex>
#include <QMutexLocker>
#include <QPushButton>
#include <QStringList>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QWaitCondition>
#include <QtTest>
#include <QWidget>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "diskmap/fs_node.hpp"
#include "diskmap/gui/main_window.hpp"
#include "diskmap/gui/treemap_widget.hpp"
#include "diskmap/view.hpp"

class TestMainWindow : public QObject {
    Q_OBJECT

private slots:
    void scanningDisplaysTheRootResult();
    void uiControlsHaveStableNamesAndAccessibleLabels();
    void tableAndTreemapShowProjectedDocument();
    void clickableBreadcrumbSegmentsNavigate();
    void representativeFiltersReprojectTable();
    void metricExplanationTracksSelection();
    void largestFilesModeProjectsRegularFiles();
    void incompleteProjectionShowsBanner();
    void tableActivationNavigatesToDirectory();
    void rescanPreservesDeepNavigationAndSelection();
    void rescanFallsBackWhenDirectoryDisappears();
    void rescanFallsBackWhenDirectoryIdentityChanges();
    void rescanClearsSelectionWhenEntryDisappears();
    void rescanCannotDisplayAnOlderGeneration();
    void rescanCancelsPreviousToken();
    void progressAppearsWhileRunnerIsBlocked();
    void cancelDiscardsPartialResultAndPreservesVisibleResult();
    void selectionSurvivesTextAndMetricSortsAndModelResets();
    void explorerControlsAreDisabledAndInertDuringRescan();
    void generationMetadataMismatchPreservesVisibleDocument();
    void summariesUseSelectedMetricAndConservativeValues();
    void metricOnlyUncertaintyUsesGenericReason();
    void regularFileRootSupportsLargestFilesActivation();
    void destructionWithLateProgressCallbackIsSafe();
    void activatingADirectoryDescendsAndUpdatesBreadcrumb();
    void goingUpReturnsToTheRootAndStopsThere();
    void activatingALeafDoesNothing();
    void cleanupStagingIsReviewableAndUndoable();
    void cleanupAuditKeepsOnlyRealRestoreTokens();
};

namespace {

QWidget* breadcrumb(MainWindow& window) {
    return window.findChild<QWidget*>(QStringLiteral("breadcrumb"));
}

QString breadcrumbPath(MainWindow& window) {
    QWidget* bar = breadcrumb(window);
    return bar == nullptr ? QString() : bar->property("currentPath").toString();
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

QPushButton* rescanButton(MainWindow& window) {
    return window.findChild<QPushButton*>(QStringLiteral("rescanButton"));
}

QToolButton* breadcrumbSegment(MainWindow& window, int index) {
    return window.findChild<QToolButton*>(
        QStringLiteral("breadcrumbSegment%1").arg(index));
}

QLineEdit* searchEdit(MainWindow& window) {
    return window.findChild<QLineEdit*>(QStringLiteral("searchEdit"));
}

QLineEdit* minimumSizeEdit(MainWindow& window) {
    return window.findChild<QLineEdit*>(QStringLiteral("minimumSizeEdit"));
}

QComboBox* metricCombo(MainWindow& window) {
    return window.findChild<QComboBox*>(QStringLiteral("metricCombo"));
}

QComboBox* typeCombo(MainWindow& window) {
    return window.findChild<QComboBox*>(QStringLiteral("typeCombo"));
}

QComboBox* ageCombo(MainWindow& window) {
    return window.findChild<QComboBox*>(QStringLiteral("ageCombo"));
}

QComboBox* modeCombo(MainWindow& window) {
    return window.findChild<QComboBox*>(QStringLiteral("viewModeCombo"));
}

QLabel* metricExplanation(MainWindow& window) {
    return window.findChild<QLabel*>(QStringLiteral("metricExplanation"));
}

QLabel* partialBanner(MainWindow& window) {
    return window.findChild<QLabel*>(QStringLiteral("partialBanner"));
}

QLabel* projectionSummary(MainWindow& window) {
    return window.findChild<QLabel*>(QStringLiteral("projectionSummary"));
}

QTableView* nodeTable(MainWindow& window) {
    return window.findChild<QTableView*>(QStringLiteral("nodeTable"));
}

QPushButton* cleanupButton(MainWindow& window, const char* name) {
    return window.findChild<QPushButton*>(QString::fromLatin1(name));
}

QTableWidget* cleanupReview(MainWindow& window) {
    return window.findChild<QTableWidget*>(QStringLiteral("cleanupReviewTable"));
}

QTableWidget* cleanupAudit(MainWindow& window) {
    return window.findChild<QTableWidget*>(QStringLiteral("cleanupAuditTable"));
}

QComboBox* restoreTokens(MainWindow& window) {
    return window.findChild<QComboBox*>(QStringLiteral("restoreTokenCombo"));
}

QLabel* cleanupSummary(MainWindow& window) {
    return window.findChild<QLabel*>(QStringLiteral("cleanupSummary"));
}

NodeTableModel* tableModel(MainWindow& window) {
    QTableView* table = nodeTable(window);
    return table == nullptr ? nullptr : qobject_cast<NodeTableModel*>(table->model());
}

QStringList tableRowNames(MainWindow& window) {
    QStringList names;
    NodeTableModel* model = tableModel(window);
    if (model == nullptr) {
        return names;
    }
    for (int row = 0; row < model->rowCount(); ++row) {
        names << model->data(model->index(row, NodeTableModel::NameColumn),
                             Qt::DisplayRole)
                     .toString();
    }
    return names;
}

QModelIndex tableIndexNamed(MainWindow& window, const QString& name) {
    NodeTableModel* model = tableModel(window);
    if (model == nullptr) {
        return QModelIndex();
    }
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex index = model->index(row, NodeTableModel::NameColumn);
        if (model->data(index, Qt::DisplayRole).toString() == name) {
            return index;
        }
    }
    return QModelIndex();
}

bool currentSelectionHasKey(MainWindow& window, const diskmap::NodeKey& key) {
    QTableView* table = nodeTable(window);
    NodeTableModel* model = tableModel(window);
    if (table == nullptr || model == nullptr || table->selectionModel() == nullptr) {
        return false;
    }
    const QModelIndex current = table->selectionModel()->currentIndex();
    if (!current.isValid()) {
        return false;
    }
    const std::optional<diskmap::NodeKey> currentKey = model->keyAt(current.row());
    return currentKey.has_value() && *currentKey == key;
}

void activateTableRow(MainWindow& window, const QString& name) {
    QTableView* table = nodeTable(window);
    QVERIFY(table != nullptr);
    if (table == nullptr) {
        return;
    }
    const QModelIndex index = tableIndexNamed(window, name);
    QVERIFY(index.isValid());
    if (index.isValid()) {
        emit table->activated(index);
    }
}

void selectTableRows(MainWindow& window, const QStringList& names) {
    QTableView* table = nodeTable(window);
    QVERIFY(table != nullptr);
    QVERIFY(table->selectionModel() != nullptr);
    table->selectionModel()->clearSelection();
    for (const QString& name : names) {
        const QModelIndex index = tableIndexNamed(window, name);
        QVERIFY2(index.isValid(), qPrintable(name));
        if (index.isValid()) {
            table->selectionModel()->select(
                index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }
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

using diskmap::FileIdentity;
using diskmap::FsKind;
using diskmap::FsNode;
using diskmap::ScanResult;

enum class ExplorerResultKind {
    Complete,
    Incomplete,
    StableRescan,
    MissingDeepDirectory,
    ChangedDeepIdentity,
    MissingSelectedFile,
};

FsNode makeExplorerDirectory(std::string name,
                             std::filesystem::path path,
                             std::vector<FsNode> children = {},
                             FileIdentity identity = {}) {
    FsNode node;
    node.name = std::move(name);
    node.path = std::move(path);
    node.is_dir = true;
    node.metadata.kind = FsKind::Directory;
    node.metadata.identity = identity;
    node.metadata.complete = true;
    node.complete = true;
    node.children = std::move(children);
    return node;
}

FsNode makeExplorerFile(std::string name,
                        std::filesystem::path path,
                        std::uint64_t logical,
                        std::uint64_t allocated,
                        std::int64_t modified,
                        FileIdentity identity = {}) {
    FsNode node;
    node.name = std::move(name);
    node.path = std::move(path);
    node.is_dir = false;
    node.size = logical;
    node.logical_size_known = true;
    node.metadata.kind = FsKind::RegularFile;
    node.metadata.identity = identity;
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
    node.reclaimable_size = allocated;
    node.reclaimable_size_known = true;
    return node;
}

FsNode makeExplorerSymlink(std::string name,
                           std::filesystem::path path,
                           std::uint64_t logical,
                           std::int64_t modified,
                           FileIdentity identity = {}) {
    FsNode node = makeExplorerFile(std::move(name), std::move(path), logical, logical,
                                   modified, identity);
    node.metadata.kind = FsKind::Symlink;
    node.followed = true;
    node.has_target_metadata = true;
    node.target_metadata.kind = FsKind::RegularFile;
    node.target_metadata.logical_size = logical;
    node.target_metadata.allocated_size = logical;
    node.target_metadata.allocated_size_known = true;
    node.target_metadata.hard_link_count = 1;
    node.target_metadata.hard_link_count_known = true;
    node.target_metadata.modified_ns = modified;
    node.target_metadata.modified_time_known = true;
    node.target_metadata.complete = true;
    return node;
}

void setExplorerGeneration(FsNode& node, std::uint64_t generation) {
    node.scan_generation = generation;
    for (FsNode& child : node.children) {
        setExplorerGeneration(child, generation);
    }
}

ScanResult explorerResult(const QString& path,
                          const diskmap::ScanOptions& options,
                          ExplorerResultKind kind) {
    ScanResult result;
    result.generation = options.generation;
    result.dirs_scanned = 4;
    result.files_scanned = 6;

    const std::filesystem::path rootPath =
        std::filesystem::path("/virtual") / path.toStdString();
    const std::int64_t now = QDateTime::currentMSecsSinceEpoch() * 1'000'000LL;
    const FileIdentity projectsIdentity{91, 2, true};
    const FileIdentity deepIdentity =
        kind == ExplorerResultKind::ChangedDeepIdentity && options.generation >= 2
            ? FileIdentity{91, 4, true}
            : FileIdentity{91, 3, true};

    std::vector<FsNode> deepChildren;
    if (!(kind == ExplorerResultKind::MissingSelectedFile && options.generation >= 2)) {
        deepChildren.push_back(makeExplorerFile(
            "keep.bin", rootPath / "projects/deep/keep.bin", 40, 32, now,
            FileIdentity{92, 1, true}));
    } else {
        deepChildren.push_back(makeExplorerFile(
            "survivor.bin", rootPath / "projects/deep/survivor.bin", 35, 28, now,
            FileIdentity{92, 5, true}));
    }
    FsNode deep = makeExplorerDirectory("deep", rootPath / "projects/deep",
                                        std::move(deepChildren), deepIdentity);

    std::vector<FsNode> projectChildren;
    if (!(kind == ExplorerResultKind::MissingDeepDirectory && options.generation >= 2)) {
        projectChildren.push_back(std::move(deep));
    }
    if (kind == ExplorerResultKind::MissingDeepDirectory && options.generation >= 2) {
        projectChildren.push_back(makeExplorerFile(
            "replacement.bin", rootPath / "projects/replacement.bin", 22, 20, now,
            FileIdentity{92, 6, true}));
    }
    FsNode projects = makeExplorerDirectory("projects", rootPath / "projects",
                                            std::move(projectChildren), projectsIdentity);

    std::vector<FsNode> rootChildren;
    rootChildren.push_back(std::move(projects));
    rootChildren.push_back(makeExplorerFile(
        "needle.bin", rootPath / "needle.bin", 100, 80, now,
        FileIdentity{92, 10, true}));
    rootChildren.push_back(makeExplorerFile(
        "other.bin", rootPath / "other.bin", 20, 16, now,
        FileIdentity{92, 11, true}));
    rootChildren.push_back(makeExplorerFile(
        "old.bin", rootPath / "old.bin", 5, 5, 0, FileIdentity{92, 12, true}));
    rootChildren.push_back(makeExplorerSymlink(
        "alias.bin", rootPath / "alias.bin", 120, now, FileIdentity{92, 13, true}));

    if (kind == ExplorerResultKind::StableRescan
        || kind == ExplorerResultKind::MissingDeepDirectory
        || kind == ExplorerResultKind::ChangedDeepIdentity
        || kind == ExplorerResultKind::MissingSelectedFile) {
        // Rescan fixtures intentionally keep the same root and intermediate
        // keys while varying only the requested descendant.
        rootChildren.resize(1);
    }

    result.root = makeExplorerDirectory("root", rootPath, std::move(rootChildren));
    if (kind == ExplorerResultKind::Incomplete) {
        result.root.complete = false;
        result.root.error = "permission denied while listing";
        result.error_count = 1;
        result.errors.push_back(result.root.error);
    }
    setExplorerGeneration(result.root, options.generation);
    diskmap::aggregateSizes(result.root);
    diskmap::aggregateStorage(result.root);
    return result;
}

MainWindow::ScanRunner explorerRunner(ExplorerResultKind kind) {
    return [kind](const QString& path,
                  const diskmap::ScanOptions& options,
                  const std::shared_ptr<diskmap::ScanCancellationToken>&,
                  const diskmap::ProgressFn&) {
        return explorerResult(path, options, kind);
    };
}

enum class GenerationMismatchKind {
    ResultOnly,
    RootOnly,
};

MainWindow::ScanRunner generationMismatchRunner(GenerationMismatchKind mismatch) {
    return [mismatch](const QString& path,
                      const diskmap::ScanOptions& options,
                      const std::shared_ptr<diskmap::ScanCancellationToken>&,
                      const diskmap::ProgressFn&) {
        diskmap::ScanResult result = fakeResult(path, options);
        if (path == QStringLiteral("mismatched")) {
            if (mismatch == GenerationMismatchKind::ResultOnly) {
                result.generation = options.generation + 1;
            } else {
                result.root.scan_generation = options.generation + 1;
            }
        }
        return result;
    };
}

ScanResult metricOnlyUnknownResult(const QString& path,
                                   const diskmap::ScanOptions& options,
                                   bool zeroValue) {
    ScanResult result = explorerResult(path, options, ExplorerResultKind::Complete);
    if (zeroValue) {
        result.root.children.clear();
        result.root.size = 0;
        result.root.allocated_size = 0;
        result.root.reclaimable_size = 0;
        result.files_scanned = 0;
    }
    // The tree and metadata remain complete; only the selected physical
    // aggregates are deliberately unavailable. This distinguishes a metric
    // uncertainty from a metadata or traversal issue.
    result.root.allocated_size_known = false;
    result.root.reclaimable_size_known = false;
    return result;
}

MainWindow::ScanRunner metricOnlyUnknownRunner(bool zeroValue = false) {
    return [zeroValue](const QString& path,
                       const diskmap::ScanOptions& options,
                       const std::shared_ptr<diskmap::ScanCancellationToken>&,
                       const diskmap::ProgressFn&) {
        return metricOnlyUnknownResult(path, options, zeroValue);
    };
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

// Returns one completed document, then holds the second invocation until the
// test has inspected the live rescan state. The second result is still valid,
// so the test can release it and verify that the controls recover normally.
struct RescanProbe {
    mutable QMutex mutex;
    QWaitCondition condition;
    int calls = 0;
    bool rescanStarted = false;
    bool rescanReleased = false;
    bool rescanFinished = false;

    ScanResult run(const QString& path,
                   const diskmap::ScanOptions& options,
                   const std::shared_ptr<diskmap::ScanCancellationToken>&,
                   const diskmap::ProgressFn&) {
        int call = 0;
        {
            QMutexLocker locker(&mutex);
            call = ++calls;
        }
        if (call == 1) {
            return explorerResult(path, options, ExplorerResultKind::Complete);
        }

        {
            QMutexLocker locker(&mutex);
            rescanStarted = true;
            condition.wakeAll();
            while (!rescanReleased) {
                condition.wait(&mutex);
            }
            rescanFinished = true;
            condition.wakeAll();
        }
        return explorerResult(path, options, ExplorerResultKind::StableRescan);
    }

    bool isRescanStarted() const {
        QMutexLocker locker(&mutex);
        return rescanStarted;
    }

    bool isRescanFinished() const {
        QMutexLocker locker(&mutex);
        return rescanFinished;
    }

    void release() {
        QMutexLocker locker(&mutex);
        rescanReleased = true;
        condition.wakeAll();
    }
};

MainWindow::ScanRunner rescanRunner(const std::shared_ptr<RescanProbe>& probe) {
    return [probe](const QString& path,
                   const diskmap::ScanOptions& options,
                   const std::shared_ptr<diskmap::ScanCancellationToken>& token,
                   const diskmap::ProgressFn& progress) {
        return probe->run(path, options, token, progress);
    };
}

struct LateProgressProbe {
    mutable QMutex mutex;
    mutable QWaitCondition condition;
    bool started = false;
    bool released = false;
    bool finished = false;

    ScanResult run(const QString& path,
                   const diskmap::ScanOptions& options,
                   const std::shared_ptr<diskmap::ScanCancellationToken>&,
                   const diskmap::ProgressFn& progress) {
        {
            QMutexLocker locker(&mutex);
            started = true;
            condition.wakeAll();
            while (!released) {
                condition.wait(&mutex);
            }
        }
        // This callback is intentionally invoked only after the MainWindow
        // owner has gone out of scope in the test.
        if (progress) {
            progress(17, 29);
        }
        {
            QMutexLocker locker(&mutex);
            finished = true;
            condition.wakeAll();
        }
        return fakeResult(path, options);
    }

    bool isStarted() const {
        QMutexLocker locker(&mutex);
        return started;
    }

    bool isFinished() const {
        QMutexLocker locker(&mutex);
        return finished;
    }

    void release() {
        QMutexLocker locker(&mutex);
        released = true;
        condition.wakeAll();
    }

    void waitUntilStartedOrReleased() const {
        QMutexLocker locker(&mutex);
        while (!started && !released) {
            condition.wait(&mutex);
        }
    }
};

MainWindow::ScanRunner lateProgressRunner(const std::shared_ptr<LateProgressProbe>& probe) {
    return [probe](const QString& path,
                   const diskmap::ScanOptions& options,
                   const std::shared_ptr<diskmap::ScanCancellationToken>& token,
                   const diskmap::ProgressFn& progress) {
        return probe->run(path, options, token, progress);
    };
}

struct LateProgressRelease {
    std::shared_ptr<LateProgressProbe> probe;
    std::thread worker;

    explicit LateProgressRelease(std::shared_ptr<LateProgressProbe> value)
        : probe(std::move(value)), worker([probe = this->probe]() {
              probe->waitUntilStartedOrReleased();
              if (!probe->isStarted()) {
                  return;
              }
              std::this_thread::sleep_for(std::chrono::milliseconds(50));
              probe->release();
          }) {}

    ~LateProgressRelease() {
        probe->release();
        if (worker.joinable()) {
            worker.join();
        }
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
    QVERIFY(!breadcrumbPath(window).isEmpty());
    QVERIFY(status(window) != nullptr);
    QVERIFY(status(window)->text().contains(QStringLiteral("dirs")));
    QVERIFY(upButton(window) != nullptr);
    QVERIFY(!upButton(window)->isEnabled());
}

void TestMainWindow::uiControlsHaveStableNamesAndAccessibleLabels() {
    MainWindow window;

    const QStringList objectNames{
        QStringLiteral("chooseFolderButton"), QStringLiteral("rescanButton"),
        QStringLiteral("upButton"),          QStringLiteral("cancelScanButton"),
        QStringLiteral("breadcrumb"),         QStringLiteral("searchEdit"),
        QStringLiteral("metricCombo"),        QStringLiteral("typeCombo"),
        QStringLiteral("ageCombo"),           QStringLiteral("minimumSizeEdit"),
        QStringLiteral("maximumSizeEdit"),    QStringLiteral("issueCombo"),
        QStringLiteral("viewModeCombo"),      QStringLiteral("metricExplanation"),
        QStringLiteral("partialBanner"),      QStringLiteral("explorerSplitter"),
        QStringLiteral("nodeTable"),           QStringLiteral("treemap"),
        QStringLiteral("treemapLegend"),       QStringLiteral("projectionSummary"),
        QStringLiteral("cleanupHeading"),      QStringLiteral("stageCleanupButton"),
        QStringLiteral("clearCleanupButton"),  QStringLiteral("undoCleanupButton"),
        QStringLiteral("redoCleanupButton"),   QStringLiteral("executeCleanupButton"),
        QStringLiteral("restoreTokenCombo"),   QStringLiteral("restoreTrashButton"),
        QStringLiteral("cleanupSummary"),      QStringLiteral("cleanupSplitter"),
        QStringLiteral("cleanupReviewTable"),  QStringLiteral("cleanupAuditTable"),
        QStringLiteral("status"),
    };
    for (const QString& name : objectNames) {
        QWidget* control = window.findChild<QWidget*>(name);
        QVERIFY2(control != nullptr, qPrintable(name));
    }

    const QStringList accessibleNames{
        QStringLiteral("chooseFolderButton"), QStringLiteral("rescanButton"),
        QStringLiteral("upButton"),          QStringLiteral("cancelScanButton"),
        QStringLiteral("breadcrumb"),         QStringLiteral("searchEdit"),
        QStringLiteral("metricCombo"),        QStringLiteral("typeCombo"),
        QStringLiteral("ageCombo"),           QStringLiteral("minimumSizeEdit"),
        QStringLiteral("maximumSizeEdit"),    QStringLiteral("issueCombo"),
        QStringLiteral("viewModeCombo"),      QStringLiteral("metricExplanation"),
        QStringLiteral("partialBanner"),      QStringLiteral("nodeTable"),
        QStringLiteral("treemap"),             QStringLiteral("treemapLegend"),
        QStringLiteral("projectionSummary"),  QStringLiteral("cleanupHeading"),
        QStringLiteral("stageCleanupButton"), QStringLiteral("clearCleanupButton"),
        QStringLiteral("undoCleanupButton"),  QStringLiteral("redoCleanupButton"),
        QStringLiteral("executeCleanupButton"), QStringLiteral("restoreTokenCombo"),
        QStringLiteral("restoreTrashButton"), QStringLiteral("cleanupSummary"),
        QStringLiteral("cleanupReviewTable"), QStringLiteral("cleanupAuditTable"),
        QStringLiteral("status"),
    };
    for (const QString& name : accessibleNames) {
        QWidget* control = window.findChild<QWidget*>(name);
        QVERIFY(control != nullptr);
        QVERIFY2(!control->accessibleName().trimmed().isEmpty(), qPrintable(name));
    }

    QVERIFY(partialBanner(window) != nullptr);
    QVERIFY(partialBanner(window)->isHidden());
    QCOMPARE(breadcrumbPath(window), QString());
    QVERIFY(nodeTable(window) != nullptr);
    QVERIFY(tableModel(window) != nullptr);
    QVERIFY(treemap(window) != nullptr);
}

void TestMainWindow::tableAndTreemapShowProjectedDocument() {
    MainWindow window(nullptr, explorerRunner(ExplorerResultKind::Complete));
    window.scanPath(QStringLiteral("explorer"));
    waitForScan(window);

    QVERIFY(nodeTable(window) != nullptr);
    QVERIFY(tableModel(window) != nullptr);
    QVERIFY(treemap(window) != nullptr);
    QVERIFY(treemap(window)->currentNode() != nullptr);
    QCOMPARE(QString::fromStdString(treemap(window)->currentNode()->name),
             QStringLiteral("root"));
    QCOMPARE(tableModel(window)->mode(), NodeTableModel::ChildrenMode);
    QCOMPARE(tableModel(window)->rowCount(), 5);
    QCOMPARE(projectionSummary(window)->text(),
             QStringLiteral("5 item(s) · exact evidence · Logical subtree: 285 B"));
    QVERIFY(partialBanner(window)->isHidden());
    QCOMPARE(nodeTable(window)->accessibleName(), QStringLiteral("Entries in current folder"));
}

void TestMainWindow::clickableBreadcrumbSegmentsNavigate() {
    MainWindow window(nullptr, explorerRunner(ExplorerResultKind::Complete));
    window.scanPath(QStringLiteral("navigation"));
    waitForScan(window);

    QCOMPARE(breadcrumbPath(window), QStringLiteral("root"));
    activateTableRow(window, QStringLiteral("projects"));
    QTRY_COMPARE(breadcrumbPath(window), QStringLiteral("root / projects"));
    QVERIFY(upButton(window)->isEnabled());

    QToolButton* rootSegment = breadcrumbSegment(window, 0);
    QVERIFY(rootSegment != nullptr);
    QVERIFY(!rootSegment->accessibleName().trimmed().isEmpty());
    rootSegment->click();
    QTRY_COMPARE(breadcrumbPath(window), QStringLiteral("root"));
    QVERIFY(!upButton(window)->isEnabled());
    QCOMPARE(QString::fromStdString(treemap(window)->currentNode()->name),
             QStringLiteral("root"));
}

void TestMainWindow::representativeFiltersReprojectTable() {
    MainWindow window(nullptr, explorerRunner(ExplorerResultKind::Complete));
    window.scanPath(QStringLiteral("filters"));
    waitForScan(window);

    searchEdit(window)->setText(QStringLiteral("NEEDLE"));
    QTRY_COMPARE((tableRowNames(window)), (QStringList{QStringLiteral("needle.bin")}));

    searchEdit(window)->clear();
    QTRY_COMPARE(tableModel(window)->rowCount(), 5);
    typeCombo(window)->setCurrentIndex(
        typeCombo(window)->findData(static_cast<int>(diskmap::FsKind::RegularFile)));
    QTRY_COMPARE((tableRowNames(window)),
                 (QStringList{QStringLiteral("needle.bin"), QStringLiteral("other.bin"),
                              QStringLiteral("old.bin")}));

    minimumSizeEdit(window)->setText(QStringLiteral("50"));
    emit minimumSizeEdit(window)->editingFinished();
    QTRY_COMPARE((tableRowNames(window)), (QStringList{QStringLiteral("needle.bin")}));

    minimumSizeEdit(window)->clear();
    emit minimumSizeEdit(window)->editingFinished();
    typeCombo(window)->setCurrentIndex(typeCombo(window)->findData(-1));
    ageCombo(window)->setCurrentIndex(ageCombo(window)->findData(3));
    QTRY_VERIFY(tableRowNames(window).contains(QStringLiteral("needle.bin"))
               && tableRowNames(window).contains(QStringLiteral("other.bin"))
               && !tableRowNames(window).contains(QStringLiteral("old.bin")));
}

void TestMainWindow::metricExplanationTracksSelection() {
    MainWindow window(nullptr, explorerRunner(ExplorerResultKind::Complete));
    window.scanPath(QStringLiteral("metrics"));
    waitForScan(window);

    QVERIFY(metricExplanation(window)->text().contains(QStringLiteral("Logical size")));
    metricCombo(window)->setCurrentIndex(
        metricCombo(window)->findData(static_cast<int>(diskmap::SizeMetric::Allocated)));
    QTRY_VERIFY(metricExplanation(window)->text().contains(QStringLiteral("Allocated size"))
               && nodeTable(window)->horizontalHeader()->sortIndicatorSection()
                      == NodeTableModel::AllocatedColumn
               && nodeTable(window)->horizontalHeader()->sortIndicatorOrder()
                      == Qt::DescendingOrder);
    metricCombo(window)->setCurrentIndex(
        metricCombo(window)->findData(static_cast<int>(diskmap::SizeMetric::Reclaimable)));
    QTRY_VERIFY(metricExplanation(window)->text().contains(QStringLiteral("Reclaimable size"))
               && nodeTable(window)->horizontalHeader()->sortIndicatorSection()
                      == NodeTableModel::ReclaimableColumn
               && nodeTable(window)->horizontalHeader()->sortIndicatorOrder()
                      == Qt::DescendingOrder);
    QVERIFY(metricExplanation(window)->text().contains(QStringLiteral("hard-link")));
}

void TestMainWindow::largestFilesModeProjectsRegularFiles() {
    MainWindow window(nullptr, explorerRunner(ExplorerResultKind::Complete));
    window.scanPath(QStringLiteral("largest"));
    waitForScan(window);

    modeCombo(window)->setCurrentIndex(
        modeCombo(window)->findData(static_cast<int>(NodeTableModel::LargestFilesMode)));
    QTRY_VERIFY(tableModel(window)->mode() == NodeTableModel::LargestFilesMode);
    QTRY_COMPARE((tableRowNames(window)),
                 (QStringList{QStringLiteral("needle.bin"), QStringLiteral("keep.bin"),
                              QStringLiteral("other.bin"), QStringLiteral("old.bin")}));
    QVERIFY(!tableRowNames(window).contains(QStringLiteral("alias.bin")));
    QCOMPARE(nodeTable(window)->accessibleName(),
             QStringLiteral("Largest files in current subtree"));
    QVERIFY(partialBanner(window)->isHidden());
}

void TestMainWindow::incompleteProjectionShowsBanner() {
    MainWindow window(nullptr, explorerRunner(ExplorerResultKind::Incomplete));
    window.scanPath(QStringLiteral("incomplete"));
    waitForScan(window);

    QTRY_VERIFY(!partialBanner(window)->isHidden());
    QVERIFY(partialBanner(window)->text().contains(QStringLiteral("conservative evidence")));
    QVERIFY(partialBanner(window)->text().contains(QStringLiteral("incomplete subtree")));
    QVERIFY(!tableModel(window)->projectionComplete());
}

void TestMainWindow::tableActivationNavigatesToDirectory() {
    MainWindow window(nullptr, explorerRunner(ExplorerResultKind::Complete));
    window.scanPath(QStringLiteral("activation"));
    waitForScan(window);

    activateTableRow(window, QStringLiteral("projects"));
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr
               && treemap(window)->currentNode()->name == "projects");
    QCOMPARE(breadcrumbPath(window), QStringLiteral("root / projects"));
    QVERIFY(upButton(window)->isEnabled());
    QCOMPARE((tableRowNames(window)), (QStringList{QStringLiteral("deep")}));
}

void TestMainWindow::rescanPreservesDeepNavigationAndSelection() {
    MainWindow window(nullptr, explorerRunner(ExplorerResultKind::StableRescan));
    window.scanPath(QStringLiteral("rescan"));
    waitForScan(window);
    activateTableRow(window, QStringLiteral("projects"));
    QTRY_COMPARE(breadcrumbPath(window), QStringLiteral("root / projects"));
    activateTableRow(window, QStringLiteral("deep"));
    QTRY_COMPARE(breadcrumbPath(window), QStringLiteral("root / projects / deep"));

    QTableView* table = nodeTable(window);
    NodeTableModel* model = tableModel(window);
    const QModelIndex keep = tableIndexNamed(window, QStringLiteral("keep.bin"));
    QVERIFY(keep.isValid());
    const std::optional<diskmap::NodeKey> selectedKey = model->keyAt(keep.row());
    QVERIFY(selectedKey.has_value());
    table->selectionModel()->setCurrentIndex(
        keep, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QTRY_VERIFY(table->selectionModel()->hasSelection());

    rescanButton(window)->click();
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr
               && treemap(window)->currentNode()->scan_generation == 2);
    QCOMPARE(breadcrumbPath(window), QStringLiteral("root / projects / deep"));
    QCOMPARE(QString::fromStdString(treemap(window)->currentNode()->name),
             QStringLiteral("deep"));
    QVERIFY(table->selectionModel()->hasSelection());
    const QModelIndex restored = table->selectionModel()->currentIndex();
    QVERIFY(restored.isValid());
    QVERIFY(selectedKey.has_value());
    QVERIFY(model->keyAt(restored.row()).has_value());
    QCOMPARE(*model->keyAt(restored.row()), *selectedKey);
}

void TestMainWindow::rescanFallsBackWhenDirectoryDisappears() {
    MainWindow window(nullptr, explorerRunner(ExplorerResultKind::MissingDeepDirectory));
    window.scanPath(QStringLiteral("missing-deep"));
    waitForScan(window);
    activateTableRow(window, QStringLiteral("projects"));
    QTRY_COMPARE(breadcrumbPath(window), QStringLiteral("root / projects"));
    activateTableRow(window, QStringLiteral("deep"));
    QTRY_COMPARE(breadcrumbPath(window), QStringLiteral("root / projects / deep"));

    rescanButton(window)->click();
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr
               && treemap(window)->currentNode()->scan_generation == 2);
    QCOMPARE(QString::fromStdString(treemap(window)->currentNode()->name),
             QStringLiteral("projects"));
    QCOMPARE(breadcrumbPath(window), QStringLiteral("root / projects"));
    QVERIFY(!tableIndexNamed(window, QStringLiteral("deep")).isValid());
    QVERIFY(tableRowNames(window).contains(QStringLiteral("replacement.bin")));
}

void TestMainWindow::rescanFallsBackWhenDirectoryIdentityChanges() {
    MainWindow window(nullptr, explorerRunner(ExplorerResultKind::ChangedDeepIdentity));
    window.scanPath(QStringLiteral("changed-deep"));
    waitForScan(window);
    activateTableRow(window, QStringLiteral("projects"));
    QTRY_COMPARE(breadcrumbPath(window), QStringLiteral("root / projects"));
    activateTableRow(window, QStringLiteral("deep"));
    QTRY_COMPARE(breadcrumbPath(window), QStringLiteral("root / projects / deep"));

    QVERIFY(treemap(window)->currentNode() != nullptr);
    const diskmap::NodeKey oldDeepKey =
        diskmap::nodeKey(*treemap(window)->currentNode());

    rescanButton(window)->click();
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr
               && treemap(window)->currentNode()->scan_generation == 2);
    QCOMPARE(QString::fromStdString(treemap(window)->currentNode()->name),
             QStringLiteral("projects"));
    QCOMPARE(breadcrumbPath(window), QStringLiteral("root / projects"));
    const QModelIndex newDeep = tableIndexNamed(window, QStringLiteral("deep"));
    QVERIFY(newDeep.isValid());
    QVERIFY(tableModel(window)->keyAt(newDeep.row()).has_value());
    QVERIFY(*tableModel(window)->keyAt(newDeep.row()) != oldDeepKey);
}

void TestMainWindow::rescanClearsSelectionWhenEntryDisappears() {
    MainWindow window(nullptr, explorerRunner(ExplorerResultKind::MissingSelectedFile));
    window.scanPath(QStringLiteral("missing-selection"));
    waitForScan(window);
    activateTableRow(window, QStringLiteral("projects"));
    QTRY_COMPARE(breadcrumbPath(window), QStringLiteral("root / projects"));
    activateTableRow(window, QStringLiteral("deep"));
    QTRY_COMPARE(breadcrumbPath(window), QStringLiteral("root / projects / deep"));

    QTableView* table = nodeTable(window);
    NodeTableModel* model = tableModel(window);
    const QModelIndex keep = tableIndexNamed(window, QStringLiteral("keep.bin"));
    QVERIFY(keep.isValid());
    const std::optional<diskmap::NodeKey> selectedKey = model->keyAt(keep.row());
    QVERIFY(selectedKey.has_value());
    table->selectionModel()->setCurrentIndex(
        keep, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QTRY_VERIFY(table->selectionModel()->hasSelection());

    rescanButton(window)->click();
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr
               && treemap(window)->currentNode()->scan_generation == 2);
    QCOMPARE(QString::fromStdString(treemap(window)->currentNode()->name),
             QStringLiteral("deep"));
    QCOMPARE(breadcrumbPath(window), QStringLiteral("root / projects / deep"));
    QVERIFY(!tableIndexNamed(window, QStringLiteral("keep.bin")).isValid());
    QVERIFY(tableIndexNamed(window, QStringLiteral("survivor.bin")).isValid());
    QVERIFY(!table->selectionModel()->hasSelection());
    QVERIFY(!model->indexForKey(*selectedKey).isValid());
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

void TestMainWindow::selectionSurvivesTextAndMetricSortsAndModelResets() {
    MainWindow window(nullptr, explorerRunner(ExplorerResultKind::Complete));
    window.scanPath(QStringLiteral("selection-sorts"));
    waitForScan(window);

    QTableView* table = nodeTable(window);
    NodeTableModel* model = tableModel(window);
    QVERIFY(table != nullptr);
    QVERIFY(model != nullptr);
    if (table == nullptr || model == nullptr) {
        return;
    }

    const QModelIndex selected = tableIndexNamed(window, QStringLiteral("projects"));
    QVERIFY(selected.isValid());
    if (!selected.isValid()) {
        return;
    }
    const std::optional<diskmap::NodeKey> selectedKey = model->keyAt(selected.row());
    QVERIFY(selectedKey.has_value());
    if (!selectedKey.has_value()) {
        return;
    }
    table->selectionModel()->setCurrentIndex(
        selected, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QTRY_VERIFY(currentSelectionHasKey(window, *selectedKey));

    // A text sort resets the model and moves the selected row. The key, not
    // the old row number or QModelIndex, is the selection contract.
    table->sortByColumn(NodeTableModel::NameColumn, Qt::AscendingOrder);
    QTRY_VERIFY(currentSelectionHasKey(window, *selectedKey));
    QVERIFY(table->selectionModel()->currentIndex().row() != selected.row());

    // Metric sorting also causes a projection/model reset and synchronizes the
    // selected metric combo. The same exact NodeKey must still be selected.
    table->sortByColumn(NodeTableModel::AllocatedColumn, Qt::AscendingOrder);
    QTRY_VERIFY(currentSelectionHasKey(window, *selectedKey));

    // Debounced filtering exercises another reset in both directions while
    // retaining the selected row in the projected result.
    searchEdit(window)->setText(QStringLiteral("projects"));
    QTRY_COMPARE((tableRowNames(window)), (QStringList{QStringLiteral("projects")}));
    QTRY_VERIFY(currentSelectionHasKey(window, *selectedKey));
    searchEdit(window)->clear();
    QTRY_COMPARE(tableModel(window)->rowCount(), 5);
    QTRY_VERIFY(currentSelectionHasKey(window, *selectedKey));
}

void TestMainWindow::explorerControlsAreDisabledAndInertDuringRescan() {
    const auto probe = std::make_shared<RescanProbe>();
    struct ReleaseOnExit {
        std::shared_ptr<RescanProbe> probe;
        ~ReleaseOnExit() { probe->release(); }
    } releaseOnExit{probe};

    MainWindow window(nullptr, rescanRunner(probe));
    window.scanPath(QStringLiteral("active-rescan"));
    waitForScan(window);
    activateTableRow(window, QStringLiteral("projects"));
    activateTableRow(window, QStringLiteral("deep"));
    QTRY_COMPARE(breadcrumbPath(window), QStringLiteral("root / projects / deep"));

    QTableView* table = nodeTable(window);
    NodeTableModel* model = tableModel(window);
    QVERIFY(table != nullptr);
    QVERIFY(model != nullptr);
    if (table == nullptr || model == nullptr) {
        return;
    }
    const QModelIndex selected = tableIndexNamed(window, QStringLiteral("keep.bin"));
    QVERIFY(selected.isValid());
    if (!selected.isValid()) {
        return;
    }
    const std::optional<diskmap::NodeKey> selectedKey = model->keyAt(selected.row());
    QVERIFY(selectedKey.has_value());
    if (!selectedKey.has_value()) {
        return;
    }
    table->selectionModel()->setCurrentIndex(
        selected, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QTRY_VERIFY(currentSelectionHasKey(window, *selectedKey));

    const QString beforePath = breadcrumbPath(window);
    const QStringList beforeRows = tableRowNames(window);
    const FsNode* beforeNode = treemap(window)->currentNode();
    QVERIFY(beforeNode != nullptr);

    rescanButton(window)->click();
    QTRY_VERIFY(probe->isRescanStarted());

    // Every user-facing explorer control except cancellation is unavailable
    // while the old document remains visible during the active rescan. The
    // folder chooser is intentionally omitted: it is the explicit command to
    // supersede a scan with a different path, rather than an explorer control.
    const std::vector<QWidget*> controls{
        rescanButton(window), upButton(window), searchEdit(window),
        minimumSizeEdit(window), metricCombo(window), typeCombo(window), ageCombo(window),
        window.findChild<QComboBox*>(QStringLiteral("issueCombo")), modeCombo(window),
        table, treemap(window),
    };
    for (QWidget* control : controls) {
        QVERIFY(control != nullptr);
        if (control != nullptr) {
            QVERIFY2(!control->isEnabled(), qPrintable(control->objectName()));
        }
    }
    for (int index = 0; index < 3; ++index) {
        QToolButton* segment = breadcrumbSegment(window, index);
        QVERIFY(segment != nullptr);
        if (segment != nullptr) {
            QVERIFY2(!segment->isEnabled(), qPrintable(segment->objectName()));
        }
    }
    QVERIFY(cancelButton(window) != nullptr);
    QVERIFY(cancelButton(window)->isEnabled());

    // Direct signal/API attempts are still guarded, which keeps this test
    // valid for an implementation that chooses inert controls over disabling
    // them at the widget level.
    emit table->activated(selected);
    if (breadcrumbSegment(window, 0) != nullptr) {
        breadcrumbSegment(window, 0)->click();
    }
    upButton(window)->click();
    searchEdit(window)->setText(QStringLiteral("needle"));
    typeCombo(window)->setCurrentIndex(1);
    modeCombo(window)->setCurrentIndex(
        modeCombo(window)->findData(static_cast<int>(NodeTableModel::LargestFilesMode)));
    QTest::qWait(250);

    QCOMPARE(breadcrumbPath(window), beforePath);
    QCOMPARE(tableRowNames(window), beforeRows);
    QCOMPARE(treemap(window)->currentNode(), beforeNode);
    QVERIFY(currentSelectionHasKey(window, *selectedKey));

    probe->release();
    QTRY_VERIFY(probe->isRescanFinished());
    QTRY_VERIFY(treemap(window)->currentNode() != nullptr
               && treemap(window)->currentNode()->scan_generation == 2);
}

void TestMainWindow::generationMetadataMismatchPreservesVisibleDocument() {
    const std::vector<GenerationMismatchKind> mismatches{
        GenerationMismatchKind::ResultOnly,
        GenerationMismatchKind::RootOnly,
    };
    for (const GenerationMismatchKind mismatch : mismatches) {
        MainWindow window(nullptr, generationMismatchRunner(mismatch));
        window.scanPath(QStringLiteral("prior-document"));
        waitForScan(window);

        const FsNode* previousNode = treemap(window)->currentNode();
        QVERIFY(previousNode != nullptr);
        const QString previousPath = breadcrumbPath(window);
        const QStringList previousRows = tableRowNames(window);
        QVERIFY(tableModel(window)->rootNode() == previousNode);

        window.scanPath(QStringLiteral("mismatched"));
        QTRY_VERIFY(status(window)->text().contains(
            QStringLiteral("generation metadata mismatch")));

        // Neither generation field may admit a document into the view. The
        // previous shared document, model root, breadcrumb, and rows remain.
        QCOMPARE(treemap(window)->currentNode(), previousNode);
        QCOMPARE(tableModel(window)->rootNode(), previousNode);
        QCOMPARE(breadcrumbPath(window), previousPath);
        QCOMPARE(tableRowNames(window), previousRows);
        QVERIFY(!cancelButton(window)->isEnabled());
    }
}

void TestMainWindow::summariesUseSelectedMetricAndConservativeValues() {
    {
        MainWindow window(nullptr, metricOnlyUnknownRunner());
        metricCombo(window)->setCurrentIndex(
            metricCombo(window)->findData(static_cast<int>(diskmap::SizeMetric::Allocated)));
        window.scanPath(QStringLiteral("metric-summary"));
        waitForScan(window);

        QTRY_VERIFY(status(window)->text().contains(QStringLiteral("Allocated:"))
                   && status(window)->text().contains(QStringLiteral("At least")));
        QTRY_VERIFY(projectionSummary(window)->text().contains(
                        QStringLiteral("Allocated subtree: At least")));

        metricCombo(window)->setCurrentIndex(
            metricCombo(window)->findData(static_cast<int>(diskmap::SizeMetric::Reclaimable)));
        QTRY_VERIFY(status(window)->text().contains(QStringLiteral("Reclaimable:"))
                   && status(window)->text().contains(QStringLiteral("At least")));
        QTRY_VERIFY(projectionSummary(window)->text().contains(
            QStringLiteral("Reclaimable subtree: At least")));
    }

    // An unknown zero cannot be rendered as an exact "0 B". It is explicitly
    // labeled Unknown in both summary surfaces.
    MainWindow window(nullptr, metricOnlyUnknownRunner(true));
    metricCombo(window)->setCurrentIndex(
        metricCombo(window)->findData(static_cast<int>(diskmap::SizeMetric::Allocated)));
    window.scanPath(QStringLiteral("metric-summary-zero"));
    waitForScan(window);
    QTRY_VERIFY(status(window)->text().contains(QStringLiteral("Allocated:"))
               && status(window)->text().contains(QStringLiteral("Unknown")));
    QTRY_VERIFY(projectionSummary(window)->text().contains(
        QStringLiteral("Allocated subtree: Unknown")));
}

void TestMainWindow::metricOnlyUncertaintyUsesGenericReason() {
    MainWindow window(nullptr, metricOnlyUnknownRunner());
    window.scanPath(QStringLiteral("metric-only-uncertainty"));
    waitForScan(window);

    metricCombo(window)->setCurrentIndex(
        metricCombo(window)->findData(static_cast<int>(diskmap::SizeMetric::Allocated)));
    QTRY_VERIFY(!partialBanner(window)->isHidden());
    QTRY_VERIFY(projectionSummary(window)->text().contains(
        QStringLiteral("Allocated subtree: At least")));

    const QString banner = partialBanner(window)->text().toLower();
    QVERIFY(banner.contains(QStringLiteral("selected metric"))
            || banner.contains(QStringLiteral("not known exactly")));
    QVERIFY(!banner.contains(QStringLiteral("metadata unknown")));
}

void TestMainWindow::regularFileRootSupportsLargestFilesActivation() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("single.bin")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray contents(4096, 'r');
    QCOMPARE(file.write(contents), contents.size());
    file.close();

    MainWindow window;
    window.scanPath(file.fileName());
    waitForScan(window);

    TreemapWidget* view = treemap(window);
    QVERIFY(view != nullptr);
    QVERIFY(view->currentNode() != nullptr);
    if (view == nullptr || view->currentNode() == nullptr) {
        return;
    }
    const FsNode* root = view->currentNode();
    QVERIFY(!root->is_dir);
    const QString rootName = QString::fromStdString(root->name);

    modeCombo(window)->setCurrentIndex(
        modeCombo(window)->findData(static_cast<int>(NodeTableModel::LargestFilesMode)));
    QTRY_VERIFY(tableModel(window)->mode() == NodeTableModel::LargestFilesMode);
    QTRY_COMPARE((tableRowNames(window)), (QStringList{rootName}));

    const diskmap::NodeKey rootKey = diskmap::nodeKey(*root);
    activateTableRow(window, rootName);
    QTRY_VERIFY(view->currentNode() != nullptr);
    QCOMPARE(view->currentNode(), root);
    QCOMPARE(diskmap::nodeKey(*view->currentNode()), rootKey);
    QVERIFY(currentSelectionHasKey(window, rootKey));
}

void TestMainWindow::destructionWithLateProgressCallbackIsSafe() {
    const auto probe = std::make_shared<LateProgressProbe>();
    LateProgressRelease release(probe);
    {
        MainWindow window(nullptr, lateProgressRunner(probe));
        window.scanPath(QStringLiteral("late-progress"));
        QTRY_VERIFY(probe->isStarted());
    }

    // The worker invokes its captured ProgressFn after MainWindow has been
    // destroyed. Joining the releaser proves the worker can finish without a
    // dangling queued callback, crash, or lifecycle deadlock.
    QTRY_VERIFY(probe->isFinished());
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
    const QString rootTrail = breadcrumbPath(window);

    // nodeActivated is the widget's public navigation contract. Emitting it
    // keeps this test independent of squarified pixel geometry and widget
    // child order while still going through MainWindow's connected slot.
    emit view->nodeActivated(diskmap::nodeKey(*child));

    QVERIFY(view->currentNode() != nullptr);
    QCOMPARE(QString::fromStdString(view->currentNode()->name),
             QString::fromStdString(child->name));
    QVERIFY(breadcrumbPath(window) != rootTrail);
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
    const QString rootTrail = breadcrumbPath(window);
    emit view->nodeActivated(diskmap::nodeKey(root->children.front()));
    QVERIFY(upButton(window)->isEnabled());

    // The button is found through its stable objectName and clicked through
    // its public API, rather than invoking MainWindow's private slot.
    upButton(window)->click();
    QCOMPARE(view->currentNode(), root);
    QCOMPARE(breadcrumbPath(window), rootTrail);
    QVERIFY(!upButton(window)->isEnabled());

    // A second click at the root is a safe no-op: the trail cannot pop past
    // the scan root and the view remains usable.
    upButton(window)->click();
    QCOMPARE(view->currentNode(), root);
    QCOMPARE(breadcrumbPath(window), rootTrail);
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

    emit view->nodeActivated(diskmap::nodeKey(*directoryNode));
    const QString directoryTrail = breadcrumbPath(window);
    emit view->nodeActivated(diskmap::nodeKey(*leaf));

    QVERIFY(view->currentNode() != nullptr);
    QCOMPARE(QString::fromStdString(view->currentNode()->name),
             QString::fromStdString(directoryNode->name));
    QCOMPARE(breadcrumbPath(window), directoryTrail);
}

void TestMainWindow::cleanupStagingIsReviewableAndUndoable() {
    MainWindow window(nullptr, explorerRunner(ExplorerResultKind::Complete));
    window.scanPath(QStringLiteral("cleanup-review"));
    waitForScan(window);

    selectTableRows(window,
                    {QStringLiteral("needle.bin"), QStringLiteral("other.bin")});
    QPushButton* stage = cleanupButton(window, "stageCleanupButton");
    QPushButton* undo = cleanupButton(window, "undoCleanupButton");
    QPushButton* redo = cleanupButton(window, "redoCleanupButton");
    QVERIFY(stage != nullptr);
    QVERIFY(stage->isEnabled());
    stage->click();

    QTableWidget* review = cleanupReview(window);
    QVERIFY(review != nullptr);
    QCOMPARE(review->rowCount(), 2);
    QCOMPARE(review->item(0, 0)->text(), QStringLiteral("Ready"));
    QVERIFY(cleanupSummary(window)->text().contains(QStringLiteral("2 ready")));
    QVERIFY(cleanupSummary(window)->text().contains(QStringLiteral("96 B")));
    QVERIFY(cleanupSummary(window)->text().contains(QStringLiteral("nothing has moved")));

    QVERIFY(undo != nullptr);
    QVERIFY(undo->isEnabled());
    undo->click();
    QCOMPARE(review->rowCount(), 0);
    QCOMPARE(cleanupSummary(window)->text(), QStringLiteral("Dry run: 0 ready, 0 rejected · reclaimable 0 B · nothing has moved"));

    QVERIFY(redo != nullptr);
    QVERIFY(redo->isEnabled());
    redo->click();
    QCOMPARE(review->rowCount(), 2);
}

void TestMainWindow::cleanupAuditKeepsOnlyRealRestoreTokens() {
    bool confirmed = false;
    std::size_t movedTargets = 0;
    std::string restoredToken;
    MainWindow::CleanupServices services;
    services.confirm = [&confirmed](const diskmap::CleanupPlan&) {
        confirmed = true;
        return true;
    };
    services.move = [&movedTargets](const diskmap::CleanupPlan& plan) {
        movedTargets = plan.targets.size();
        std::vector<diskmap::TrashReceipt> receipts;
        for (std::size_t index = 0; index < plan.targets.size(); ++index) {
            diskmap::TrashReceipt receipt;
            receipt.original_path = plan.targets[index].path;
            if (index == 0) {
                receipt.status = diskmap::TrashStatus::Moved;
                receipt.restore_token = "opaque-token";
                receipt.message = "moved safely";
            } else {
                receipt.status = diskmap::TrashStatus::RevalidationFailed;
                receipt.message = "identity changed";
            }
            receipts.push_back(std::move(receipt));
        }
        return receipts;
    };
    services.restore = [&restoredToken](const std::string& token) {
        restoredToken = token;
        diskmap::TrashReceipt receipt;
        receipt.status = diskmap::TrashStatus::Restored;
        receipt.original_path = "/virtual/cleanup-audit/needle.bin";
        receipt.message = "restored safely";
        return receipt;
    };

    MainWindow window(nullptr, explorerRunner(ExplorerResultKind::Complete),
                      std::move(services));
    window.scanPath(QStringLiteral("cleanup-audit"));
    waitForScan(window);
    selectTableRows(window,
                    {QStringLiteral("needle.bin"), QStringLiteral("other.bin")});
    cleanupButton(window, "stageCleanupButton")->click();
    cleanupButton(window, "executeCleanupButton")->click();

    QTRY_VERIFY(rescanButton(window)->isEnabled());
    QVERIFY(confirmed);
    QCOMPARE(movedTargets, std::size_t{2});
    QTableWidget* audit = cleanupAudit(window);
    QVERIFY(audit != nullptr);
    QCOMPARE(audit->rowCount(), 2);
    QCOMPARE(audit->item(0, 0)->text(), QStringLiteral("moved"));
    QCOMPARE(audit->item(1, 0)->text(), QStringLiteral("revalidation-failed"));
    QComboBox* tokens = restoreTokens(window);
    QVERIFY(tokens != nullptr);
    QCOMPARE(tokens->count(), 1);
    QCOMPARE(tokens->itemData(0).toString(), QStringLiteral("opaque-token"));
    QCOMPARE(cleanupReview(window)->rowCount(), 0);

    cleanupButton(window, "restoreTrashButton")->click();
    QTRY_VERIFY(rescanButton(window)->isEnabled());
    QCOMPARE(QString::fromStdString(restoredToken), QStringLiteral("opaque-token"));
    QCOMPARE(audit->rowCount(), 3);
    QCOMPARE(audit->item(2, 0)->text(), QStringLiteral("restored"));
    QCOMPARE(tokens->count(), 0);
}

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
