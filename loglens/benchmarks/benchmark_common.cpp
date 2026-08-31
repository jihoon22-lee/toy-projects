#include "benchmark_common.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>

#ifndef _WIN32
#include <sys/resource.h>
#endif

namespace loglens::benchmark {

namespace {

std::string jsonEscape(const std::string& text) {
    std::string escaped;
    for (const char byte : text) {
        if (byte == '"' || byte == '\\') {
            escaped.push_back('\\');
        }
        if (byte == '\n') {
            escaped += "\\n";
        } else if (byte == '\r') {
            escaped += "\\r";
        } else if (byte == '\t') {
            escaped += "\\t";
        } else if (static_cast<unsigned char>(byte) >= 0x20U) {
            escaped.push_back(byte);
        }
    }
    return escaped;
}

double procHighWaterMiB() {
    std::ifstream input("/proc/self/status");
    std::string key;
    while (input >> key) {
        if (key != "VmHWM:") {
            input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            continue;
        }
        double kibibytes = 0.0;
        input >> kibibytes;
        return kibibytes / 1024.0;
    }
    return 0.0;
}

} // namespace

bool parsePositiveSize(const std::string& text, std::size_t& value) {
    std::uint64_t parsedValue = 0;
    const char* first = text.data();
    const char* last = first + text.size();
    const std::from_chars_result parsed = std::from_chars(first, last, parsedValue);
    if (parsed.ec != std::errc() || parsed.ptr != last || parsedValue == 0
        || parsedValue > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    value = static_cast<std::size_t>(parsedValue);
    return true;
}

bool fileSize(const std::string& path, std::uint64_t& bytes, std::string& error) {
    std::error_code statusError;
    const std::uintmax_t size = std::filesystem::file_size(path, statusError);
    if (statusError) {
        error = "cannot determine input size: " + statusError.message();
        return false;
    }
    bytes = static_cast<std::uint64_t>(size);
    return true;
}

double peakRssMiB(std::string& source) {
    const double proc = procHighWaterMiB();
    if (proc > 0.0) {
        source = "procfs";
        return proc;
    }
#ifndef _WIN32
    struct rusage usage {};
    if (::getrusage(RUSAGE_SELF, &usage) == 0) {
        source = "rusage";
        return static_cast<double>(usage.ru_maxrss) / 1024.0;
    }
#endif
    source = "unavailable";
    return 0.0;
}

bool writeResult(const std::string& path, const Result& result, std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot open benchmark result '" + path + "'";
        return false;
    }
    const double seconds = result.load_ms / 1000.0;
    const double mibPerSecond =
        seconds > 0.0 ? (static_cast<double>(result.input_bytes) / (1024.0 * 1024.0)) / seconds
                      : 0.0;
    const double recordsPerSecond =
        seconds > 0.0 ? static_cast<double>(result.seen_records) / seconds : 0.0;
    output << std::fixed << std::setprecision(3)
           << "{\n"
           << "  \"schema\": \"loglens-benchmark/v1\",\n"
           << "  \"component\": \"" << jsonEscape(result.component) << "\",\n"
           << "  \"qt_major\": " << result.qt_major << ",\n"
           << "  \"configuration\": {\"capacity\": " << result.capacity
           << ", \"source_chunk_bytes\": 1048576, \"max_record_bytes\": 65536},\n"
           << "  \"input\": {\"bytes\": " << result.input_bytes
           << ", \"expected_records\": " << result.expected_records << "},\n"
           << "  \"metrics\": {\"first_result_ms\": " << result.first_result_ms
           << ", \"first_paint_ms\": ";
    if (result.first_paint_ms) {
        output << *result.first_paint_ms;
    } else {
        output << "null";
    }
    output << ", \"load_ms\": " << result.load_ms
           << ", \"throughput_mib_s\": " << mibPerSecond
           << ", \"records_per_s\": " << recordsPerSecond
           << ", \"peak_rss_mib\": " << result.peak_rss_mib
           << ", \"rss_source\": \"" << jsonEscape(result.rss_source)
           << "\", \"source_chunks\": " << result.source_chunks << "},\n"
           << "  \"retention\": {\"seen\": " << result.seen_records
           << ", \"retained\": " << result.retained_records
           << ", \"dropped\": " << result.dropped_records
           << ", \"oldest_line\": " << result.oldest_line
           << ", \"newest_line\": " << result.newest_line << "},\n"
           << "  \"checks\": {\"correctness\": "
           << (result.correctness ? "true" : "false") << "},\n"
           << "  \"error\": \"" << jsonEscape(result.error) << "\"\n"
           << "}\n";
    if (!output) {
        error = "cannot write benchmark result '" + path + "'";
        return false;
    }
    return true;
}

} // namespace loglens::benchmark
