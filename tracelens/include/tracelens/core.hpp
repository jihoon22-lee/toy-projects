#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace tracelens {
struct Limits {
  uint64_t input_bytes = 1024ULL * 1024 * 1024, events = 1000000, milliseconds = 0;
  size_t line_bytes = 1024 * 1024, nesting = 64, paths = 100000, processes = 65536, pending = 65536,
         diagnostics = 1000, slow = 1000, retained = 5000, sources = 128, syscalls = 4096;
};
struct Evidence {
  size_t source = 0;
  uint64_t line = 0, offset = 0, length = 0;
};
struct Source {
  std::string path, sha256, device, inode, modified_ns;
  uint64_t size = 0, scanned_bytes = 0, lines = 0;
  bool changed = false;
};
struct Event {
  std::string kind = "call", syscall, arguments, result, error, timestamp, time_kind, diagnostic;
  uint64_t tid = 0, generation = 0;
  std::optional<uint64_t> duration_ns;
  Evidence start;
  std::optional<Evidence> end;
  std::vector<std::string> paths;
};
struct Stats {
  uint64_t count = 0, errors = 0, known_duration = 0, total_ns = 0, max_ns = 0;
};
struct Process {
  uint64_t tid = 0, generation = 0, calls = 0;
  std::string parent, relation = "unobserved";
  bool exited = false;
};
struct Diagnostic {
  std::string message;
  Evidence evidence;
};
struct Report {
  Limits limits;
  std::vector<Source> sources;
  std::map<std::string, Stats> syscalls, errors, paths;
  std::map<std::string, Process> processes;
  std::vector<Event> retained, slow;
  std::vector<Diagnostic> diagnostics;
  uint64_t events = 0, calls = 0, unknown = 0, incomplete_calls = 0, diagnostic_count = 0,
           dropped_paths = 0, dropped_processes = 0, missing_duration = 0;
  bool partial = false, cancelled = false;
  std::vector<std::string> reasons;
};
using Sink = std::function<void(const Event &)>;
using Progress = std::function<void(uint64_t, uint64_t)>;
Report analyze(const std::vector<std::string> &files, const Limits &limits = {}, Sink sink = {},
               std::atomic_bool *cancel = nullptr, Progress progress = {});
std::string escaped(const std::string &bytes);
std::string process_key(uint64_t tid, uint64_t generation);
std::string source_excerpt(const Source &, const Evidence &, size_t max_bytes = 1024 * 1024);
// SHA-256 over exact bytes; exposed to verify saved evidence without Qt.
std::string file_digest(const std::string &path);
} // namespace tracelens
