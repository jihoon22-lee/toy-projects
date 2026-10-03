#include "adapter.hpp"
#include "tracelens/version.hpp"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
using namespace tracelens;
namespace {
std::atomic_bool cancelled{false};
void cancel_handler(int) { cancelled.store(true); }
uint64_t positive(const QString &s, bool zero = false) {
  bool ok;
  auto n = s.toULongLong(&ok);
  if (!ok || (!zero && !n) || s.startsWith('-'))
    throw std::runtime_error("invalid unsigned numeric option");
  return n;
}
} // namespace
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName("tracelens");
  QCoreApplication::setApplicationVersion(TRACELENS_VERSION);
  QCommandLineParser p;
  p.setApplicationDescription(
      "Inspect saved Linux strace evidence; never executes traced programs.");
  p.addHelpOption();
  p.addVersionOption();
  p.addPositionalArgument("command", "inspect | events | source | diff");
  p.addPositionalArgument("files", "Explicit source files (diff takes two snapshots)", "files...");
  auto option = [&](QString name, QString description, QString value = QString(),
                    QString def = QString()) {
    p.addOption(QCommandLineOption(name, description, value, def));
  };
  option("format", "inspect output: text or json", "format", "text");
  option("output", "Atomic output file (default stdout)", "path");
  option("strict", "inspect/events/diff: exit 3 when evidence is partial");
  option("max-bytes", "Input byte budget", "N", "1073741824");
  option("max-events", "Logical event budget", "N", "1000000");
  option("max-ms", "Analysis time budget; zero disables", "N", "0");
  option("max-line-bytes", "Physical line byte limit", "N", "1048576");
  option("max-nesting", "Argument nesting limit", "N", "64");
  option("max-paths", "Distinct path limit", "N", "100000");
  option("max-processes", "Process generation limit", "N", "65536");
  option("max-pending", "Unfinished call limit", "N", "65536");
  option("max-diagnostics", "Retained diagnostic limit", "N", "1000");
  option("max-syscalls", "Syscall and errno cardinality limit", "N", "4096");
  option("top", "Slow-call evidence limit; zero disables", "N", "1000");
  option("pid", "events: filter numeric PID/TID", "N");
  option("syscall", "events: filter exact syscall", "name");
  option("errno", "events: filter exact errno", "name");
  option("min-duration-ns", "events: filter minimum explicit duration", "N");
  option("path", "events: filter observed path substring", "text");
  option("limit", "events: maximum emitted matches (scan continues)", "N", "100");
  option("line", "source: one-based source line", "N", "1");
  option("context", "source: lines before and after (0-100)", "N", "3");
  option("source-id", "source: zero-based snapshot source index", "N", "0");
  p.process(app);
  try {
    auto pos = p.positionalArguments();
    if (pos.size() < 2)
      throw std::runtime_error("command and files required; see --help");
    auto command = pos.takeFirst();
    std::vector<std::string> files;
    for (auto &f : pos)
      files.push_back(f.toStdString());
    if (p.isSet("output"))
      check_output_alias(p.value("output"), files);
    auto deliver = [&](const QByteArray &b) {
      if (p.isSet("output"))
        save_atomic(p.value("output"), b, files);
      else {
        std::cout.write(b.constData(), b.size());
        if (!std::cout)
          throw std::runtime_error("stdout write failed");
      }
    };
    if (command == "diff") {
      if (files.size() != 2)
        throw std::runtime_error("diff requires two snapshots");
      auto a = read_snapshot(pos[0]), b = read_snapshot(pos[1]);
      auto out = diff_json(a, b);
      for (const auto &snapshot : {a, b})
        for (const auto &source : snapshot["sources"].toArray())
          files.push_back(source.toObject()["path"].toString().toStdString());
      deliver(QJsonDocument(out).toJson());
      return p.isSet("strict") && out["partial"].toBool() ? 3 : 0;
    }
    Limits limits;
    limits.retained = 0;
    limits.input_bytes = positive(p.value("max-bytes"));
    limits.events = positive(p.value("max-events"));
    limits.milliseconds = positive(p.value("max-ms"), true);
    limits.line_bytes = positive(p.value("max-line-bytes"));
    limits.nesting = positive(p.value("max-nesting"));
    limits.paths = positive(p.value("max-paths"));
    limits.processes = positive(p.value("max-processes"));
    limits.pending = positive(p.value("max-pending"));
    limits.diagnostics = positive(p.value("max-diagnostics"));
    limits.syscalls = positive(p.value("max-syscalls"));
    limits.slow = positive(p.value("top"), true);
    if (command == "source") {
      if (files.size() != 1)
        throw std::runtime_error("source accepts one trace or snapshot");
      Source src;
      if (pos[0].endsWith(".json")) {
        auto snapshot = read_snapshot(pos[0]);
        auto sources = snapshot["sources"].toArray();
        auto id = positive(p.value("source-id"), true);
        if (id >= uint64_t(sources.size()))
          throw std::runtime_error("source index out of range");
        src = source_from_json(sources[int(id)].toObject());
        files.push_back(src.path);
        if (p.isSet("output"))
          check_output_alias(p.value("output"), files);
      } else {
        auto report = analyze(files, limits);
        src = report.sources.at(0);
      }
      auto target = positive(p.value("line")), context = positive(p.value("context"), true);
      if (context > 100)
        throw std::runtime_error("context limit is 100 lines");
      if (target > src.lines)
        throw std::runtime_error("source line out of range");
      uint64_t low = target > context ? target - context : 1,
               high = std::min(src.lines, target + std::min(context, UINT64_MAX - target));
      source_excerpt(src, Evidence{0, 1, 0, 0});
      std::ifstream f(src.path, std::ios::binary);
      uint64_t line = 1, offset = 0, start = 0;
      QByteArray result;
      char c;
      while (line <= high && f.get(c)) {
        ++offset;
        if (c == '\n' || offset == src.size) {
          if (line >= low) {
            Evidence e{0, line, start, offset - start};
            result += QByteArray::number(line) + ": " +
                      QByteArray::fromStdString(source_excerpt(src, e)) + "\n";
          }
          start = offset;
          ++line;
        }
      }
      deliver(result);
      return 0;
    }
    if (command != "inspect" && command != "events")
      throw std::runtime_error("unknown command");
    if (p.value("format") != "text" && p.value("format") != "json")
      throw std::runtime_error("format must be text or json");
    std::signal(SIGINT, cancel_handler);
    std::signal(SIGTERM, cancel_handler);
    std::unique_ptr<QSaveFile> stream;
    uint64_t emitted = 0, matching = 0, max_output = positive(p.value("limit"));
    std::optional<uint64_t> pid, min_duration;
    if (p.isSet("pid"))
      pid = positive(p.value("pid"), true);
    if (p.isSet("min-duration-ns"))
      min_duration = positive(p.value("min-duration-ns"), true);
    if (command == "events" && p.isSet("output")) {
      stream = std::make_unique<QSaveFile>(p.value("output"));
      stream->setDirectWriteFallback(false);
      if (!stream->open(QIODevice::WriteOnly))
        throw std::runtime_error("cannot open atomic event output");
    }
    auto write_line = [&](const QByteArray &line) {
      if (stream) {
        if (stream->write(line) != line.size())
          throw std::runtime_error("event output write failed");
      } else {
        std::cout.write(line.constData(), line.size());
        if (!std::cout)
          throw std::runtime_error("stdout write failed");
      }
    };
    Sink sink;
    if (command == "events")
      sink = [&](const Event &e) {
        if (pid && e.tid != *pid)
          return;
        if (p.isSet("syscall") && text(e.syscall) != p.value("syscall"))
          return;
        if (p.isSet("errno") && text(e.error) != p.value("errno"))
          return;
        if (min_duration && (!e.duration_ns || *e.duration_ns < *min_duration))
          return;
        if (p.isSet("path")) {
          bool found = false;
          for (auto &path : e.paths)
            if (text(path).contains(p.value("path")))
              found = true;
          if (!found)
            return;
        }
        ++matching;
        if (emitted >= max_output)
          return;
        ++emitted;
        write_line(QJsonDocument(event_json(e)).toJson(QJsonDocument::Compact) + "\n");
      };
    auto report = analyze(files, limits, sink, &cancelled);
    auto snapshot = snapshot_json(report);
    if (command == "events") {
      QJsonObject footer{
          {"schema", "tracelens.events-end/v1"},    {"events", snapshot["events"]},
          {"matching", QString::number(matching)},  {"emitted", QString::number(emitted)},
          {"output_truncated", matching > emitted}, {"partial", report.partial},
          {"reasons", snapshot["reasons"]},         {"sources", snapshot["sources"]}};
      write_line(QJsonDocument(footer).toJson(QJsonDocument::Compact) + "\n");
      if (stream && !stream->commit())
        throw std::runtime_error("event output commit failed");
    } else if (p.value("format") == "json")
      deliver(QJsonDocument(snapshot).toJson());
    else {
      QByteArray out = QByteArray("TraceLens ") + TRACELENS_VERSION + " — saved strace evidence\n";
      out += QByteArray::number(report.calls) + " calls; " +
             QByteArray::number(report.missing_duration) + " without -T duration; " +
             (report.partial ? "PARTIAL" : "complete observed input") + "\n";
      out += "syscall\tcount\terrors\tknown durations\ttotal ns\tmax ns\n";
      for (auto &[name, s] : report.syscalls)
        out += QByteArray::fromStdString(escaped(name)) + "\t" + QByteArray::number(s.count) +
               "\t" + QByteArray::number(s.errors) + "\t" + QByteArray::number(s.known_duration) +
               "\t" + QByteArray::number(s.total_ns) + "\t" + QByteArray::number(s.max_ns) + "\n";
      for (auto &reason : report.reasons)
        out += "! " + QByteArray::fromStdString(reason) + "\n";
      deliver(out);
    }
    return report.cancelled ? 130 : p.isSet("strict") && report.partial ? 3 : 0;
  } catch (const std::exception &e) {
    std::cerr << "tracelens: " << e.what() << '\n';
    return 2;
  }
}
