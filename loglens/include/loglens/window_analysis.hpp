#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "loglens/log_record.hpp"

namespace loglens {

struct TimeWindow {
    std::uint64_t begin_ms = 0;
    std::uint64_t end_ms = 0;
};

enum class SignalKind { NewPattern, RateSpike };

struct WindowSignal {
    SignalKind kind = SignalKind::RateSpike;
    std::string dimension;
    std::string key;
    std::size_t baseline_count = 0;
    std::size_t comparison_count = 0;
    double baseline_rate_per_minute = 0.0;
    double comparison_rate_per_minute = 0.0;
    double score = 0.0;
    std::string explanation;
    std::size_t first_line = 0;
    std::size_t last_line = 0;
};

struct CorrelationGroup {
    std::string field;
    std::string value;
    std::size_t count = 0;
    std::size_t first_line = 0;
    std::size_t last_line = 0;
};

struct WindowAnalysis {
    TimeWindow baseline;
    TimeWindow comparison;
    std::size_t baseline_records = 0;
    std::size_t comparison_records = 0;
    // Avoid Qt's legacy `signals` macro so this core header is safe to include
    // from both Qt and non-Qt translation units.
    std::vector<WindowSignal> findings;
    std::vector<CorrelationGroup> correlations;
};

// Compares two half-open timestamp windows. Signals are deterministic
// heuristics: they describe measured rate changes and never claim an AI
// diagnosis. Untimestamped records are deliberately excluded.
WindowAnalysis compareWindows(const std::vector<LogRecord>& records,
                              TimeWindow baseline,
                              TimeWindow comparison);

const char* signalKindName(SignalKind kind);

} // namespace loglens
