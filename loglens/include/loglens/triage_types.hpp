#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "loglens/highlight_rules.hpp"
namespace loglens {
constexpr std::size_t kMaxHighlightRules = 128;
constexpr std::size_t kMaxTriageEntries = 8192;
constexpr std::size_t kMaxHighlightPatternBytes = 1024;
constexpr std::size_t kMaxAnnotationBytes = 4096;

struct NamedHighlightRule {
    std::string name;
    Rule rule;
};

struct TriageEntry {
    std::string source_path;
    std::size_t line_number = 0;
    bool bookmarked = false;
    std::string annotation;
    std::string source_identity;
    std::uint64_t generation = 0;
    std::string record_fingerprint;
};

struct TriageState {
    std::vector<NamedHighlightRule> rules;
    std::vector<TriageEntry> entries;
};

} // namespace loglens
