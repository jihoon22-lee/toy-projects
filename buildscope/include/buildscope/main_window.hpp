#pragma once

#include <QMainWindow>
#include <QModelIndex>

#include <memory>

namespace Ui {
class MainWindow;
}

namespace buildscope {

class CompilationEntryView;
class CompilationTreeModel;
class StatusFilterProxyModel;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool loadSnapshot(const QString &path);
    int entryCount() const;
    QString statusText() const;

private slots:
    void chooseSnapshot();
    void showSelection(const QModelIndex &index);
    void applyFilter(const QString &text);

private:
    void clearDetails(const QString &message);
    void showEntry(const CompilationEntryView &view);

    std::unique_ptr<Ui::MainWindow> ui_;
    std::unique_ptr<CompilationTreeModel> model_;
    std::unique_ptr<StatusFilterProxyModel> proxy_;
};

}  // namespace buildscope
