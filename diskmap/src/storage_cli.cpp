#include "storage_cli.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace diskmap_cli {
namespace {

bool appendShortJsonEscape(std::string& output, unsigned char character) {
    switch (character) {
    case '"': output += "\\\""; return true;
    case '\\': output += "\\\\"; return true;
    case '\b': output += "\\b"; return true;
    case '\f': output += "\\f"; return true;
    case '\n': output += "\\n"; return true;
    case '\r': output += "\\r"; return true;
    case '\t': output += "\\t"; return true;
    default: return false;
    }
}

void appendControlJsonEscape(std::string& output, unsigned char character) {
    static constexpr char hex[] = "0123456789abcdef";
    output += "\\u00";
    output.push_back(hex[(character >> 4U) & 0x0fU]);
    output.push_back(hex[character & 0x0fU]);
}

void appendJsonCharacter(std::string& output, unsigned char character) {
    if (appendShortJsonEscape(output, character)) {
        return;
    }
    if (character < 0x20U) {
        appendControlJsonEscape(output, character);
        return;
    }
    output.push_back(static_cast<char>(character));
}

bool continuation(unsigned char character) {
    return character >= 0x80U && character <= 0xbfU;
}

struct Utf8LeadBounds {
    std::size_t width;
    unsigned char secondMinimum;
    unsigned char secondMaximum;
};

std::optional<Utf8LeadBounds> utf8LeadBounds(unsigned char first) {
    Utf8LeadBounds result{0, 0x80U, 0xbfU};
    if (first >= 0xc2U && first <= 0xdfU) {
        result.width = 2;
    } else if (first == 0xe0U) {
        result.width = 3;
        result.secondMinimum = 0xa0U;
    } else if (first == 0xedU) {
        result.width = 3;
        result.secondMaximum = 0x9fU;
    } else if ((first >= 0xe1U && first <= 0xecU)
               || (first >= 0xeeU && first <= 0xefU)) {
        result.width = 3;
    } else if (first == 0xf0U) {
        result.width = 4;
        result.secondMinimum = 0x90U;
    } else if (first >= 0xf1U && first <= 0xf3U) {
        result.width = 4;
    } else if (first == 0xf4U) {
        result.width = 4;
        result.secondMaximum = 0x8fU;
    } else {
        return std::nullopt;
    }
    return result;
}

std::size_t validUtf8Sequence(const std::string& value, std::size_t index) {
    const unsigned char first = static_cast<unsigned char>(value[index]);
    const auto bounds = utf8LeadBounds(first);
    if (!bounds || bounds->width > value.size() - index) {
        return 0;
    }
    const unsigned char second = static_cast<unsigned char>(value[index + 1]);
    if (second < bounds->secondMinimum || second > bounds->secondMaximum) {
        return 0;
    }
    for (std::size_t offset = 2; offset < bounds->width; ++offset) {
        if (!continuation(static_cast<unsigned char>(value[index + offset]))) {
            return 0;
        }
    }
    return bounds->width;
}

void appendMalformedByte(std::string& output, unsigned char character) {
    appendControlJsonEscape(output, character);
}

} // namespace

std::string escapeJsonStringContent(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 2);
    std::size_t index = 0;
    while (index < value.size()) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        if (character < 0x80U) {
            appendJsonCharacter(result, character);
            ++index;
            continue;
        }
        const std::size_t sequence = validUtf8Sequence(value, index);
        if (sequence == 0) {
            appendMalformedByte(result, character);
            ++index;
            continue;
        }
        result.append(value, index, sequence);
        index += sequence;
    }
    return result;
}

namespace {

const char* changeName(diskmap::SnapshotChangeKind kind) {
    switch (kind) {
    case diskmap::SnapshotChangeKind::Added: return "added";
    case diskmap::SnapshotChangeKind::Removed: return "removed";
    case diskmap::SnapshotChangeKind::Grown: return "grown";
    case diskmap::SnapshotChangeKind::Shrunk: return "shrunk";
    case diskmap::SnapshotChangeKind::Moved: return "moved";
    case diskmap::SnapshotChangeKind::Uncertain: return "uncertain";
    }
    return "unknown";
}

void printMetricJson(const diskmap::MetricValue& value, std::ostream& out) {
    out << "{\"bytes\":" << value.bytes << ",\"known\":"
        << (value.known ? "true" : "false") << "}";
}

void printSnapshotDiffJson(const diskmap::SnapshotDiff& diff, std::ostream& out) {
    out << "{\"schema\":\"diskmap.snapshot-diff/v1\",\"complete\":"
        << (diff.complete ? "true" : "false") << ",\"uncertain\":"
        << (diff.uncertain ? "true" : "false") << ",\"compared_nodes\":"
        << diff.compared_nodes << ",\"changes\":[";
    for (std::size_t index = 0; index < diff.changes.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const diskmap::SnapshotChange& change = diff.changes[index];
        out << "{\"kind\":\"" << changeName(change.kind)
            << "\",\"certain\":" << (change.certain ? "true" : "false")
            << ",\"before\":";
        if (change.has_before) {
            out << "{\"path\":\"" << escapeJsonStringContent(change.before_key.normalized_path)
                << "\",\"kind\":" << static_cast<int>(change.before_key.kind)
                << ",\"followed\":" << (change.before_key.followed ? "true" : "false")
                << ",\"metric\":";
            printMetricJson(change.before_metric, out);
            out << "}";
        } else {
            out << "null";
        }
        out << ",\"after\":";
        if (change.has_after) {
            out << "{\"path\":\"" << escapeJsonStringContent(change.after_key.normalized_path)
                << "\",\"kind\":" << static_cast<int>(change.after_key.kind)
                << ",\"followed\":" << (change.after_key.followed ? "true" : "false")
                << ",\"metric\":";
            printMetricJson(change.after_metric, out);
            out << "}";
        } else {
            out << "null";
        }
        out << ",\"reason\":\"" << escapeJsonStringContent(change.reason) << "\"}";
    }
    out << "]}\n";
}

void printSnapshotDiffText(const diskmap::SnapshotDiff& diff, std::ostream& out) {
    out << "Snapshot comparison: " << diff.changes.size() << " change(s), "
        << (diff.complete ? "exact evidence" : "conservative evidence") << "\n";
    for (const diskmap::SnapshotChange& change : diff.changes) {
        const std::string before = change.has_before
                                       ? change.before_key.normalized_path
                                       : "(absent)";
        const std::string after = change.has_after
                                      ? change.after_key.normalized_path
                                      : "(absent)";
        out << "  " << changeName(change.kind) << "  "
            << (change.certain ? "certain" : "candidate") << "  " << before
            << " -> " << after;
        if (!change.reason.empty()) {
            out << "  [" << change.reason << ']';
        }
        out << '\n';
    }
}

void printDuplicateEntryJson(const diskmap::DuplicateEntry& entry,
                             std::ostream& out,
                             bool first) {
    if (!first) {
        out << ',';
    }
    out << "{\"path\":\"" << escapeJsonStringContent(entry.path.generic_string())
        << "\",\"size\":" << entry.size << ",\"device\":"
        << entry.identity.device << ",\"file\":" << entry.identity.file
        << ",\"identity_valid\":" << (entry.identity.valid ? "true" : "false")
        << ",\"hard_link_count\":" << entry.hard_link_count
        << ",\"hard_link_count_known\":"
        << (entry.hard_link_count_known ? "true" : "false")
        << ",\"partial_fingerprint\":\""
        << escapeJsonStringContent(entry.partial_fingerprint) << "\",\"content_hash\":\""
        << escapeJsonStringContent(entry.content_hash) << "\",\"certain\":"
        << (entry.certain ? "true" : "false") << "}";
}

void printDuplicateGroupJson(const diskmap::DuplicateGroup& group,
                             std::ostream& out,
                             bool first) {
    if (!first) {
        out << ',';
    }
    out << "{\"size\":" << group.size << ",\"partial_fingerprint\":\""
        << escapeJsonStringContent(group.partial_fingerprint) << "\",\"content_hash\":\""
        << escapeJsonStringContent(group.content_hash) << "\",\"certain\":"
        << (group.certain ? "true" : "false") << ",\"reclaimable\":"
        << (group.reclaimable ? "true" : "false") << ",\"hard_link_alias\":"
        << (group.hard_link_alias ? "true" : "false")
        << ",\"reclaimable_bytes\":" << group.reclaimable_bytes
        << ",\"reason\":\"" << escapeJsonStringContent(group.reason) << "\",\"entries\":[";
    for (std::size_t entryIndex = 0; entryIndex < group.entries.size(); ++entryIndex) {
        printDuplicateEntryJson(group.entries[entryIndex], out, entryIndex == 0);
    }
    out << "]}";
}

void printDuplicateIssuesJson(const std::vector<diskmap::DuplicateIssue>& issues,
                              std::ostream& out) {
    for (std::size_t index = 0; index < issues.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        const diskmap::DuplicateIssue& issue = issues[index];
        out << "{\"kind\":\"" << diskmap::duplicateIssueKindName(issue.kind)
            << "\",\"path\":\"" << escapeJsonStringContent(issue.path.generic_string())
            << "\",\"message\":\"" << escapeJsonStringContent(issue.message) << "\"}";
    }
}

void printDuplicateJson(const diskmap::DuplicateAnalysis& analysis, std::ostream& out) {
    out << "{\"schema\":\"diskmap.duplicates/v1\",\"complete\":"
        << (analysis.complete ? "true" : "false") << ",\"uncertain\":"
        << (analysis.uncertain ? "true" : "false") << ",\"truncated\":"
        << (analysis.truncated ? "true" : "false") << ",\"cancelled\":"
        << (analysis.cancelled ? "true" : "false") << ",\"candidates_seen\":"
        << analysis.candidates_seen << ",\"candidates_retained\":"
        << analysis.candidates_retained << ",\"files_hashed\":"
        << analysis.files_hashed << ",\"bytes_read\":" << analysis.bytes_read
        << ",\"groups\":[";
    for (std::size_t groupIndex = 0; groupIndex < analysis.groups.size(); ++groupIndex) {
        printDuplicateGroupJson(analysis.groups[groupIndex], out, groupIndex == 0);
    }
    out << "],\"issues\":[";
    printDuplicateIssuesJson(analysis.issues, out);
    out << "]}\n";
}

void printDuplicateText(const diskmap::DuplicateAnalysis& analysis, std::ostream& out) {
    out << "Duplicate analysis: " << analysis.groups.size() << " group(s), "
        << analysis.candidates_retained << " retained candidate(s), "
        << (analysis.complete ? "complete inventory" : "conservative inventory") << "\n";
    for (std::size_t index = 0; index < analysis.groups.size(); ++index) {
        const diskmap::DuplicateGroup& group = analysis.groups[index];
        out << "  Group " << (index + 1) << ": " << group.size << " bytes, "
            << (group.certain ? "certain" : "candidate") << ", "
            << (group.reclaimable ? "reclaimable" : "not reclaimable");
        if (!group.reason.empty()) {
            out << " (" << group.reason << ')';
        }
        out << "\n";
        for (const diskmap::DuplicateEntry& entry : group.entries) {
            out << "    " << (entry.certain ? "certain" : "candidate") << "  "
                << entry.path.generic_string() << "  hash=" << group.content_hash << "\n";
        }
    }
    for (const diskmap::DuplicateIssue& issue : analysis.issues) {
        out << "  issue: " << diskmap::duplicateIssueKindName(issue.kind);
        if (!issue.path.empty()) {
            out << "  " << issue.path.generic_string();
        }
        out << "  " << issue.message << "\n";
    }
}

} // namespace

void printSnapshotDiff(const diskmap::SnapshotDiff& diff,
                       bool json,
                       std::ostream& out) {
    if (json) {
        printSnapshotDiffJson(diff, out);
    } else {
        printSnapshotDiffText(diff, out);
    }
}

void printDuplicateAnalysis(const diskmap::DuplicateAnalysis& analysis,
                            bool json,
                            std::ostream& out) {
    if (json) {
        printDuplicateJson(analysis, out);
    } else {
        printDuplicateText(analysis, out);
    }
}

} // namespace diskmap_cli
