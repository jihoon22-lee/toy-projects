#include "tracelens/core.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <unistd.h>
namespace tracelens {
namespace {
using SV = std::string_view;
SV trim(SV s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r'))
    s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t'))
    s.remove_suffix(1);
  return s;
}
bool number(SV s, uint64_t &v) {
  if (s.empty())
    return false;
  auto r = std::from_chars(s.data(), s.data() + s.size(), v);
  return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}
std::optional<uint64_t> nanos(SV s) {
  auto p = s.find('.');
  uint64_t whole = 0, frac = 0;
  if (!number(s.substr(0, p), whole) || whole > UINT64_MAX / 1000000000ULL)
    return {};
  if (p != SV::npos) {
    auto f = s.substr(p + 1);
    if (f.empty() || f.size() > 9 || !number(f, frac))
      return {};
    for (size_t i = f.size(); i < 9; ++i)
      frac *= 10;
  }
  if (whole * 1000000000ULL > UINT64_MAX - frac)
    return {};
  return whole * 1000000000ULL + frac;
}
std::string modified(const struct stat &s) {
  return std::to_string(s.st_mtim.tv_sec) + ":" + std::to_string(s.st_mtim.tv_nsec);
}
struct Fd {
  int n;
  ~Fd() {
    if (n >= 0)
      ::close(n);
  }
};
struct Prefix {
  uint64_t tid;
  std::string stamp, kind;
  SV body;
};
Prefix prefix(SV s, uint64_t fallback) {
  Prefix p{fallback, {}, {}, trim(s)};
  s = p.body;
  if (s.starts_with("[pid ")) {
    auto end = s.find(']');
    uint64_t n;
    if (end != SV::npos && number(trim(s.substr(5, end - 5)), n)) {
      p.tid = n;
      s = trim(s.substr(end + 1));
    }
  } else {
    auto sp = s.find_first_of(" \t");
    uint64_t n;
    if (sp != SV::npos && number(s.substr(0, sp), n)) {
      p.tid = n;
      s = trim(s.substr(sp + 1));
    }
  }
  auto sp = s.find_first_of(" \t");
  if (sp != SV::npos) {
    auto t = s.substr(0, sp);
    if (t.find(':') != SV::npos && t.find_first_not_of("0123456789:.") == SV::npos) {
      p.stamp = t;
      p.kind = "clock";
      s = trim(s.substr(sp + 1));
    } else if (t.find('.') != SV::npos && nanos(t)) {
      p.stamp = t;
      p.kind = "numeric";
      s = trim(s.substr(sp + 1));
    }
  }
  p.body = s;
  return p;
}
// Find the matching outer parenthesis with bounded nesting and quoted-string escapes.
size_t closing(SV s, size_t open, size_t max) {
  std::vector<char> stack;
  bool quoted = false, escape = false;
  for (size_t i = open; i < s.size(); ++i) {
    char c = s[i];
    if (quoted) {
      if (escape)
        escape = false;
      else if (c == '\\')
        escape = true;
      else if (c == '"')
        quoted = false;
      continue;
    }
    if (c == '"') {
      quoted = true;
      continue;
    }
    if (c == '(' || c == '[' || c == '{') {
      if (stack.size() >= max)
        return SV::npos;
      stack.push_back(c);
    } else if (c == ')' || c == ']' || c == '}') {
      if (stack.empty())
        return SV::npos;
      char want = c == ')' ? '(' : c == ']' ? '[' : '{';
      if (stack.back() != want)
        return SV::npos;
      stack.pop_back();
      if (stack.empty())
        return i;
    }
  }
  return SV::npos;
}
std::vector<SV> arguments(SV s) {
  std::vector<SV> out;
  int depth = 0;
  bool quoted = false, escape = false;
  size_t start = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (quoted) {
      if (escape)
        escape = false;
      else if (c == '\\')
        escape = true;
      else if (c == '"')
        quoted = false;
      continue;
    }
    if (c == '"')
      quoted = true;
    else if (c == '(' || c == '[' || c == '{')
      ++depth;
    else if (c == ')' || c == ']' || c == '}')
      --depth;
    else if (c == ',' && depth == 0) {
      out.push_back(trim(s.substr(start, i - start)));
      start = i + 1;
    }
  }
  out.push_back(trim(s.substr(start)));
  return out;
}
std::optional<std::string> quoted_path(SV s) {
  if (s.empty() || s.front() != '"')
    return {};
  std::string out;
  for (size_t i = 1; i < s.size(); ++i) {
    unsigned char c = s[i];
    if (c == '"') {
      if (!trim(s.substr(i + 1)).empty())
        return {};
      return out;
    }
    if (c != '\\') {
      out += char(c);
      continue;
    }
    if (++i >= s.size())
      return {};
    c = s[i];
    if (c == 'n')
      out += '\n';
    else if (c == 'r')
      out += '\r';
    else if (c == 't')
      out += '\t';
    else if (c == '\\' || c == '"')
      out += char(c);
    else if (c >= '0' && c <= '7') {
      unsigned v = c - '0';
      for (int j = 0; j < 2 && i + 1 < s.size() && s[i + 1] >= '0' && s[i + 1] <= '7'; ++j)
        v = v * 8 + (s[++i] - '0');
      out += char(v);
    } else if (c == 'x') {
      unsigned v = 0;
      int n = 0;
      while (n < 2 && i + 1 < s.size()) {
        char h = s[i + 1];
        int d = h >= '0' && h <= '9'   ? h - '0'
                : h >= 'a' && h <= 'f' ? h - 'a' + 10
                : h >= 'A' && h <= 'F' ? h - 'A' + 10
                                       : -1;
        if (d < 0)
          break;
        v = v * 16 + d;
        ++i;
        ++n;
      }
      if (!n)
        return {};
      out += char(v);
    } else
      return {};
  }
  return {};
}
void extract_paths(Event &e) {
  static const std::map<std::string, std::vector<size_t>> positions = {
      {"open", {0}},        {"openat", {1}},       {"openat2", {1}},    {"creat", {0}},
      {"stat", {0}},        {"lstat", {0}},        {"statx", {1}},      {"newfstatat", {1}},
      {"access", {0}},      {"faccessat", {1}},    {"faccessat2", {1}}, {"execve", {0}},
      {"execveat", {1}},    {"unlink", {0}},       {"unlinkat", {1}},   {"rename", {0, 1}},
      {"renameat", {1, 3}}, {"renameat2", {1, 3}}, {"mkdir", {0}},      {"mkdirat", {1}},
      {"rmdir", {0}},       {"chdir", {0}},        {"readlink", {0}},   {"readlinkat", {1}},
      {"chmod", {0}},       {"chown", {0}},        {"link", {0, 1}},    {"symlink", {0, 1}}};
  auto p = positions.find(e.syscall);
  if (p == positions.end())
    return;
  auto args = arguments(e.arguments);
  for (auto i : p->second)
    if (i < args.size())
      if (auto path = quoted_path(args[i]))
        e.paths.push_back(*path);
}
bool parse_call(SV s, Event &e, size_t nesting) {
  auto open = s.find('(');
  if (open == SV::npos || open == 0 || open > 128)
    return false;
  auto name = s.substr(0, open);
  if (name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
      SV::npos)
    return false;
  auto close = closing(s, open, nesting);
  if (close == SV::npos)
    return false;
  auto rest = trim(s.substr(close + 1));
  if (!rest.starts_with('='))
    return false;
  rest = trim(rest.substr(1));
  e.syscall = name;
  e.arguments = s.substr(open + 1, close - open - 1);
  if (rest.ends_with('>')) {
    auto angle = rest.rfind('<');
    if (angle != SV::npos && angle > 0 && rest[angle - 1] == ' ') {
      if (auto ns = nanos(rest.substr(angle + 1, rest.size() - angle - 2))) {
        e.duration_ns = *ns;
        rest = trim(rest.substr(0, angle));
      }
    }
  }
  e.result = rest;
  if (rest.starts_with("-1 ")) {
    auto rem = trim(rest.substr(3));
    auto end = rem.find(' ');
    auto err = rem.substr(0, end);
    if (err.size() > 1 && err.size() <= 128 && err.front() == 'E' &&
        err.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == SV::npos)
      e.error = err;
  }
  if (rest.empty() || rest.starts_with('?'))
    e.diagnostic = "return value unavailable";
  extract_paths(e);
  return true;
}
bool add(Stats &s, const Event &e) {
  bool overflow = false;
  ++s.count;
  if (!e.error.empty())
    ++s.errors;
  if (e.duration_ns) {
    ++s.known_duration;
    overflow = UINT64_MAX - s.total_ns < *e.duration_ns;
    s.total_ns = overflow ? UINT64_MAX : s.total_ns + *e.duration_ns;
    s.max_ns = std::max(s.max_ns, *e.duration_ns);
  }
  return overflow;
}
} // namespace
std::string process_key(uint64_t tid, uint64_t generation) {
  return std::to_string(tid) + ":" + std::to_string(generation);
}
std::string escaped(const std::string &bytes) {
  static const char *hex = "0123456789abcdef";
  std::string out;
  for (unsigned char c : bytes) {
    if (c >= 32 && c < 127 && c != '\\')
      out += char(c);
    else if (c == '\\')
      out += "\\\\";
    else if (c == '\n')
      out += "\\n";
    else if (c == '\r')
      out += "\\r";
    else if (c == '\t')
      out += "\\t";
    else {
      out += "\\x";
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}
Report analyze(const std::vector<std::string> &files, const Limits &limits, Sink sink,
               std::atomic_bool *cancel, Progress progress) {
  if (files.empty())
    throw std::runtime_error("at least one source is required");
  if (files.size() > limits.sources)
    throw std::runtime_error("source count limit exceeded");
  if (!limits.line_bytes || !limits.nesting || !limits.events || !limits.input_bytes)
    throw std::runtime_error("limits must be positive");
  Report r;
  r.limits = limits;
  auto started = std::chrono::steady_clock::now();
  uint64_t bytes = 0, total = 0;
  bool stop = false;
  std::map<uint64_t, uint64_t> generations;
  std::map<uint64_t, size_t> observed_sources;
  std::map<uint64_t, Event> pending;
  auto partial = [&](const std::string &reason) {
    r.partial = true;
    if (std::find(r.reasons.begin(), r.reasons.end(), reason) == r.reasons.end())
      r.reasons.push_back(reason);
  };
  auto diagnostic = [&](std::string message, Evidence evidence) {
    ++r.diagnostic_count;
    if (r.diagnostics.size() < limits.diagnostics)
      r.diagnostics.push_back({std::move(message), evidence});
  };
  auto process = [&](uint64_t tid) -> Process * {
    auto g = generations.find(tid);
    if (g == generations.end()) {
      if (generations.size() >= limits.processes) {
        ++r.dropped_processes;
        partial("process cardinality limit");
        return nullptr;
      }
      g = generations.emplace(tid, 0).first;
    }
    auto key = process_key(tid, g->second);
    auto p = r.processes.find(key);
    if (p != r.processes.end() && p->second.exited) {
      ++g->second;
      key = process_key(tid, g->second);
      p = r.processes.end();
    }
    if (p == r.processes.end()) {
      if (r.processes.size() >= limits.processes) {
        ++r.dropped_processes;
        partial("process cardinality limit");
        return nullptr;
      }
      p = r.processes.emplace(key, Process{tid, g->second, 0, {}, "unobserved", false}).first;
    }
    return &p->second;
  };
  auto emit = [&](Event e) {
    if (r.events >= limits.events) {
      partial("event budget");
      stop = true;
      return;
    }
    ++r.events;
    if (e.kind == "call") {
      ++r.calls;
      if (!e.duration_ns)
        ++r.missing_duration;
      if (auto p = r.processes.find(process_key(e.tid, e.generation)); p != r.processes.end())
        ++p->second.calls;
      auto key = e.syscall;
      if (!r.syscalls.contains(key) && r.syscalls.size() >= limits.syscalls) {
        key = "<overflow>";
        partial("syscall cardinality limit");
      }
      if (add(r.syscalls[key], e))
        partial("duration aggregate overflow (saturated uint64)");
      if (!e.error.empty()) {
        auto key = e.error;
        if (!r.errors.contains(key) && r.errors.size() >= limits.syscalls) {
          key = "<overflow>";
          partial("errno cardinality limit");
        }
        if (add(r.errors[key], e))
          partial("duration aggregate overflow (saturated uint64)");
      }
      for (auto &path : e.paths) {
        auto key = path;
        if (!r.paths.contains(key) && r.paths.size() >= limits.paths) {
          key = "<overflow>";
          ++r.dropped_paths;
          partial("path cardinality limit");
        }
        if (add(r.paths[key], e))
          partial("duration aggregate overflow (saturated uint64)");
      }
      if (e.duration_ns && limits.slow) {
        auto cmp = [](const Event &a, const Event &b) {
          if (a.duration_ns != b.duration_ns)
            return a.duration_ns > b.duration_ns;
          return std::tie(a.start.source, a.start.offset) <
                 std::tie(b.start.source, b.start.offset);
        };
        if (r.slow.size() < limits.slow) {
          r.slow.push_back(e);
          std::push_heap(r.slow.begin(), r.slow.end(), cmp);
        } else if (cmp(e, r.slow.front())) {
          std::pop_heap(r.slow.begin(), r.slow.end(), cmp);
          r.slow.back() = e;
          std::push_heap(r.slow.begin(), r.slow.end(), cmp);
        }
      }
    } else if (e.kind == "unknown")
      ++r.unknown;
    else if (e.kind == "incomplete")
      ++r.incomplete_calls;
    if (!e.diagnostic.empty()) {
      diagnostic(e.diagnostic, e.start);
      partial("incomplete or unsupported evidence");
    }
    if (r.retained.size() < limits.retained)
      r.retained.push_back(e);
    if (sink)
      sink(e);
  };
  std::vector<std::pair<dev_t, ino_t>> identities;
  for (const auto &file : files) {
    struct stat st{};
    if (::stat(file.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
      throw std::runtime_error("cannot read regular source: " + file);
    if (st.st_size < 0)
      throw std::runtime_error("invalid source size");
    auto identity = std::make_pair(st.st_dev, st.st_ino);
    if (std::find(identities.begin(), identities.end(), identity) != identities.end())
      throw std::runtime_error("duplicate input source identity");
    identities.push_back(identity);
    total = UINT64_MAX - total < uint64_t(st.st_size) ? UINT64_MAX : total + uint64_t(st.st_size);
  }
  for (const auto &file : files) {
    if (stop)
      break;
    Fd fd{::open(file.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK)};
    struct stat st{};
    if (fd.n < 0 || fstat(fd.n, &st) != 0 || !S_ISREG(st.st_mode))
      throw std::runtime_error("cannot open source: " + file);
    Source src;
    src.path = std::filesystem::absolute(file).lexically_normal().string();
    src.size = uint64_t(st.st_size);
    src.device = std::to_string(st.st_dev);
    src.inode = std::to_string(st.st_ino);
    src.modified_ns = modified(st);
    size_t sid = r.sources.size();
    r.sources.push_back(src);
    uint64_t fallback = 0;
    auto ext = std::filesystem::path(file).extension().string();
    if (ext.size() > 1)
      number(SV(ext).substr(1), fallback);
    // Anonymous TIDs cannot be paired across independently selected files.
    if (sid > 0 && generations.contains(0)) {
      auto old = pending.find(0);
      if (old != pending.end()) {
        auto lost = std::move(old->second);
        pending.erase(old);
        lost.kind = "incomplete";
        lost.diagnostic = "anonymous TID cannot resume across sources";
        emit(std::move(lost));
      }
      ++generations[0];
    }
    Sha256 digest;
    std::array<char, 65536> chunk{};
    std::string line;
    line.reserve(std::min(limits.line_bytes, size_t(65536)));
    uint64_t offset = 0, line_start = 0, line_no = 1;
    bool oversized = false;
    auto consume = [&](bool newline) {
      Evidence ev{sid, line_no++, line_start, offset - line_start};
      line_start = offset;
      auto p = prefix(line, fallback);
      Event e;
      e.start = ev;
      e.tid = p.tid;
      e.timestamp = p.stamp;
      e.time_kind = p.kind;
      auto proc = process(e.tid);
      if (proc)
        observed_sources[e.tid] = sid;
      if (e.tid == 0 && files.size() > 1)
        partial("unidentified TID in multiple-source input");
      e.generation = proc ? proc->generation : 0;
      SV body = p.body;
      if (oversized) {
        e.kind = "unknown";
        e.diagnostic = "physical line exceeds byte limit";
        partial("line byte limit");
      } else if (body.starts_with("--- ")) {
        e.kind = "signal";
        e.result = body;
      } else if (body.starts_with("+++ ")) {
        e.kind = "exit";
        e.result = body;
        if (proc)
          proc->exited = true;
        auto old = pending.find(e.tid);
        if (old != pending.end()) {
          auto lost = std::move(old->second);
          pending.erase(old);
          lost.kind = "incomplete";
          lost.diagnostic = "unfinished call interrupted by exit";
          emit(std::move(lost));
        }
      } else if (body.starts_with("strace:") || body.starts_with("Process ")) {
        e.kind = "notice";
        e.result = body;
      } else if (body.starts_with("<... ")) {
        auto mark = body.find(" resumed>");
        auto old = pending.find(e.tid);
        if (mark != SV::npos && old != pending.end() &&
            SV(old->second.syscall) == body.substr(5, mark - 5)) {
          auto initial = std::move(old->second);
          pending.erase(old);
          auto joined = initial.arguments + std::string(body.substr(mark + 9));
          initial.end = ev;
          if (parse_call(joined, initial, limits.nesting)) {
            e = std::move(initial);
          } else {
            e = std::move(initial);
            e.kind = "incomplete";
            e.diagnostic = "malformed resumed call or nesting limit";
          }
        } else {
          e.kind = "unknown";
          e.result = body;
          e.diagnostic = "resumed call has no matching TID and syscall";
        }
      } else if (body.ends_with("<unfinished ...>")) {
        auto open = body.find('(');
        if (open != SV::npos && open > 0) {
          e.syscall = std::string(body.substr(0, open));
          e.arguments = std::string(body.substr(0, body.size() - 16));
          auto old = pending.find(e.tid);
          if (old != pending.end()) {
            auto lost = std::move(old->second);
            pending.erase(old);
            lost.kind = "incomplete";
            lost.diagnostic = "unfinished call replaced before resume";
            emit(std::move(lost));
          }
          if (pending.size() < limits.pending) {
            pending[e.tid] = e;
            line.clear();
            oversized = false;
            return;
          }
          e.kind = "incomplete";
          e.diagnostic = "unfinished call capacity exceeded";
          partial("pending call limit");
        } else {
          e.kind = "unknown";
          e.diagnostic = "malformed unfinished call";
        }
      } else if (!parse_call(body, e, limits.nesting)) {
        e.kind = "unknown";
        e.result = body;
        e.diagnostic = "unrecognized line, malformed call, or nesting limit";
      }
      if (!newline) {
        e.diagnostic = "unterminated final line";
        partial("unterminated final line");
      }
      if (e.kind == "call" && (e.syscall == "fork" || e.syscall == "vfork" ||
                               e.syscall == "clone" || e.syscall == "clone3")) {
        auto token = SV(e.result).substr(0, e.result.find(' '));
        uint64_t child = 0;
        if (number(token, child) && child) {
          Process *cp = nullptr;
          // Split-file traversal order is not chronological. Link a child observed
          // in another source without inventing a new generation after its exit.
          auto seen = observed_sources.find(child);
          if (seen != observed_sources.end() && seen->second != sid) {
            auto gen = generations.at(child);
            if (gen == 0)
              cp = &r.processes.at(process_key(child, gen));
            else
              partial("ambiguous reused TID relationship across sources");
          } else
            cp = process(child);
          if (cp) {
            cp->parent = process_key(e.tid, e.generation);
            cp->relation = (e.syscall == "fork" || e.syscall == "vfork")           ? "process"
                           : e.arguments.find("CLONE_THREAD") != std::string::npos ? "thread"
                                                                                   : "unknown";
          }
        }
      }
      emit(std::move(e));
      line.clear();
      oversized = false;
      if (r.events >= limits.events && (offset < src.size || sid + 1 < files.size())) {
        partial("event budget");
        stop = true;
      }
    };
    while (offset < src.size && !stop) {
      if (cancel && cancel->load()) {
        r.cancelled = true;
        partial("cancelled");
        stop = true;
        break;
      }
      if (limits.milliseconds && uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - started)
                                              .count()) >= limits.milliseconds) {
        partial("time budget");
        stop = true;
        break;
      }
      if (bytes >= limits.input_bytes) {
        partial("input byte budget");
        stop = true;
        break;
      }
      size_t want =
          size_t(std::min({uint64_t(chunk.size()), src.size - offset, limits.input_bytes - bytes}));
      auto got = ::read(fd.n, chunk.data(), want);
      if (got < 0)
        throw std::runtime_error("source read failed: " + file);
      if (got == 0) {
        partial("source shortened during analysis");
        break;
      }
      size_t used = 0;
      for (; used < size_t(got) && !stop; ++used) {
        char c = chunk[used];
        ++offset;
        ++bytes;
        if (c == '\n')
          consume(true);
        else if (line.size() < limits.line_bytes)
          line += c;
        else
          oversized = true;
      }
      digest.add(chunk.data(), used);
      if (progress)
        progress(bytes, total);
    }
    if (!stop && !line.empty())
      consume(false);
    auto &stored = r.sources[sid];
    stored.scanned_bytes = offset;
    stored.lines = line_no - 1;
    stored.sha256 = digest.finish();
    struct stat after{}, path_after{};
    stored.changed = fstat(fd.n, &after) != 0 || ::stat(file.c_str(), &path_after) != 0 ||
                     after.st_size != st.st_size || modified(after) != modified(st) ||
                     path_after.st_ino != st.st_ino || path_after.st_dev != st.st_dev;
    if (stored.changed)
      partial("source changed during analysis");
    if (offset < src.size)
      partial("source range incomplete");
  }
  for (auto &[tid, e] : pending) {
    (void)tid;
    e.kind = "incomplete";
    e.diagnostic = "unfinished call has no resume within observed input";
    emit(e);
  }
  if (r.sources.size() < files.size())
    partial("unvisited sources");
  std::sort(r.reasons.begin(), r.reasons.end());
  std::sort(r.slow.begin(), r.slow.end(), [](const Event &a, const Event &b) {
    if (a.duration_ns != b.duration_ns)
      return a.duration_ns > b.duration_ns;
    return std::tie(a.start.source, a.start.offset) < std::tie(b.start.source, b.start.offset);
  });
  return r;
}
std::string file_digest(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    throw std::runtime_error("cannot open source for verification");
  Sha256 sha;
  std::array<char, 65536> b{};
  while (f) {
    f.read(b.data(), b.size());
    sha.add(b.data(), size_t(f.gcount()));
  }
  if (!f.eof())
    throw std::runtime_error("source verification read failed");
  return sha.finish();
}
std::string source_excerpt(const Source &source, const Evidence &e, size_t max_bytes) {
  if (source.changed || source.scanned_bytes != source.size)
    throw std::runtime_error("source is changed or only partially fingerprinted");
  if (e.offset > source.size || e.length > source.size - e.offset || e.length > max_bytes)
    throw std::runtime_error("source evidence range exceeds limit");
  Fd fd{::open(source.path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK)};
  struct stat before{};
  auto matches = [&](const struct stat &s) {
    return std::to_string(s.st_dev) == source.device && std::to_string(s.st_ino) == source.inode &&
           uint64_t(s.st_size) == source.size && modified(s) == source.modified_ns;
  };
  if (fd.n < 0 || fstat(fd.n, &before) != 0 || !S_ISREG(before.st_mode) || !matches(before))
    throw std::runtime_error("source changed since analysis; reopen it before navigating");
  Sha256 sha;
  std::array<char, 65536> chunk{};
  uint64_t read_bytes = 0;
  while (read_bytes < source.size) {
    auto n = ::read(fd.n, chunk.data(),
                    size_t(std::min(uint64_t(chunk.size()), source.size - read_bytes)));
    if (n <= 0)
      throw std::runtime_error("source verification read failed");
    sha.add(chunk.data(), size_t(n));
    read_bytes += uint64_t(n);
  }
  if (sha.finish() != source.sha256)
    throw std::runtime_error("source content changed since analysis");
  std::string b(size_t(e.length), '\0');
  size_t count = 0;
  while (count < b.size()) {
    auto n =
        ::pread(fd.n, b.data() + count, b.size() - count, static_cast<off_t>(e.offset + count));
    if (n <= 0)
      throw std::runtime_error("source changed while reading evidence");
    count += size_t(n);
  }
  struct stat after{}, path_after{};
  if (fstat(fd.n, &after) != 0 || ::stat(source.path.c_str(), &path_after) != 0 ||
      !matches(after) || !matches(path_after))
    throw std::runtime_error("source changed during evidence verification");
  return escaped(b);
}
} // namespace tracelens
