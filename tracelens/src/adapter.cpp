#include "adapter.hpp"
#include "tracelens/version.hpp"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <set>
#include <stdexcept>
#include <sys/stat.h>
namespace tracelens {
QString text(const std::string &v) { return QString::fromStdString(escaped(v)); }
namespace {
QString n(uint64_t v) { return QString::number(v); }
QJsonObject evidence(const Evidence &e) {
  return {{"source", int(e.source)},
          {"line", n(e.line)},
          {"offset", n(e.offset)},
          {"length", n(e.length)}};
}
QJsonObject stats(const Stats &s) {
  return {{"count", n(s.count)},
          {"errors", n(s.errors)},
          {"known_duration", n(s.known_duration)},
          {"total_ns", n(s.total_ns)},
          {"max_ns", n(s.max_ns)}};
}
QJsonObject table(const std::map<std::string, Stats> &t) {
  QJsonObject o;
  for (auto &[k, v] : t)
    o[text(k)] = stats(v);
  return o;
}
uint64_t integer(const QJsonValue &v) {
  bool ok = false;
  auto s = v.toString();
  auto n = s.toULongLong(&ok);
  if (!v.isString() || !ok || s != QString::number(n))
    throw std::runtime_error("snapshot requires canonical unsigned integer strings");
  return n;
}
// QJsonDocument accepts duplicate keys. Reject them before semantic parsing.
class Keys {
  const QByteArray &b;
  qsizetype p = 0;
  void ws() {
    while (p < b.size() && (b[p] == ' ' || b[p] == '\n' || b[p] == '\r' || b[p] == '\t'))
      ++p;
  }
  QString string() {
    auto start = p++;
    bool escape = false;
    while (p < b.size()) {
      char c = b[p++];
      if (escape)
        escape = false;
      else if (c == '\\')
        escape = true;
      else if (c == '"') {
        auto a = QJsonDocument::fromJson("[" + b.mid(start, p - start) + "]").array();
        if (a.size() != 1)
          throw std::runtime_error("invalid JSON string");
        return a[0].toString();
      }
    }
    throw std::runtime_error("unterminated JSON string");
  }
  void value(int depth) {
    if (depth > 64)
      throw std::runtime_error("snapshot nesting limit");
    ws();
    if (p >= b.size())
      throw std::runtime_error("truncated JSON");
    char c = b[p];
    if (c == '{') {
      ++p;
      std::set<QString> seen;
      ws();
      if (p < b.size() && b[p] == '}') {
        ++p;
        return;
      }
      while (p < b.size()) {
        ws();
        if (p >= b.size())
          throw std::runtime_error("truncated object");
        if (b[p] != '"')
          throw std::runtime_error("invalid object key");
        auto key = string();
        if (!seen.insert(key).second)
          throw std::runtime_error("duplicate snapshot key");
        ws();
        if (p >= b.size() || b[p++] != ':')
          throw std::runtime_error("missing colon");
        value(depth + 1);
        ws();
        if (p >= b.size())
          break;
        c = b[p++];
        if (c == '}')
          return;
        if (c != ',')
          throw std::runtime_error("invalid object separator");
      }
      throw std::runtime_error("truncated object");
    }
    if (c == '[') {
      ++p;
      ws();
      if (p < b.size() && b[p] == ']') {
        ++p;
        return;
      }
      while (p < b.size()) {
        value(depth + 1);
        ws();
        if (p >= b.size())
          break;
        c = b[p++];
        if (c == ']')
          return;
        if (c != ',')
          throw std::runtime_error("invalid array separator");
      }
      throw std::runtime_error("truncated array");
    }
    if (c == '"') {
      string();
      return;
    }
    while (p < b.size() && b[p] != ',' && b[p] != ']' && b[p] != '}' && b[p] != ' ' &&
           b[p] != '\n' && b[p] != '\r' && b[p] != '\t')
      ++p;
  }

public:
  explicit Keys(const QByteArray &bytes) : b(bytes) {}
  void run() {
    value(0);
    ws();
    if (p != b.size())
      throw std::runtime_error("trailing JSON data");
  }
};
} // namespace
QJsonObject event_json(const Event &e) {
  QJsonObject o{{"schema", "tracelens.event/v1"},
                {"byte_encoding", "c-escaped"},
                {"kind", text(e.kind)},
                {"tid", n(e.tid)},
                {"generation", n(e.generation)},
                {"syscall", text(e.syscall)},
                {"arguments", text(e.arguments)},
                {"result", text(e.result)},
                {"errno", text(e.error)},
                {"timestamp", text(e.timestamp)},
                {"timestamp_kind", text(e.time_kind)},
                {"diagnostic", text(e.diagnostic)},
                {"start", evidence(e.start)}};
  o["duration_ns"] = e.duration_ns ? QJsonValue(n(*e.duration_ns)) : QJsonValue();
  o["end"] = e.end ? QJsonValue(evidence(*e.end)) : QJsonValue();
  QJsonArray paths;
  for (auto &p : e.paths)
    paths.append(text(p));
  o["paths"] = paths;
  return o;
}
QJsonObject snapshot_json(const Report &r) {
  QJsonArray sources, processes, diagnostics, slow, reasons;
  for (auto &s : r.sources)
    sources.append(QJsonObject{{"path", QString::fromStdString(s.path)},
                               {"sha256", QString::fromStdString(s.sha256)},
                               {"device", QString::fromStdString(s.device)},
                               {"inode", QString::fromStdString(s.inode)},
                               {"modified_ns", QString::fromStdString(s.modified_ns)},
                               {"size", n(s.size)},
                               {"scanned_bytes", n(s.scanned_bytes)},
                               {"lines", n(s.lines)},
                               {"changed", s.changed}});
  for (auto &[key, p] : r.processes)
    processes.append(QJsonObject{{"key", text(key)},
                                 {"tid", n(p.tid)},
                                 {"generation", n(p.generation)},
                                 {"parent", text(p.parent)},
                                 {"relation", text(p.relation)},
                                 {"calls", n(p.calls)},
                                 {"exited", p.exited}});
  for (auto &d : r.diagnostics)
    diagnostics.append(
        QJsonObject{{"message", text(d.message)}, {"evidence", evidence(d.evidence)}});
  for (auto &e : r.slow)
    slow.append(event_json(e));
  for (auto &reason : r.reasons)
    reasons.append(text(reason));
  QJsonObject l{{"input_bytes", n(r.limits.input_bytes)},   {"events", n(r.limits.events)},
                {"milliseconds", n(r.limits.milliseconds)}, {"line_bytes", n(r.limits.line_bytes)},
                {"nesting", n(r.limits.nesting)},           {"paths", n(r.limits.paths)},
                {"processes", n(r.limits.processes)},       {"pending", n(r.limits.pending)},
                {"diagnostics", n(r.limits.diagnostics)},   {"slow", n(r.limits.slow)},
                {"retained", n(r.limits.retained)},         {"sources", n(r.limits.sources)},
                {"syscalls", n(r.limits.syscalls)}};
  return {{"schema", "tracelens.snapshot/v1"},
          {"version", TRACELENS_VERSION},
          {"byte_encoding", "c-escaped"},
          {"sources", sources},
          {"limits", l},
          {"partial", r.partial},
          {"cancelled", r.cancelled},
          {"reasons", reasons},
          {"events", n(r.events)},
          {"calls", n(r.calls)},
          {"unknown", n(r.unknown)},
          {"incomplete_calls", n(r.incomplete_calls)},
          {"missing_duration", n(r.missing_duration)},
          {"diagnostic_count", n(r.diagnostic_count)},
          {"dropped_paths", n(r.dropped_paths)},
          {"dropped_processes", n(r.dropped_processes)},
          {"syscalls", table(r.syscalls)},
          {"errors", table(r.errors)},
          {"paths", table(r.paths)},
          {"processes", processes},
          {"diagnostics", diagnostics},
          {"slow", slow}};
}
Source source_from_json(const QJsonObject &o) {
  Source s;
  s.path = o["path"].toString().toStdString();
  s.sha256 = o["sha256"].toString().toStdString();
  s.device = o["device"].toString().toStdString();
  s.inode = o["inode"].toString().toStdString();
  s.modified_ns = o["modified_ns"].toString().toStdString();
  s.size = integer(o["size"]);
  s.scanned_bytes = integer(o["scanned_bytes"]);
  s.lines = integer(o["lines"]);
  s.changed = o["changed"].toBool();
  return s;
}
Evidence evidence_from_json(const QJsonObject &o) {
  if (!o["source"].isDouble() || o["source"].toDouble() < 0 ||
      o["source"].toDouble() != o["source"].toInt())
    throw std::runtime_error("invalid evidence source index");
  return {size_t(o["source"].toInt()), integer(o["line"]), integer(o["offset"]),
          integer(o["length"])};
}
QJsonObject read_snapshot(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    throw std::runtime_error("cannot open snapshot");
  if (f.size() > 64 * 1024 * 1024)
    throw std::runtime_error("snapshot exceeds 64 MiB limit");
  auto bytes = f.readAll();
  Keys(bytes).run();
  QJsonParseError error;
  auto doc = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject())
    throw std::runtime_error("invalid snapshot JSON");
  auto o = doc.object();
  if (o["schema"] != "tracelens.snapshot/v1")
    throw std::runtime_error("unsupported snapshot schema");
  if (!o["partial"].isBool() || !o["sources"].isArray() || !o["reasons"].isArray())
    throw std::runtime_error("invalid snapshot structure");
  for (auto field : {"events", "calls", "unknown", "incomplete_calls", "missing_duration",
                     "diagnostic_count", "dropped_paths", "dropped_processes"})
    integer(o[field]);
  for (auto axis : {"syscalls", "errors", "paths"}) {
    if (!o[axis].isObject())
      throw std::runtime_error("missing aggregate table");
    auto t = o[axis].toObject();
    for (auto it = t.begin(); it != t.end(); ++it) {
      if (!it.value().isObject())
        throw std::runtime_error("invalid aggregate");
      auto s = it.value().toObject();
      for (auto field : {"count", "errors", "known_duration", "total_ns", "max_ns"})
        integer(s[field]);
      if (integer(s["known_duration"]) > integer(s["count"]) ||
          integer(s["errors"]) > integer(s["count"]))
        throw std::runtime_error("invalid aggregate counts");
    }
  }
  for (auto v : o["sources"].toArray()) {
    if (!v.isObject())
      throw std::runtime_error("invalid source manifest");
    auto s = source_from_json(v.toObject());
    if (s.scanned_bytes > s.size || s.sha256.size() != 64 ||
        s.sha256.find_first_not_of("0123456789abcdef") != std::string::npos)
      throw std::runtime_error("invalid source digest/range");
  }
  if (!o["cancelled"].isBool() || o["byte_encoding"] != "c-escaped" || !o["version"].isString() ||
      !o["limits"].isObject() || !o["processes"].isArray() || !o["diagnostics"].isArray() ||
      !o["slow"].isArray())
    throw std::runtime_error("invalid snapshot metadata");
  for (auto reason : o["reasons"].toArray())
    if (!reason.isString())
      throw std::runtime_error("invalid partial reason");
  for (auto key :
       {"input_bytes", "events", "milliseconds", "line_bytes", "nesting", "paths", "processes",
        "pending", "diagnostics", "slow", "retained", "sources", "syscalls"})
    integer(o["limits"].toObject()[key]);
  auto sources = o["sources"].toArray();
  for (auto value : sources) {
    auto source = value.toObject();
    if (!source["path"].isString() || source["path"].toString().isEmpty() ||
        !source["changed"].isBool() || !source["modified_ns"].isString())
      throw std::runtime_error("invalid source metadata");
    integer(source["device"]);
    integer(source["inode"]);
  }
  auto check_evidence = [&](const QJsonValue &value) {
    if (!value.isObject())
      throw std::runtime_error("invalid evidence object");
    auto e = evidence_from_json(value.toObject());
    if (e.source >= size_t(sources.size()) || e.line == 0)
      throw std::runtime_error("evidence source or line out of range");
    auto source = source_from_json(sources[int(e.source)].toObject());
    if (e.offset > source.scanned_bytes || e.length > source.scanned_bytes - e.offset ||
        e.line > source.lines)
      throw std::runtime_error("evidence byte range out of bounds");
  };
  for (auto value : o["diagnostics"].toArray()) {
    auto d = value.toObject();
    if (!d["message"].isString())
      throw std::runtime_error("invalid diagnostic");
    check_evidence(d["evidence"]);
  }
  for (auto value : o["slow"].toArray()) {
    auto e = value.toObject();
    if (e["schema"] != "tracelens.event/v1" || !e["syscall"].isString() || !e["paths"].isArray())
      throw std::runtime_error("invalid slow-call event");
    integer(e["tid"]);
    integer(e["generation"]);
    if (!e["duration_ns"].isNull())
      integer(e["duration_ns"]);
    check_evidence(e["start"]);
    if (!e["end"].isNull())
      check_evidence(e["end"]);
  }
  for (auto value : o["processes"].toArray()) {
    auto process = value.toObject();
    auto tid = integer(process["tid"]), generation = integer(process["generation"]);
    integer(process["calls"]);
    if (process["key"].toString() != text(process_key(tid, generation)) ||
        !process["parent"].isString() || !process["exited"].isBool() ||
        !QStringList{"unobserved", "process", "thread", "unknown"}.contains(
            process["relation"].toString()))
      throw std::runtime_error("invalid process generation metadata");
  }
  return o;
}
QJsonObject diff_json(const QJsonObject &a, const QJsonObject &b) {
  QJsonObject axes;
  for (auto axis : {"syscalls", "errors", "paths"}) {
    auto x = a[axis].toObject(), y = b[axis].toObject();
    std::set<QString> keys;
    for (auto i = x.begin(); i != x.end(); ++i)
      keys.insert(i.key());
    for (auto i = y.begin(); i != y.end(); ++i)
      keys.insert(i.key());
    QJsonArray rows;
    for (auto &key : keys) {
      auto left = x[key].toObject(), right = y[key].toObject();
      QJsonObject deltas;
      for (auto metric : {"count", "errors", "known_duration", "total_ns"}) {
        uint64_t l = left.isEmpty() ? 0 : integer(left[metric]),
                 r = right.isEmpty() ? 0 : integer(right[metric]);
        deltas[metric] = r >= l ? n(r - l) : "-" + n(l - r);
      }
      rows.append(QJsonObject{{"key", key}, {"before", left}, {"after", right}, {"delta", deltas}});
    }
    axes[axis] = rows;
  }
  QJsonArray reasons;
  if (a["partial"].toBool() || b["partial"].toBool())
    reasons.append("At least one input is partial; deltas describe observed evidence only.");
  if (integer(a["missing_duration"]) || integer(b["missing_duration"]))
    reasons.append("Duration coverage is incomplete; total_ns includes only explicit -T evidence.");
  return {
      {"schema", "tracelens.diff/v1"}, {"before_sources", a["sources"]},
      {"after_sources", b["sources"]}, {"path_policy", "exact-observed-bytes"},
      {"pid_matching", false},         {"partial", a["partial"].toBool() || b["partial"].toBool()},
      {"cautions", reasons},           {"axes", axes}};
}
void check_output_alias(const QString &path, const std::vector<std::string> &inputs) {
  QFileInfo output(path);
  struct stat out{};
  bool exists = ::stat(QFile::encodeName(path).constData(), &out) == 0;
  for (auto &input : inputs) {
    QFileInfo in(QString::fromStdString(input));
    struct stat st{};
    if (output.absoluteFilePath() == in.absoluteFilePath() ||
        (exists && ::stat(input.c_str(), &st) == 0 && out.st_dev == st.st_dev &&
         out.st_ino == st.st_ino) ||
        (!output.canonicalFilePath().isEmpty() &&
         output.canonicalFilePath() == in.canonicalFilePath()))
      throw std::runtime_error("output aliases an input source");
  }
}
void save_atomic(const QString &path, const QByteArray &bytes,
                 const std::vector<std::string> &inputs) {
  check_output_alias(path, inputs);
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    throw std::runtime_error("atomic output write failed");
}
} // namespace tracelens
