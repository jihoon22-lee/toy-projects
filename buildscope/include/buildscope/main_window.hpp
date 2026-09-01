#pragma once

#include <QMainWindow>
#include <QModelIndex>

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

}  // namespace buildscope
