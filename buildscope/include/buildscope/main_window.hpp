#pragma once

#include <QMainWindow>

#include <memory>

namespace Ui {
class MainWindow;
}

namespace buildscope {

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

private:
    std::unique_ptr<Ui::MainWindow> ui_;
};

}  // namespace buildscope
