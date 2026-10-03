#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "loglens/format_plugin.hpp"
#include "loglens/log_parser.hpp"
#include "loglens/log_record.hpp"

namespace loglens {

struct FileSearchOptions {
    std::string path;
    std::string text;
    std::string filter;
    Format format = Format::Auto;
    MultilinePolicy multiline = MultilinePolicy::FoldContinuations;
    std::size_t max_record_bytes = kDefaultMaxRecordBytes;
    std::shared_ptr<const FormatPlugin> plugin;
    std::uint64_t max_scan_bytes = 1024ULL * 1024 * 1024;
    std::size_t max_results = 1000;
    std::size_t max_result_bytes = 8 * 1024 * 1024;
    std::uint64_t max_elapsed_ms = 30000;
};

struct FileSearchResult {
    std::vector<LogRecord> records;
    std::string source_identity;
    std::uint64_t snapshot_end = 0;
    std::uint64_t scanned_bytes = 0;
    std::size_t scanned_records = 0;
    bool complete = false;
    bool cancelled = false;
    bool limit_reached = false;
    std::string error;
};

// Streams a fixed opened-file boundary independently of the retained GUI ring.
// Results are final logical records (including continuations), never intermediate deltas.
FileSearchResult searchFile(const FileSearchOptions &options,
                            const std::function<bool()> &cancelled = {});

} // namespace loglens
