#pragma once
#include "buildscope/main_window.hpp"
#include "native_analysis.hpp"
#include "native_relocation.hpp"
#include <QJsonObject>
#include <QTimer>
#include <atomic>
#include <memory>
class QComboBox;
class QPushButton;
class QProgressBar;
class QLineEdit;
class QPlainTextEdit;
namespace buildscope {
struct MainWindow::Workflow {
    struct Job {
        std::atomic_bool cancel{false};
        std::atomic_int completed{0}, total{0};
    };
    std::shared_ptr<Job> job;
    quint64 generation = 0;
    bool busy = false;
    QJsonObject raw;
    QString inputPath;
    native::AnalysisLimits limits;
    QList<native::RootMapping> mappings;
    QStringList editorArgv;
    QTimer *filterTimer = nullptr;
    QComboBox *mode = nullptr;
    QPushButton *cancel = nullptr, *analyze = nullptr, *save = nullptr;
    QProgressBar *progress = nullptr;
    QLineEdit *impactPath = nullptr;
    QPlainTextEdit *impactText = nullptr;
};
} // namespace buildscope
