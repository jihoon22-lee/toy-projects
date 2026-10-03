#include "adapter.hpp"
#include "tracelens/version.hpp"
#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <memory>
using namespace tracelens;
namespace {
struct Job {
  std::atomic_bool cancel{false};
  std::atomic<uint64_t> bytes{0}, total{0};
};
struct Loaded {
  Report report;
  std::vector<Event> rows;
  QString error;
  uint64_t matching = 0;
};
class Window : public QMainWindow {
  std::vector<std::string> files;
  Report report;
  std::vector<Event> rows;
  std::shared_ptr<Job> active;
  uint64_t generation = 0, evidence_generation = 0;
  bool ready = false;
  int smoke = 0;
  QTreeWidget *processes = new QTreeWidget;
  QTableWidget *calls = new QTableWidget;
  QTableWidget *slow = new QTableWidget;
  QTreeWidget *aggregates = new QTreeWidget;
  QTextBrowser *summary = new QTextBrowser;
  QTextBrowser *comparison = new QTextBrowser;
  QPlainTextEdit *evidence = new QPlainTextEdit;
  QLineEdit *pid = new QLineEdit, *syscall = new QLineEdit, *error = new QLineEdit,
            *duration = new QLineEdit, *path = new QLineEdit;
  QProgressBar *progress = new QProgressBar;
  QLabel *state = new QLabel("Open explicitly selected saved strace files.");
  static void table(QTableWidget *t) {
    t->setColumnCount(7);
    t->setHorizontalHeaderLabels({"TID:generation", "Syscall / event", "Errno", "Duration (ns)",
                                  "Observed path", "Source:line", "Return"});
    t->horizontalHeader()->setStretchLastSection(true);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
  }
  void fill(QTableWidget *t, const std::vector<Event> &events) {
    t->setRowCount(int(events.size()));
    for (size_t i = 0; i < events.size(); ++i) {
      auto &e = events[i];
      QStringList cols{text(process_key(e.tid, e.generation)),
                       text(e.syscall.empty() ? e.kind : e.syscall),
                       text(e.error),
                       e.duration_ns ? QString::number(*e.duration_ns) : "unavailable",
                       e.paths.empty() ? "" : text(e.paths[0]),
                       QString::number(e.start.source) + ":" + QString::number(e.start.line),
                       text(e.result)};
      for (int j = 0; j < cols.size(); ++j)
        t->setItem(int(i), j, new QTableWidgetItem(cols[j]));
    }
    t->resizeColumnsToContents();
  }
  void show_evidence(const Event &event) {
    auto token = generation;
    auto selection = ++evidence_generation;
    auto sources = report.sources;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, token, selection] {
      if (token == generation && selection == evidence_generation)
        evidence->setPlainText(watcher->result());
      watcher->deleteLater();
    });
    evidence->setPlainText("Verifying exact source content…");
    watcher->setFuture(QtConcurrent::run([sources, event] {
      try {
        QString out = "Original bytes (C-escaped; non-ASCII bytes remain reversible)\n";
        auto show = [&](const Evidence &e) {
          if (e.source >= sources.size())
            throw std::runtime_error("invalid source reference");
          return QString::fromStdString(sources[e.source].path) + ":" + QString::number(e.line) +
                 " @ byte " + QString::number(e.offset) + "\n" +
                 QString::fromStdString(source_excerpt(sources[e.source], e)) + "\n";
        };
        out += show(event.start);
        if (event.end)
          out += "\nResumed evidence\n" + show(*event.end);
        if (!event.diagnostic.empty())
          out += "\nDiagnostic: " + text(event.diagnostic);
        return out;
      } catch (const std::exception &e) {
        return QString("Evidence unavailable: ") + e.what();
      }
    }));
  }
  void load() {
    if (files.empty())
      return;
    if (active)
      active->cancel = true;
    active = std::make_shared<Job>();
    auto job = active;
    auto token = ++generation;
    ready = false;
    state->setText("Analyzing saved input…");
    progress->setValue(0);
    auto selected = files;
    QString fp = pid->text(), fs = syscall->text(), fe = error->text(), fd = duration->text(),
            fx = path->text();
    bool valid;
    uint64_t min = fd.toULongLong(&valid);
    if (!fd.isEmpty() && !valid) {
      state->setText("Minimum duration must be unsigned nanoseconds.");
      return;
    }
    uint64_t tid = fp.toULongLong(&valid);
    if (!fp.isEmpty() && !valid) {
      state->setText("TID must be an unsigned number.");
      return;
    }
    auto *watcher = new QFutureWatcher<Loaded>(this);
    connect(watcher, &QFutureWatcher<Loaded>::finished, this, [this, watcher, token] {
      auto result = watcher->result();
      watcher->deleteLater();
      if (token != generation)
        return;
      if (!result.error.isEmpty()) {
        state->setText(result.error);
        if (smoke)
          qApp->exit(1);
        return;
      }
      report = std::move(result.report);
      rows = std::move(result.rows);
      ready = true;
      progress->setValue(100);
      state->setText(QString("%1 calls · %2 without duration · %3 of %4 matching events shown · %5")
                         .arg(report.calls)
                         .arg(report.missing_duration)
                         .arg(rows.size())
                         .arg(result.matching)
                         .arg(report.partial ? "PARTIAL evidence" : "complete observed input"));
      render();
      if (smoke == 1) {
        if (report.calls != 7) {
          qApp->exit(2);
          return;
        }
        smoke = 2;
        syscall->setText("__no_such_call__");
        load();
        load();
      } else if (smoke == 2) {
        if (!rows.empty()) {
          qApp->exit(3);
          return;
        }
        smoke = 3;
        syscall->clear();
        load();
      } else if (smoke == 3) {
        if (rows.empty()) {
          qApp->exit(4);
          return;
        }
        calls->selectRow(0);
        QTimer::singleShot(
            300, this, [this] { qApp->exit(evidence->toPlainText().contains("openat") ? 0 : 5); });
      }
    });
    watcher->setFuture(QtConcurrent::run([selected, job, fp, fs, fe, fd, fx, min, tid] {
      Loaded out;
      try {
        Limits limits;
        limits.retained = 0;
        out.report = analyze(
            selected, limits,
            [&](const Event &e) {
              if (!fp.isEmpty() && e.tid != tid)
                return;
              if (!fs.isEmpty() && !text(e.syscall).contains(fs))
                return;
              if (!fe.isEmpty() && text(e.error) != fe)
                return;
              if (!fd.isEmpty() && (!e.duration_ns || *e.duration_ns < min))
                return;
              if (!fx.isEmpty()) {
                bool found = false;
                for (auto &p : e.paths)
                  if (text(p).contains(fx))
                    found = true;
                if (!found)
                  return;
              }
              ++out.matching;
              if (out.rows.size() < 5000)
                out.rows.push_back(e);
            },
            &job->cancel,
            [job](uint64_t n, uint64_t total) {
              job->bytes = n;
              job->total = total;
            });
      } catch (const std::exception &e) {
        out.error = e.what();
      }
      return out;
    }));
  }
  void render() {
    fill(calls, rows);
    fill(slow, report.slow);
    QString content = "<h2>Observed evidence</h2><p>" + QString::number(report.sources.size()) +
                      " sources; " + QString::number(report.calls) + " calls; " +
                      QString::number(report.unknown) + " unknown lines; " +
                      QString::number(report.incomplete_calls) +
                      " unfinished calls.</p><p>Duration totals include explicit strace -T "
                      "evidence only. Concurrent syscall time is not wall-clock elapsed time.</p>";
    for (auto &reason : report.reasons)
      content += "<p>Partial: " + text(reason).toHtmlEscaped() + "</p>";
    content += "<p>" + QString::number(report.diagnostic_count) + " diagnostics (showing up to " +
               QString::number(report.limits.diagnostics) + ").</p>";
    for (auto &d : report.diagnostics)
      content += "<p>" + QString::number(d.evidence.source) + ":" +
                 QString::number(d.evidence.line) + " — " + text(d.message).toHtmlEscaped() +
                 "</p>";
    summary->setHtml(content);
    processes->clear();
    std::map<std::string, QTreeWidgetItem *> items;
    for (auto &[key, p] : report.processes) {
      auto *item =
          new QTreeWidgetItem(QStringList{text(key), text(p.relation), QString::number(p.calls)});
      item->setData(0, Qt::UserRole, QString::number(p.tid));
      items[key] = item;
    }
    for (auto &[key, p] : report.processes) {
      auto *item = items[key];
      auto it = items.find(p.parent);
      bool cycle = false;
      if (it != items.end()) {
        auto *ancestor = it->second;
        while (ancestor) {
          if (ancestor == item) {
            cycle = true;
            break;
          }
          ancestor = ancestor->parent();
        }
      }
      if (it != items.end() && !cycle)
        it->second->addChild(item);
      else
        processes->addTopLevelItem(item);
    }
    processes->expandToDepth(1);
    aggregates->clear();
    auto axis = [&](QString label, const std::map<std::string, Stats> &stats) {
      auto *group = new QTreeWidgetItem(aggregates, {label});
      for (auto &[key, s] : stats)
        new QTreeWidgetItem(group,
                            {text(key), QString::number(s.count), QString::number(s.errors),
                             QString::number(s.known_duration), QString::number(s.total_ns)});
    };
    axis("Syscalls", report.syscalls);
    axis("Errno", report.errors);
    axis("Observed paths (unresolved)", report.paths);
    aggregates->expandToDepth(0);
  }

public:
  Window(const QStringList &initial, bool test) : smoke(test ? 1 : 0) {
    setWindowTitle("TraceLens — saved syscall evidence");
    resize(1280, 800);
    auto *toolbar = addToolBar("Trace actions");
    auto *open = toolbar->addAction("Open traces…");
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, [this] {
      auto names = QFileDialog::getOpenFileNames(
          this, "Open saved strace files (select split files explicitly)");
      if (names.empty())
        return;
      files.clear();
      for (auto &name : names)
        files.push_back(name.toStdString());
      load();
    });
    auto *save = toolbar->addAction("Save snapshot…");
    save->setShortcut(QKeySequence::Save);
    connect(save, &QAction::triggered, this, [this] {
      if (!ready)
        return;
      auto name = QFileDialog::getSaveFileName(this, "Save snapshot", {}, "JSON (*.json)");
      if (name.isEmpty())
        return;
      try {
        save_atomic(name, QJsonDocument(snapshot_json(report)).toJson(), files);
      } catch (const std::exception &e) {
        QMessageBox::warning(this, "Cannot save snapshot", e.what());
      }
    });
    auto *compare = toolbar->addAction("Compare snapshot…");
    compare->setShortcut(QKeySequence("Ctrl+D"));
    connect(compare, &QAction::triggered, this, [this] {
      if (!ready)
        return;
      auto file =
          QFileDialog::getOpenFileName(this, "Choose baseline snapshot", {}, "JSON (*.json)");
      if (file.isEmpty())
        return;
      try {
        auto diff = diff_json(read_snapshot(file), snapshot_json(report));
        comparison->setPlainText(QJsonDocument(diff).toJson());
      } catch (const std::exception &e) {
        comparison->setPlainText(e.what());
      }
    });
    auto *cancel = toolbar->addAction("Cancel");
    cancel->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(cancel, &QAction::triggered, this, [this] {
      if (active)
        active->cancel = true;
    });
    progress->setRange(0, 100);
    progress->setMaximumWidth(180);
    toolbar->addWidget(progress);
    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    auto *filters = new QHBoxLayout;
    auto field = [&](QString label, QLineEdit *edit, QString hint) {
      auto *box = new QVBoxLayout;
      box->addWidget(new QLabel(label));
      edit->setPlaceholderText(hint);
      edit->setAccessibleName(label);
      box->addWidget(edit);
      filters->addLayout(box);
      connect(edit, &QLineEdit::returnPressed, this, [this] { load(); });
    };
    field("PID / TID", pid, "all");
    field("Syscall contains", syscall, "all");
    field("Errno", error, "e.g. ENOENT");
    field("Minimum duration (ns)", duration, "explicit -T only");
    field("Observed path contains", path, "unresolved path");
    auto *apply = new QPushButton("Apply filters");
    filters->addWidget(apply);
    connect(apply, &QPushButton::clicked, this, [this] { load(); });
    layout->addLayout(filters);
    layout->addWidget(state);
    auto *vertical = new QSplitter(Qt::Vertical);
    auto *horizontal = new QSplitter;
    processes->setHeaderLabels({"TID:generation", "Relation", "Calls"});
    processes->setMinimumWidth(240);
    horizontal->addWidget(processes);
    auto *tabs = new QTabWidget;
    auto *overview = new QWidget;
    auto *ol = new QVBoxLayout(overview);
    ol->addWidget(summary, 1);
    ol->addWidget(new QLabel("Slow calls — explicit durations only; bounded top 1,000"));
    table(slow);
    ol->addWidget(slow, 2);
    tabs->addTab(overview, "Summary / slow calls");
    table(calls);
    tabs->addTab(calls, "Calls / events");
    aggregates->setHeaderLabels({"Key", "Calls", "Errors", "Known durations", "Total ns"});
    tabs->addTab(aggregates, "Errors / paths");
    comparison->setPlainText(
        "Open a baseline snapshot with Compare snapshot. Deltas compare syscalls, errno and exact "
        "observed paths; numeric PIDs are never matched across runs.");
    tabs->addTab(comparison, "Run comparison");
    horizontal->addWidget(tabs);
    horizontal->setStretchFactor(1, 1);
    vertical->addWidget(horizontal);
    evidence->setReadOnly(true);
    evidence->setPlaceholderText("Select a call to verify and display its original source bytes.");
    evidence->setAccessibleName("Original source evidence");
    vertical->addWidget(evidence);
    vertical->setSizes({520, 180});
    layout->addWidget(vertical);
    setCentralWidget(central);
    statusBar()->showMessage("Saved Linux strace only · no tracing or network · table capped at "
                             "5,000 matches; filters rescan all budgeted input");
    connect(calls, &QTableWidget::itemSelectionChanged, this, [this] {
      auto i = calls->currentRow();
      if (i >= 0 && size_t(i) < rows.size())
        show_evidence(rows[size_t(i)]);
    });
    connect(slow, &QTableWidget::itemSelectionChanged, this, [this] {
      auto i = slow->currentRow();
      if (i >= 0 && size_t(i) < report.slow.size())
        show_evidence(report.slow[size_t(i)]);
    });
    connect(processes, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
      pid->setText(item->data(0, Qt::UserRole).toString());
      load();
    });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
      if (active && !ready && active->total)
        progress->setValue(int(100.0 * active->bytes / active->total));
    });
    timer->start(100);
    for (auto &file : initial)
      files.push_back(file.toStdString());
    if (!files.empty())
      QTimer::singleShot(0, this, [this] { load(); });
  }
  ~Window() {
    if (active)
      active->cancel = true;
  }
};
} // namespace
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setApplicationName("TraceLens");
  app.setApplicationVersion(TRACELENS_VERSION);
  QThreadPool::globalInstance()->setMaxThreadCount(2);
  auto args = app.arguments();
  args.removeFirst();
  bool smoke = args.removeAll("--smoke-test") > 0;
  if (args.contains("--help")) {
    qInfo("Usage: tracelens-gui [saved-strace-files...]\nCtrl+O open, Ctrl+S snapshot, Ctrl+D "
          "compare, Escape cancel.");
    return 0;
  }
  Window window(args, smoke);
  window.show();
  if (smoke)
    QTimer::singleShot(20000, &app, [] { qApp->exit(10); });
  return app.exec();
}
