#pragma once

#include <QMainWindow>
#include <QString>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "loglens/filter_expr.hpp"
#include "loglens/gui/log_load_worker.hpp"
#include "loglens/log_parser.hpp"
#include "loglens/persistence.hpp"
#include "loglens/ring_buffer.hpp"
#include "loglens/triage.hpp"
#include "loglens/window_analysis.hpp"

class LogModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QTableView;
class QThread;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class TimelineWidget;

struct MainWindowOptions {
    std::size_t recordCapacity = loglens::kDefaultRecordCapacity;
    std::size_t sourceChunkBytes = loglens::kDefaultSourceChunkBytes;
    // Empty paths use the per-user QStandardPaths app-config location. Tests
    // and embedders can inject file paths for deterministic stores.
    QString sourceProfilesPath;
    QString savedQueriesPath;
    QString triagePath;
};

// Loads a log file, shows it filtered, and draws a level histogram over time.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr, MainWindowOptions options = {});
    ~MainWindow() override;

    // Opens a file without the dialog, so `loglens-gui <path>` works and the
    // load path can be exercised headlessly.
    void openPath(const QString& path);
    void openPath(const QString& path, loglens::InitialLoadMode mode,
                  std::size_t tailRecords);
    // Deterministic, lossless JSON export seam used by the button and tests.
    bool exportSelectedRows(const QString& path);

signals:
    void startLoadRequested(loglens::LoadRequest request);
    void pollRequested(quint64 jobId);
    void acknowledgeRequested(quint64 jobId, quint64 sequence);
    void loadProgress(qulonglong seen, qulonglong retained, bool initialComplete,
                      QString error);

private slots:
    void chooseFile();
    void applyFilter();
    void applySourceProfile();
    void selectSourceProfile(int index);
    void saveSourceProfile();
    void applySavedQuery();
    void saveSavedQuery();
    void pollSource();
    void setFollowing(bool following);
    void handleLoadBatch(const loglens::LoadBatch& batch);
    void selectHighlightRule(int index);
    void saveHighlightRule();
    void deleteHighlightRule();
    void moveHighlightRuleUp();
    void moveHighlightRuleDown();
    void updateHighlightPreview();
    void refreshRecordDetail();
    void saveRecordTriage();
    void chooseExportPath();
    void selectTimelineRange(qulonglong beginMs, qulonglong endMs);
    void clearTimelineRange();
    void setBaselineWindow();
    void setComparisonWindow();
    void runWindowAnalysis();
    void navigateAnalysisItem(QTreeWidgetItem* item, int column);

private:
    enum class FollowState { Stopped, Following, WaitingRetry };

    LogModel* model_ = nullptr;
    QTableView* table_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QComboBox* loadMode_ = nullptr;
    QSpinBox* tailRecords_ = nullptr;
    QComboBox* sourceProfile_ = nullptr;
    QComboBox* sourceFormat_ = nullptr;
    QComboBox* multilinePolicy_ = nullptr;
    QSpinBox* maxRecordBytes_ = nullptr;
    QComboBox* savedQuery_ = nullptr;
    QLabel* status_ = nullptr;
    TimelineWidget* timeline_ = nullptr;
    std::optional<loglens::Filter> filter_;
    std::vector<loglens::SourceProfile> sourceProfiles_;
    std::vector<loglens::SavedQuery> savedQueries_;
    QString sourceProfilesPath_;
    QString savedQueriesPath_;
    QString triagePath_;
    bool sourceProfilesPathIsDefault_ = false;
    bool savedQueriesPathIsDefault_ = false;
    bool triagePathIsDefault_ = false;
    QString currentPath_;
    QThread* loaderThread_ = nullptr;
    loglens::LogLoadWorker* loader_ = nullptr;
    QTimer* pollTimer_ = nullptr;
    QTimer* timelineTimer_ = nullptr;
    QCheckBox* followBox_ = nullptr;
    FollowState followState_ = FollowState::Stopped;
    std::size_t retryAttempts_ = 0;
    bool autoScroll_ = true;
    bool backlog_pending_ = false;
    bool source_active_ = false;
    std::size_t source_chunk_bytes_ = loglens::kDefaultSourceChunkBytes;
    std::size_t record_capacity_ = loglens::kDefaultRecordCapacity;
    quint64 active_job_ = 0;
    quint64 expected_sequence_ = 0;
    loglens::TriageState triageState_;
    QComboBox* highlightRule_ = nullptr;
    QLineEdit* highlightPattern_ = nullptr;
    QLineEdit* highlightStyle_ = nullptr;
    QCheckBox* highlightWholeLine_ = nullptr;
    QSpinBox* highlightPriority_ = nullptr;
    QLabel* highlightPreview_ = nullptr;
    QPlainTextEdit* recordDetail_ = nullptr;
    QCheckBox* bookmarkBox_ = nullptr;
    QLineEdit* annotationEdit_ = nullptr;
    QLabel* selectedWindowLabel_ = nullptr;
    QLabel* baselineWindowLabel_ = nullptr;
    QLabel* comparisonWindowLabel_ = nullptr;
    QTreeWidget* analysisTree_ = nullptr;
    std::optional<loglens::TimeWindow> selectedWindow_;
    std::optional<loglens::TimeWindow> baselineWindow_;
    std::optional<loglens::TimeWindow> comparisonWindow_;

    void refreshTimeline();
    void scheduleTimelineRefresh();
    void updateStatus(const QString& extra);
    void applyDeltas(const std::vector<loglens::RecordDelta>& deltas);
    void handleLoadError(const loglens::LoadBatch& batch);
    loglens::InitialLoadMode selectedLoadMode() const;
    void loadPersistenceState();
    void rebuildSourceProfiles(const QString& selectedName = QString());
    void rebuildSavedQueries(const QString& selectedName = QString());
    void setProfileControls(const loglens::SourceProfile& profile);
    loglens::SourceProfile profileFromControls() const;
    loglens::Format selectedFormat() const;
    loglens::MultilinePolicy selectedMultilinePolicy() const;
    bool applyFilterText(const QString& text, const QString& successMessage = QString());
    void showPersistenceError(const QString& action,
                              const loglens::PersistenceError& error);
    bool prepareDefaultStoreDirectory(bool profiles);
    QString storePath(bool profiles) const;
    void setupInvestigationDock();
    void loadTriageWorkflow();
    void rebuildHighlightRules(const QString& selectedName = QString());
    bool writeTriageWorkflow(const loglens::TriageState& state,
                             const QString& action);
    bool prepareTriageStoreDirectory();
};
