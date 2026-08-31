#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace loglens::benchmark {

struct Result {
    std::string component;
    int qt_major = 0;
    std::uint64_t input_bytes = 0;
    std::size_t expected_records = 0;
    std::size_t seen_records = 0;
    std::size_t retained_records = 0;
    std::size_t dropped_records = 0;
    std::size_t oldest_line = 0;
    std::size_t newest_line = 0;
    std::size_t capacity = 0;
    std::size_t source_chunks = 0;
    double first_result_ms = 0.0;
    std::optional<double> first_paint_ms;
    double load_ms = 0.0;
    double peak_rss_mib = 0.0;
    std::string rss_source;
    bool correctness = false;
    std::string error;
};

bool parsePositiveSize(const std::string& text, std::size_t& value);
bool fileSize(const std::string& path, std::uint64_t& bytes, std::string& error);
double peakRssMiB(std::string& source);
bool writeResult(const std::string& path, const Result& result, std::string& error);

} // namespace loglens::benchmark
