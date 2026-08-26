#include "check.hpp"

#include "loglens/highlight_rules.hpp"

#include <string>

using loglens::HighlightRules;
using loglens::Rule;
using loglens::Span;

namespace {

Rule makeRule(const std::string& pattern, int priority, const std::string& style,
              bool wholeLine = false) {
    Rule rule;
    rule.pattern = pattern;
    rule.priority = priority;
    rule.style = style;
    rule.whole_line = wholeLine;
    return rule;
}

bool spansDisjointAndSorted(const std::vector<Span>& spans) {
    for (std::size_t i = 1; i < spans.size(); ++i) {
        if (spans[i].begin < spans[i - 1].end || spans[i].begin < spans[i - 1].begin) {
            return false;
        }
    }
    return true;
}

void testEmpty() {
    HighlightRules rules;
    CHECK_EQ(rules.size(), static_cast<std::size_t>(0));
    CHECK(rules.apply("anything").empty());

    rules.add(makeRule("", 1, "s"));
    CHECK(rules.apply("anything").empty());
    CHECK(rules.apply("").empty());
}

void testSingleAndRepeated() {
    HighlightRules rules;
    rules.add(makeRule("err", 1, "red"));
    const auto spans = rules.apply("err and err again");
    CHECK_EQ(spans.size(), static_cast<std::size_t>(2));
    CHECK(spansDisjointAndSorted(spans));
    if (spans.size() == 2) {
        CHECK_EQ(spans[0].begin, static_cast<std::size_t>(0));
        CHECK_EQ(spans[0].end, static_cast<std::size_t>(3));
        CHECK_EQ(spans[1].begin, static_cast<std::size_t>(8));
        CHECK_EQ(spans[0].style, std::string("red"));
    }
}

void testPriorityWins() {
    HighlightRules rules;
    rules.add(makeRule("timeout", 1, "low"));
    rules.add(makeRule("time", 9, "high"));
    const auto spans = rules.apply("a timeout here");
    CHECK(spansDisjointAndSorted(spans));
    // The higher-priority rule claims the overlapping range.
    CHECK_EQ(spans.size(), static_cast<std::size_t>(1));
    if (!spans.empty()) {
        CHECK_EQ(spans[0].style, std::string("high"));
        CHECK_EQ(spans[0].end - spans[0].begin, static_cast<std::size_t>(4));
    }
}

void testTiePrefersEarlier() {
    HighlightRules rules;
    rules.add(makeRule("bcd", 5, "second"));
    rules.add(makeRule("abc", 5, "first"));
    const auto spans = rules.apply("abcd");
    CHECK_EQ(spans.size(), static_cast<std::size_t>(1));
    if (!spans.empty()) {
        CHECK_EQ(spans[0].style, std::string("first"));
    }
}

void testWholeLine() {
    HighlightRules rules;
    rules.add(makeRule("", 1, "banner", true));
    const auto spans = rules.apply("some text");
    CHECK_EQ(spans.size(), static_cast<std::size_t>(1));
    if (!spans.empty()) {
        CHECK_EQ(spans[0].begin, static_cast<std::size_t>(0));
        CHECK_EQ(spans[0].end, static_cast<std::size_t>(9));
    }

    // A whole-line rule at lower priority loses to a stronger inner match.
    HighlightRules mixed;
    mixed.add(makeRule("", 1, "banner", true));
    mixed.add(makeRule("text", 9, "word"));
    const auto mixedSpans = mixed.apply("some text");
    CHECK(spansDisjointAndSorted(mixedSpans));
    CHECK_EQ(mixedSpans.size(), static_cast<std::size_t>(1));
    if (!mixedSpans.empty()) {
        CHECK_EQ(mixedSpans[0].style, std::string("word"));
    }
}

void testNonOverlappingCoexist() {
    HighlightRules rules;
    rules.add(makeRule("aaa", 1, "a"));
    rules.add(makeRule("ccc", 2, "c"));
    const auto spans = rules.apply("aaa bbb ccc");
    CHECK_EQ(spans.size(), static_cast<std::size_t>(2));
    CHECK(spansDisjointAndSorted(spans));
    if (spans.size() == 2) {
        CHECK_EQ(spans[0].style, std::string("a"));
        CHECK_EQ(spans[1].style, std::string("c"));
    }
}

void testClear() {
    HighlightRules rules;
    rules.add(makeRule("x", 1, "s"));
    CHECK_EQ(rules.size(), static_cast<std::size_t>(1));
    rules.clear();
    CHECK_EQ(rules.size(), static_cast<std::size_t>(0));
    CHECK(rules.apply("x").empty());
}

} // namespace

int main() {
    testEmpty();
    testSingleAndRepeated();
    testPriorityWins();
    testTiePrefersEarlier();
    testWholeLine();
    testNonOverlappingCoexist();
    testClear();
    return checkSummary();
}
