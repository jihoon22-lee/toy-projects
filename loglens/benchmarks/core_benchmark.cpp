#include "benchmark_common.hpp"

#include "loglens/log_parser.hpp"
#include "loglens/log_source.hpp"
#include "loglens/ring_buffer.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string input;
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
            options.input = value;
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
    options.valid = options.valid && !options.input.empty() && !options.output.empty()
                    && options.capacity <= loglens::kMaxRecordCapacity;
    return options;
}

double elapsedMilliseconds(const std::chrono::steady_clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
        .count();
}

bool applyDeltas(const std::vector<loglens::RecordDelta>& deltas,
                 loglens::RingBuffer& records, std::size_t& seen,
                 std::optional<double>& firstResult,
                 const std::chrono::steady_clock::time_point& start,
                 std::string& error) {
    for (const loglens::RecordDelta& delta : deltas) {
        if (!firstResult) {
            firstResult = elapsedMilliseconds(start);
        }
        if (delta.kind != loglens::RecordDelta::Kind::Append
            || delta.record_index != seen || delta.physical_line_number != seen + 1
            || delta.record.line_number != seen + 1 || delta.generation != 0) {
            error = "parser delta sequence does not match the deterministic input";
            return false;
        }
        records.push(delta.record);
        ++seen;
    }
    return true;
}

bool load(const Options& options, loglens::benchmark::Result& result) {
    loglens::FileTailer source(options.input);
    loglens::RecordAssembler assembler(loglens::Format::Auto);
    loglens::RingBuffer records(options.capacity);
    std::optional<std::uint64_t> snapshotEnd;
    std::optional<double> firstResult;
    std::size_t seen = 0;
    const auto start = std::chrono::steady_clock::now();

    while (true) {
        const loglens::SourceChunk chunk =
            snapshotEnd ? source.pollChunk(*snapshotEnd) : source.pollChunk();
        ++result.source_chunks;
        if (!chunk.ok()) {
            result.error = chunk.error.message;
            return false;
        }
        if (!snapshotEnd) {
            snapshotEnd = chunk.snapshot_end;
        }
        if (chunk.generation_changed || chunk.generation != 0) {
            result.error = "source changed during the fixed benchmark snapshot";
            return false;
        }
        if (!applyDeltas(assembler.consumeBytes(chunk.bytes), records, seen, firstResult,
                         start, result.error)) {
            return false;
        }
        if (!chunk.more_available) {
            break;
        }
    }

    const std::vector<loglens::RecordDelta> finalDeltas = assembler.flush();
    if (!finalDeltas.empty()
        && !applyDeltas(finalDeltas, records, seen, firstResult, start, result.error)) {
        return false;
    }
    result.load_ms = elapsedMilliseconds(start);
    result.first_result_ms = firstResult.value_or(result.load_ms);
    result.seen_records = seen;
    result.retained_records = records.size();
    result.dropped_records = records.droppedCount();
    if (!records.empty()) {
        result.oldest_line = records.at(0).line_number;
        result.newest_line = records.at(records.size() - 1).line_number;
    }
    const std::size_t expectedRetained = std::min(options.capacity, options.expected_records);
    const std::size_t expectedOldest = options.expected_records - expectedRetained + 1;
    result.correctness = seen == options.expected_records
                         && records.size() == expectedRetained
                         && records.droppedCount() == options.expected_records - expectedRetained
                         && result.oldest_line == expectedOldest
                         && result.newest_line == options.expected_records;
    if (!result.correctness) {
        result.error = "record count or retained window does not match the benchmark contract";
    }
    return result.correctness;
}

void printUsage() {
    std::cerr << "Usage: loglens-bench-core --input PATH --output JSON --capacity N "
                 "--expected-records N --expected-bytes N\n";
}

} // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    if (!options.valid) {
        printUsage();
        return 2;
    }

    loglens::benchmark::Result result;
    result.component = "core";
    result.capacity = options.capacity;
    result.expected_records = options.expected_records;
    std::string sizeError;
    if (!loglens::benchmark::fileSize(options.input, result.input_bytes, sizeError)) {
        result.error = sizeError;
    } else if (result.input_bytes != options.expected_bytes) {
        result.error = "input byte size does not match --expected-bytes";
    } else {
        static_cast<void>(load(options, result));
    }
    result.peak_rss_mib = loglens::benchmark::peakRssMiB(result.rss_source);

    std::string writeError;
    if (!loglens::benchmark::writeResult(options.output, result, writeError)) {
        std::cerr << writeError << '\n';
        return 2;
    }
    std::cout << "core load_ms=" << result.load_ms << " records=" << result.seen_records
              << " peak_rss_mib=" << result.peak_rss_mib << '\n';
    if (!result.correctness) {
        std::cerr << result.error << '\n';
        return 1;
    }
    return 0;
}
