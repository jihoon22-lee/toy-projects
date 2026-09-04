#include "benchmark_common.hpp"

#include "loglens/gui/log_model.hpp"
#include "loglens/gui/main_window.hpp"
#include "loglens/initial_load.hpp"
#include "loglens/ring_buffer.hpp"

#include <QAbstractItemModel>
#include <QApplication>
#include <QCheckBox>
#include <QPaintEvent>
#include <QTableView>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>

namespace {

struct Options {
    QString input;
    std::string output;
    std::size_t capacity = loglens::kDefaultRecordCapacity;
    std::size_t expected_records = 1'000'000;
    std::size_t expected_bytes = 1024U * 1024U * 1024U;
    bool valid = true;
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            options.valid = false;
            break;
        }
        const std::string name = argv[index];
        const std::string value = argv[index + 1];
        if (name == "--input") {
            options.input = QString::fromStdString(value);
        } else if (name == "--output") {
            options.output = value;
        } else {
            std::size_t parsed = 0;
            if (!loglens::benchmark::parsePositiveSize(value, parsed)) {
                options.valid = false;
            } else if (name == "--capacity") {
                options.capacity = parsed;
            } else if (name == "--expected-records") {
                options.expected_records = parsed;
            } else if (name == "--expected-bytes") {
                options.expected_bytes = parsed;
            } else {
                options.valid = false;
            }
        }
    }
    options.valid = options.valid && !options.input.isEmpty() && !options.output.empty()
                    && options.capacity <= loglens::kMaxRecordCapacity;
    return options;
}

double elapsedMilliseconds(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

class PaintTimingWindow : public MainWindow {
public:
    PaintTimingWindow(std::size_t capacity, const std::chrono::steady_clock::time_point& start,
                      std::optional<double>& firstPaint)
        : MainWindow(nullptr,
                     MainWindowOptions{capacity, loglens::kDefaultSourceChunkBytes, {}, {}}),
          start_(start), first_paint_(firstPaint) {}

protected:
    void paintEvent(QPaintEvent* event) override {
        QMainWindow::paintEvent(event);
        QTableView* table = findChild<QTableView*>(QStringLiteral("logTable"));
        if (!first_paint_ && table != nullptr && table->model() != nullptr
            && table->model()->rowCount() > 0) {
            first_paint_ = elapsedMilliseconds(start_);
        }
    }

private:
    const std::chrono::steady_clock::time_point& start_;
    std::optional<double>& first_paint_;
};

void printUsage() {
    std::cerr << "Usage: loglens-bench-gui --input PATH --output JSON --capacity N "
                 "--expected-records N --expected-bytes N\n";
}

} // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    const Options options = parseOptions(argc, argv);
    if (!options.valid) {
        printUsage();
        return 2;
    }

    loglens::benchmark::Result result;
    result.component = "gui";
    result.qt_major = QT_VERSION >> 16;
    result.capacity = options.capacity;
    result.expected_records = options.expected_records;
    std::string sizeError;
    if (!loglens::benchmark::fileSize(options.input.toStdString(), result.input_bytes,
                                      sizeError)
        || result.input_bytes != options.expected_bytes) {
        result.error = sizeError.empty() ? "input byte size does not match --expected-bytes"
                                         : sizeError;
    }

    const auto start = std::chrono::steady_clock::now();
    std::optional<double> firstResult;
    std::optional<double> firstPaint;
    bool complete = false;
    PaintTimingWindow window(options.capacity, start, firstPaint);
    QCheckBox* follow = window.findChild<QCheckBox*>(QStringLiteral("followCheckBox"));
    if (follow != nullptr) {
        follow->setChecked(false);
    }
    QTableView* table = window.findChild<QTableView*>(QStringLiteral("logTable"));
    QAbstractItemModel* model = table == nullptr ? nullptr : table->model();
    if (model == nullptr) {
        result.error = "cannot locate the production log table model";
    } else {
        QObject::connect(model, &QAbstractItemModel::rowsInserted, &window,
                         [&firstResult, &start] {
                             if (!firstResult) {
                                 firstResult = elapsedMilliseconds(start);
                             }
                         });
    }

    QTimer finishTimer;
    finishTimer.setInterval(10);
    QObject::connect(&finishTimer, &QTimer::timeout, &window, [&] {
        if (complete && firstPaint) {
            application.exit(0);
        }
    });
    finishTimer.start();

    QObject::connect(&window, &MainWindow::loadProgress, &window,
                     [&](qulonglong seen, qulonglong retained, bool initialComplete,
                         const QString& error) {
                         if (!error.isEmpty()) {
                             result.error = error.toStdString();
                             application.exit(1);
                             return;
                         }
                         result.seen_records = static_cast<std::size_t>(seen);
                         result.retained_records = static_cast<std::size_t>(retained);
                         if (initialComplete) {
                             result.load_ms = elapsedMilliseconds(start);
                             complete = true;
                             window.update();
                         }
                     });

    QTimer hardTimeout;
    hardTimeout.setSingleShot(true);
    QObject::connect(&hardTimeout, &QTimer::timeout, &window, [&] {
        result.error = "GUI benchmark exceeded the 180 second hard timeout";
        application.exit(124);
    });
    hardTimeout.start(180000);

    window.resize(1100, 700);
    window.show();
    QApplication::processEvents();
    if (result.error.empty()) {
        window.openPath(options.input, loglens::InitialLoadMode::FromStart, options.capacity);
        static_cast<void>(application.exec());
    }

    result.first_result_ms = firstResult.value_or(result.load_ms);
    result.first_paint_ms = firstPaint;
    result.dropped_records = result.seen_records >= result.retained_records
                                 ? result.seen_records - result.retained_records
                                 : 0;
    if (auto* logModel = qobject_cast<LogModel*>(model)) {
        result.oldest_line = logModel->oldestLine().value_or(0);
        result.newest_line = logModel->newestLine().value_or(0);
    }
    const std::size_t expectedRetained = std::min(options.capacity, options.expected_records);
    result.correctness = result.error.empty() && complete && firstPaint
                         && result.seen_records >= result.retained_records
                         && result.seen_records == options.expected_records
                         && result.retained_records == expectedRetained
                         && result.dropped_records == options.expected_records - expectedRetained
                         && result.oldest_line == options.expected_records - expectedRetained + 1
                         && result.newest_line == options.expected_records;
    if (!result.correctness && result.error.empty()) {
        result.error = "GUI model window does not match the benchmark contract";
    }
    result.peak_rss_mib = loglens::benchmark::peakRssMiB(result.rss_source);

    std::string writeError;
    if (!loglens::benchmark::writeResult(options.output, result, writeError)) {
        std::cerr << writeError << '\n';
        return 2;
    }
    std::cout << "gui load_ms=" << result.load_ms << " first_paint_ms="
              << result.first_paint_ms.value_or(0.0) << " records=" << result.seen_records
              << " peak_rss_mib=" << result.peak_rss_mib << '\n';
    if (!result.correctness) {
        std::cerr << result.error << '\n';
        return 1;
    }
    return 0;
}
