#pragma once

namespace main_window_filter_data {

constexpr int kAnyValue = -1;
constexpr int kAnyIssue = -1;
constexpr int kProblemIssues = -2;

enum AgeChoice {
    AnyAge = 0,
    LastDay,
    LastWeek,
    LastMonth,
    OlderThanMonth,
};

} // namespace main_window_filter_data
