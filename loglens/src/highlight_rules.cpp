#include "loglens/highlight_rules.hpp"

#include <algorithm>

namespace loglens {

namespace {

struct Candidate {
    std::size_t begin = 0;
    std::size_t end = 0;
    int priority = 0;
    std::size_t order = 0;
    std::string style;
};

bool candidatePrecedes(const Candidate& a, const Candidate& b) {
    if (a.priority != b.priority) {
        return a.priority > b.priority;
    }
    if (a.begin != b.begin) {
        return a.begin < b.begin;
    }
    return a.order < b.order;
}

bool spanPrecedes(const Span& a, const Span& b) { return a.begin < b.begin; }

void collectMatches(const Rule& rule, const std::string& text, std::size_t order,
                    std::vector<Candidate>& out) {
    if (rule.whole_line) {
        out.push_back(Candidate{0, text.size(), rule.priority, order, rule.style});
        return;
    }
    if (rule.pattern.empty()) {
        return;
    }
    std::size_t at = text.find(rule.pattern);
    while (at != std::string::npos) {
        out.push_back(
            Candidate{at, at + rule.pattern.size(), rule.priority, order, rule.style});
        at = text.find(rule.pattern, at + 1);
    }
}

bool overlapsAccepted(const Candidate& candidate, const std::vector<Span>& accepted) {
    for (const Span& span : accepted) {
        if (candidate.begin < span.end && span.begin < candidate.end) {
            return true;
        }
    }
    return false;
}

} // namespace

void HighlightRules::add(const Rule& rule) { rules_.push_back(rule); }

void HighlightRules::clear() { rules_.clear(); }

std::size_t HighlightRules::size() const { return rules_.size(); }

std::vector<Span> HighlightRules::apply(const std::string& text) const {
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < rules_.size(); ++i) {
        collectMatches(rules_[i], text, i, candidates);
    }
    std::sort(candidates.begin(), candidates.end(), candidatePrecedes);

    // Greedy by priority: the strongest candidate claims its range, and any
    // weaker candidate touching that range is dropped.
    std::vector<Span> accepted;
    for (const Candidate& candidate : candidates) {
        if (candidate.begin >= candidate.end || overlapsAccepted(candidate, accepted)) {
            continue;
        }
        accepted.push_back(Span{candidate.begin, candidate.end, candidate.style});
    }
    std::sort(accepted.begin(), accepted.end(), spanPrecedes);
    return accepted;
}

std::optional<std::string> HighlightRules::rowStyle() const {
    const Rule* selected = nullptr;
    for (const Rule& rule : rules_) {
        if (rule.whole_line && (selected == nullptr || rule.priority > selected->priority)) {
            selected = &rule;
        }
    }
    return selected == nullptr ? std::nullopt
                               : std::optional<std::string>(selected->style);
}

} // namespace loglens
