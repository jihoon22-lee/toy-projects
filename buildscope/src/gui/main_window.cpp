#include "buildscope/main_window.hpp"

#include "buildscope/contract.hpp"
#include "ui_main_window.h"

#include <QFileDialog>
#include <QHeaderView>
#include <QTreeWidgetItem>

namespace buildscope {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui_(new Ui::MainWindow) {
    ui_->setupUi(this);
    ui_->entryTree->setHeaderLabels(
        {tr("Source"), tr("Working directory"), tr("Compiler invocation")});
    ui_->entryTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui_->entryTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui_->entryTree->header()->setStretchLastSection(true);
    connect(ui_->openButton, &QPushButton::clicked, this, &MainWindow::chooseSnapshot);
}

MainWindow::~MainWindow() = default;

bool MainWindow::loadSnapshot(const QString &path) {
    try {
        const auto snapshot = loadSnapshotFile(path);
        ui_->entryTree->clear();
        for (const auto &entry : snapshot.entries) {
            auto *item = new QTreeWidgetItem(
                {entry.file, entry.directory, invocationText(entry)});
            item->setToolTip(0, entry.file);
            item->setToolTip(1, entry.directory);
            item->setToolTip(2, invocationText(entry));
            ui_->entryTree->addTopLevelItem(item);
        }
        ui_->pathEdit->setText(path);
        ui_->statusLabel->setText(
            tr("Compilation entries: %1 · contract %2 · producer %3")
                .arg(snapshot.entries.size())
                .arg(snapshot.schemaVersion, snapshot.producerVersion));
        return true;
    } catch (const ContractError &error) {
        ui_->entryTree->clear();
        ui_->pathEdit->setText(path);
        ui_->statusLabel->setText(tr("Could not load snapshot: %1").arg(error.what()));
        return false;
    }
}

int MainWindow::entryCount() const {
    return ui_->entryTree->topLevelItemCount();
}

QString MainWindow::statusText() const {
    return ui_->statusLabel->text();
}

void MainWindow::chooseSnapshot() {
    const auto path = QFileDialog::getOpenFileName(
        this, tr("Open BuildScope snapshot"), {}, tr("JSON files (*.json);;All files (*)"));
    if (!path.isEmpty()) {
        loadSnapshot(path);
    }
}

}  // namespace buildscope
