#include "loglens/window_analysis.hpp"

#include "loglens/log_stats.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <string_view>
#include <tuple>

namespace loglens {
namespace {

struct CountRange {
    std::size_t count = 0;
    std::size_t first_line = 0;
    std::size_t last_line = 0;
};

using Counts = std::map<std::string, CountRange>;

bool inWindow(const LogRecord& record, const TimeWindow& window) {
    return record.timestamp_ms != 0 && window.begin_ms < window.end_ms
           && record.timestamp_ms >= window.begin_ms && record.timestamp_ms < window.end_ms;
}

void addCount(Counts& counts, const std::string& key, std::size_t line) {
    CountRange& range = counts[key.empty() ? "(empty)" : key];
    ++range.count;
    if (range.first_line == 0 || (line != 0 && line < range.first_line)) {
        range.first_line = line;
    }
    range.last_line = std::max(range.last_line, line);
}

double ratePerMinute(std::size_t count, const TimeWindow& window) {
    if (window.begin_ms >= window.end_ms) {
        return 0.0;
    }
    const double minutes = static_cast<double>(window.end_ms - window.begin_ms) / 60000.0;
    return static_cast<double>(count) / minutes;
}

std::string formatRate(double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << value;
    return output.str();
}

void appendSignals(std::vector<WindowSignal>& output,
                   std::string dimension,
                   const Counts& baseline,
                   const Counts& comparison,
                   const TimeWindow& baseline_window,
                   const TimeWindow& comparison_window) {
    for (const auto& item : comparison) {
        const auto old = baseline.find(item.first);
        const std::size_t old_count = old == baseline.end() ? 0 : old->second.count;
        const std::size_t new_count = item.second.count;
        const double old_rate = ratePerMinute(old_count, baseline_window);
        const double new_rate = ratePerMinute(new_count, comparison_window);
        const bool is_new = old_count == 0 && new_count > 0;
        const bool is_spike = old_count > 0 && new_count >= 2
                              && new_rate >= old_rate * 2.0;
        if (!is_new && !is_spike) {
            continue;
        }

        WindowSignal signal;
        signal.kind = is_new ? SignalKind::NewPattern : SignalKind::RateSpike;
        signal.dimension = dimension;
        signal.key = item.first;
        signal.baseline_count = old_count;
        signal.comparison_count = new_count;
        signal.baseline_rate_per_minute = old_rate;
        signal.comparison_rate_per_minute = new_rate;
        signal.score = is_new ? new_rate : new_rate / std::max(old_rate, 0.000001);
        signal.first_line = item.second.first_line;
        signal.last_line = item.second.last_line;
        signal.explanation = is_new
            ? "not observed in the baseline; comparison rate " + formatRate(new_rate)
                  + "/min"
            : "measured rate increased from " + formatRate(old_rate) + " to "
                  + formatRate(new_rate) + "/min (" + formatRate(signal.score) + "x)";
        output.push_back(std::move(signal));
    }
}

bool identifierCharacter(char value) {
    const unsigned char byte = static_cast<unsigned char>(value);
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z')
           || (byte >= '0' && byte <= '9') || value == '-' || value == '_' || value == '.';
}

std::string extractField(std::string_view text, std::string_view field) {
    const std::array<char, 2> separators{'=', ':'};
    for (char separator : separators) {
        const std::string needle = std::string(field) + separator;
        std::size_t position = text.find(needle);
        while (position != std::string_view::npos) {
            if (position == 0 || !identifierCharacter(text[position - 1])) {
                std::size_t begin = position + needle.size();
                while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\"')) {
                    ++begin;
                }
                std::size_t end = begin;
                while (end < text.size() && identifierCharacter(text[end])) {
                    ++end;
                }
                if (end > begin && end - begin <= 128) {
                    return std::string(text.substr(begin, end - begin));
                }
            }
            position = text.find(needle, position + needle.size());
        }
    }
    return {};
}

void addCorrelation(std::map<std::pair<std::string, std::string>, CountRange>& groups,
                    const std::string& field,
                    const LogRecord& record) {
    const std::string value = extractField(record.raw, field);
    if (!value.empty()) {
        CountRange& range = groups[{field, value}];
        ++range.count;
        if (range.first_line == 0 || (record.line_number != 0
                                      && record.line_number < range.first_line)) {
            range.first_line = record.line_number;
        }
        range.last_line = std::max(range.last_line, record.line_number);
    }
}

} // namespace

WindowAnalysis compareWindows(const std::vector<LogRecord>& records,
                              TimeWindow baseline,
                              TimeWindow comparison) {
    WindowAnalysis result;
    result.baseline = baseline;
    result.comparison = comparison;
    if (baseline.begin_ms >= baseline.end_ms || comparison.begin_ms >= comparison.end_ms) {
        return result;
    }

    Counts baseline_levels;
    Counts comparison_levels;
    Counts baseline_sources;
    Counts comparison_sources;
    Counts baseline_patterns;
    Counts comparison_patterns;
    std::map<std::pair<std::string, std::string>, CountRange> correlations;
    for (const LogRecord& record : records) {
        const bool in_baseline = inWindow(record, baseline);
        const bool in_comparison = inWindow(record, comparison);
        if (!in_baseline && !in_comparison) {
            continue;
        }
        Counts& levels = in_baseline ? baseline_levels : comparison_levels;
        Counts& sources = in_baseline ? baseline_sources : comparison_sources;
        Counts& patterns = in_baseline ? baseline_patterns : comparison_patterns;
        if (in_baseline) {
            ++result.baseline_records;
        } else {
            ++result.comparison_records;
        }
        addCount(levels, levelName(record.level), record.line_number);
        addCount(sources, record.source, record.line_number);
        addCount(patterns, normalizeMessage(record.message), record.line_number);
        if (in_comparison) {
            addCorrelation(correlations, "correlation_id", record);
            addCorrelation(correlations, "request_id", record);
            addCorrelation(correlations, "thread_id", record);
            addCorrelation(correlations, "thread", record);
        }
    }

    appendSignals(result.signals, "level", baseline_levels, comparison_levels,
                  baseline, comparison);
    appendSignals(result.signals, "source", baseline_sources, comparison_sources,
                  baseline, comparison);
    appendSignals(result.signals, "pattern", baseline_patterns, comparison_patterns,
                  baseline, comparison);
    std::sort(result.signals.begin(), result.signals.end(), [](const WindowSignal& left,
                                                               const WindowSignal& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return std::tie(left.dimension, left.key) < std::tie(right.dimension, right.key);
    });

    for (const auto& item : correlations) {
        result.correlations.push_back(CorrelationGroup{
            item.first.first, item.first.second, item.second.count,
            item.second.first_line, item.second.last_line});
    }
    std::sort(result.correlations.begin(), result.correlations.end(),
              [](const CorrelationGroup& left, const CorrelationGroup& right) {
                  if (left.count != right.count) {
                      return left.count > right.count;
                  }
                  return std::tie(left.field, left.value) < std::tie(right.field, right.value);
              });
    return result;
}

const char* signalKindName(SignalKind kind) {
    return kind == SignalKind::NewPattern ? "new-pattern" : "rate-spike";
}

} // namespace loglens
