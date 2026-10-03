#pragma once

#include <QMainWindow>
#include <QModelIndex>
#include <QStringList>

#include <memory>
#include <optional>

class QTreeWidgetItem;

namespace Ui {
class MainWindow;
}

namespace buildscope {

class CompilationEntryView;
class CompilationTreeModel;
class DiffTreeModel;
struct DiffUnit;
struct Snapshot;
struct DiffReport;
class StatusFilterProxyModel;

class MainWindow final : public QMainWindow {
    Q_OBJECT

  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool loadSnapshot(const QString &path);
    bool loadDiff(const QString &path);
    int entryCount() const;
    QString statusText() const;
    void openInputAsync(const QString &path, bool database = false);
    void openDiffAsync(const QString &path);
    void analyzeAsync(const QString &mode = QStringLiteral("estimate"),
                      const QStringList &units = {});
    bool busy() const;
    void cancelWork();
    void setEditorArguments(const QStringList &arguments);
    QStringList editorArgumentsFor(const QString &path, qsizetype line) const;
    void setRootMappings(const QStringList &specifications);

  signals:
    void workFinished(bool success);

  private slots:
    void chooseSnapshot();
    void chooseDiff();
    void showSelection(const QModelIndex &index);
    void applyFilter(const QString &text);
    void showIncludeEdge(QTreeWidgetItem *item, int column);
    void openIncludeLocation();
    void showCompilationCommand();

  private:
    void clearDetails(const QString &message);
    void setupWorkflow();
    void scheduleFilter(const QString &text);
    void applySnapshot(Snapshot snapshot, const QString &path);
    void applyDiffReport(DiffReport report, const QString &path);
    void updateBusy(bool busy);
    void showImpactAsync();
    void chooseBudgets();
    void chooseRootMapping();
    void chooseEditor();
    struct Workflow;
    std::shared_ptr<Workflow> workflow_;
    void setDiffMode(bool enabled);
    void showDiffUnit(const DiffUnit &unit, std::optional<qsizetype> selectedChange);
    void showEntry(const CompilationEntryView &view);

    std::unique_ptr<Ui::MainWindow> ui_;
    std::unique_ptr<CompilationTreeModel> model_;
    std::unique_ptr<DiffTreeModel> diffModel_;
    std::unique_ptr<StatusFilterProxyModel> proxy_;
    bool diffMode_ = false;
    QString selectedIncludePath_;
    qsizetype selectedIncludeLine_ = 0;
};

} // namespace buildscope
