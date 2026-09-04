#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace loglens {

struct Rule {
    std::string pattern;
    bool whole_line = false;
    int priority = 0;
    std::string style;
};

struct Span {
    std::size_t begin = 0;
    std::size_t end = 0; // exclusive
    std::string style;
};

// Ordered highlight rules. apply() resolves overlaps so the result is always
// sorted and non-overlapping, which is what a renderer needs.
class HighlightRules {
public:
    void add(const Rule& rule);
    void clear();
    std::size_t size() const;

    // Higher priority wins an overlap; ties go to the earlier match.
    std::vector<Span> apply(const std::string& text) const;
    // Returns the strongest whole-line rule, using insertion order for ties.
    std::optional<std::string> rowStyle() const;

private:
    std::vector<Rule> rules_;
};

} // namespace loglens
