#include "loglens/file_search.hpp"

#include "loglens/evidence.hpp"
#include "loglens/filter_expr.hpp"
#include "loglens/log_source.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <stdexcept>

namespace loglens {
namespace {

bool contains(std::string_view text, std::string_view needle) {
    const auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
    return needle.empty() ||
           std::search(text.begin(), text.end(), needle.begin(), needle.end(), [&](char a, char b) {
               return lower(static_cast<unsigned char>(a)) == lower(static_cast<unsigned char>(b));
           }) != text.end();
}

} // namespace

FileSearchResult searchFile(const FileSearchOptions &options,
                            const std::function<bool()> &cancelled) {
    FileSearchResult result;
    if (options.path.empty() || (options.text.empty() && options.filter.empty()) ||
        options.text.size() > kMaxFilterQueryBytes || options.max_scan_bytes == 0 ||
        options.max_results == 0 || options.max_results > 10000 || options.max_result_bytes == 0 ||
        options.max_result_bytes > 64 * 1024 * 1024 || options.max_elapsed_ms == 0 ||
        options.max_record_bytes == 0 || options.max_record_bytes > kMaxRecordBytes) {
        result.error = "invalid whole-file search options";
        return result;
    }
    ParseError error;
    const auto filter =
        options.filter.empty() ? std::optional<Filter>() : Filter::parse(options.filter, error);
    if (!options.filter.empty() && !filter) {
        result.error = error.message;
        return result;
    }
    const auto initialEvidence = captureSourceEvidence(options.path);
    if (!initialEvidence.ok()) {
        result.error = initialEvidence.error;
        return result;
    }
    const auto started = std::chrono::steady_clock::now();
    const auto stopped = [&] {
        if (cancelled && cancelled()) {
            result.cancelled = true;
            return true;
        }
        if (static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - started)
                                           .count()) >= options.max_elapsed_ms) {
            result.limit_reached = true;
            return true;
        }
        return false;
    };
    try {
        FileTailer source(options.path, 64 * 1024);
        RecordAssembler assembler(options.format, EncodingErrorPolicy::PreserveBytes,
                                  options.max_record_bytes, options.multiline);
        assembler.setFormatPlugin(options.plugin.get());
        std::optional<LogRecord> pending;
        std::size_t retainedBytes = 0;
        bool limited = false;
        const auto publish = [&] {
            if (!pending)
                return;
            ++result.scanned_records;
            if (contains(pending->raw, options.text) && (!filter || filter->matches(*pending))) {
                std::size_t bytes =
                    pending->raw.size() + pending->message.size() + pending->source.size();
                for (const auto &field : pending->fields)
                    bytes += field.first.size() + field.second.size();
                for (const auto &diagnostic : pending->diagnostics)
                    bytes += diagnostic.field.size() + diagnostic.message.size();
                if (result.records.size() >= options.max_results ||
                    bytes > options.max_result_bytes - retainedBytes) {
                    result.limit_reached = limited = true;
                } else {
                    retainedBytes += bytes;
                    result.records.push_back(std::move(*pending));
                }
            }
            pending.reset();
        };
        const auto consume = [&](std::vector<RecordDelta> deltas) {
            for (auto &delta : deltas) {
                if (stopped() || limited)
                    return;
                if (delta.kind == RecordDelta::Kind::Append)
                    publish();
                if (limited)
                    return;
                pending = std::move(delta.record);
            }
        };
        bool first = true;
        std::uint64_t boundary = 0;
        while (!stopped() && !limited) {
            const auto chunk = source.pollChunk(first ? options.max_scan_bytes : boundary);
            if (!chunk.ok()) {
                result.error = chunk.error.message;
                break;
            }
            if (first) {
                result.source_identity = sourceIdentity(chunk.identity);
                if (result.source_identity != initialEvidence.identity) {
                    result.error = "source replaced before search";
                    break;
                }
                result.snapshot_end = chunk.snapshot_end;
                boundary = std::min(options.max_scan_bytes, result.snapshot_end);
                first = false;
            } else if (chunk.generation_changed ||
                       sourceIdentity(chunk.identity) != result.source_identity) {
                result.error = "source changed during whole-file search";
                break;
            }
            result.scanned_bytes = chunk.position;
            consume(assembler.consumeBytes(chunk.bytes));
            if (limited || result.cancelled || result.limit_reached)
                break;
            if (chunk.position >= boundary) {
                if (boundary == result.snapshot_end) {
                    consume(assembler.flush());
                    if (!stopped() && !limited)
                        publish();
                    result.complete = !result.cancelled && !result.limit_reached;
                } else {
                    // A partial logical record at the scan limit is not a verified result.
                    result.limit_reached = true;
                }
                break;
            }
        }
    } catch (const std::exception &exception) {
        result.error = exception.what();
    }
    const auto finalEvidence = captureSourceEvidence(options.path);
    if (!finalEvidence.ok() || initialEvidence.identity != finalEvidence.identity ||
        initialEvidence.modified != finalEvidence.modified ||
        initialEvidence.size != finalEvidence.size ||
        initialEvidence.fingerprint != finalEvidence.fingerprint) {
        result.complete = false;
        result.error = "source changed during whole-file search; evidence may be stale";
    }
    return result;
}

} // namespace loglens
