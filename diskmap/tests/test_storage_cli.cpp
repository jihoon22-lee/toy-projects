// Coverage tests for the text and versioned JSON storage report writers.

#include "check.hpp"
#include "storage_cli.hpp"

#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using diskmap::DuplicateAnalysis;
using diskmap::DuplicateEntry;
using diskmap::DuplicateGroup;
using diskmap::DuplicateIssue;
using diskmap::DuplicateIssueKind;
using diskmap::FileIdentity;
using diskmap::FsKind;
using diskmap::MetricValue;
using diskmap::NodeKey;
using diskmap::SnapshotChange;
using diskmap::SnapshotChangeKind;
using diskmap::SnapshotDiff;

bool contains(const std::string& value, const std::string& needle) {
    return value.find(needle) != std::string::npos;
}

NodeKey key(std::string path, FsKind kind = FsKind::RegularFile, bool followed = false) {
    NodeKey result;
    result.normalized_path = std::move(path);
    result.kind = kind;
    result.followed = followed;
    result.identity = FileIdentity{7, 42, true};
    return result;
}

SnapshotChange change(SnapshotChangeKind kind,
                      std::string before,
                      std::string after,
                      bool certain,
                      std::string reason,
                      bool beforeKnown = true,
                      bool afterKnown = true) {
    SnapshotChange result;
    result.kind = kind;
    result.certain = certain;
    result.reason = std::move(reason);
    result.has_before = !before.empty();
    result.has_after = !after.empty();
    if (result.has_before) {
        result.before_key = key(std::move(before));
        result.before_metric = MetricValue{11, beforeKnown, true};
    }
    if (result.has_after) {
        result.after_key = key(std::move(after));
        result.after_metric = MetricValue{22, afterKnown, true};
    }
    return result;
}

void testEmptyReports() {
    SnapshotDiff emptyDiff;
    std::ostringstream diffText;
    diskmap_cli::printSnapshotDiff(emptyDiff, false, diffText);
    CHECK_EQ(diffText.str(),
             "Snapshot comparison: 0 change(s), exact evidence\n");

    std::ostringstream diffJson;
    diskmap_cli::printSnapshotDiff(emptyDiff, true, diffJson);
    CHECK_EQ(diffJson.str(),
             "{\"schema\":\"diskmap.snapshot-diff/v1\",\"complete\":true,"
             "\"uncertain\":false,\"compared_nodes\":0,\"changes\":[]}\n");

    DuplicateAnalysis emptyAnalysis;
    std::ostringstream duplicateText;
    diskmap_cli::printDuplicateAnalysis(emptyAnalysis, false, duplicateText);
    CHECK_EQ(duplicateText.str(),
             "Duplicate analysis: 0 group(s), 0 retained candidate(s), complete inventory\n");

    std::ostringstream duplicateJson;
    diskmap_cli::printDuplicateAnalysis(emptyAnalysis, true, duplicateJson);
    CHECK_EQ(duplicateJson.str(),
             "{\"schema\":\"diskmap.duplicates/v1\",\"complete\":true,"
             "\"uncertain\":false,\"truncated\":false,\"cancelled\":false,"
             "\"candidates_seen\":0,\"candidates_retained\":0,\"files_hashed\":0,"
             "\"bytes_read\":0,\"groups\":[],\"issues\":[]}\n");
}

void testSnapshotReports() {
    std::string special = "\"\\\b\f\n\r\t";
    special.push_back('\x01');
    special += u8" 한글";
    special.push_back(static_cast<char>(0xff));

    SnapshotDiff diff;
    diff.complete = true;
    diff.uncertain = false;
    diff.compared_nodes = 17;
    diff.changes.push_back(
        change(SnapshotChangeKind::Added, {}, "/added", true, "added reason"));
    diff.changes.push_back(
        change(SnapshotChangeKind::Removed, "/removed", {}, false, "removed reason"));
    diff.changes.push_back(change(SnapshotChangeKind::Grown, "/grown", "/grown", true, {}));
    diff.changes.push_back(
        change(SnapshotChangeKind::Shrunk, "/shrunk", "/shrunk", false, "shrunk reason", false));
    diff.changes.push_back(
        change(SnapshotChangeKind::Moved, "/old", "/new", true, "moved reason"));
    diff.changes.push_back(change(SnapshotChangeKind::Uncertain, "/uncertain", "/uncertain",
                                  false, special, true, false));
    diff.changes.push_back(change(static_cast<SnapshotChangeKind>(99), "/unknown", "/unknown",
                                  false, {}));

    std::ostringstream text;
    diskmap_cli::printSnapshotDiff(diff, false, text);
    const std::string renderedText = text.str();
    CHECK(contains(renderedText, "Snapshot comparison: 7 change(s), exact evidence"));
    CHECK(contains(renderedText, "added  certain  (absent) -> /added"));
    CHECK(contains(renderedText, "removed  candidate  /removed -> (absent)"));
    CHECK(contains(renderedText, "grown  certain  /grown -> /grown"));
    CHECK(contains(renderedText, "shrunk  candidate  /shrunk -> /shrunk"));
    CHECK(contains(renderedText, "moved  certain  /old -> /new"));
    CHECK(contains(renderedText, "uncertain  candidate  /uncertain -> /uncertain"));
    CHECK(contains(renderedText, "unknown  candidate  /unknown -> /unknown"));
    CHECK(contains(renderedText, "[added reason]"));
    CHECK(contains(renderedText, "[removed reason]"));

    std::ostringstream json;
    diskmap_cli::printSnapshotDiff(diff, true, json);
    const std::string renderedJson = json.str();
    CHECK(contains(renderedJson, "\"schema\":\"diskmap.snapshot-diff/v1\""));
    CHECK(contains(renderedJson, "\"compared_nodes\":17"));
    CHECK(contains(renderedJson, "\"kind\":\"added\""));
    CHECK(contains(renderedJson, "\"kind\":\"removed\""));
    CHECK(contains(renderedJson, "\"kind\":\"grown\""));
    CHECK(contains(renderedJson, "\"kind\":\"shrunk\""));
    CHECK(contains(renderedJson, "\"kind\":\"moved\""));
    CHECK(contains(renderedJson, "\"kind\":\"uncertain\""));
    CHECK(contains(renderedJson, "\"kind\":\"unknown\""));
    CHECK(contains(renderedJson, "\"before\":null"));
    CHECK(contains(renderedJson, "\"after\":null"));
    CHECK(contains(renderedJson, "\"known\":false"));
    CHECK(contains(renderedJson, "\"reason\":\"\""));
    CHECK(contains(renderedJson, "\"reason\":\"added reason\""));
    CHECK(contains(renderedJson, "\\\""));
    CHECK(contains(renderedJson, "\\\\"));
    CHECK(contains(renderedJson, "\\b"));
    CHECK(contains(renderedJson, "\\f"));
    CHECK(contains(renderedJson, "\\n"));
    CHECK(contains(renderedJson, "\\r"));
    CHECK(contains(renderedJson, "\\t"));
    CHECK(contains(renderedJson, "\\u0001"));
    CHECK(contains(renderedJson, u8"한글"));
    CHECK(contains(renderedJson, "\\u00ff"));
}

DuplicateEntry entry(std::string path,
                     bool certain,
                     bool identityValid,
                     bool hardLinkKnown) {
    DuplicateEntry result;
    result.key = key(path);
    result.path = std::filesystem::path(result.key.normalized_path);
    result.size = 42;
    result.identity = FileIdentity{7, 42, identityValid};
    result.hard_link_count = 1;
    result.hard_link_count_known = hardLinkKnown;
    result.partial_fingerprint = "partial";
    result.content_hash = "hash";
    result.certain = certain;
    return result;
}

void testDuplicateReports() {
    std::string special = "\"\\\b\f\n\r\t";
    special.push_back('\x01');

    DuplicateGroup first;
    first.size = 42;
    first.partial_fingerprint = special;
    first.content_hash = "hash\\\"\n";
    first.certain = true;
    first.reclaimable = true;
    first.hard_link_alias = false;
    first.reclaimable_bytes = 42;
    first.reason = special;
    first.entries.push_back(entry("/tmp/a", true, true, true));
    first.entries.push_back(entry("/tmp/b", false, false, false));

    DuplicateGroup second;
    second.size = 0;
    second.certain = false;
    second.reclaimable = false;
    second.hard_link_alias = true;
    second.entries.push_back(entry("/tmp/c", false, true, true));

    DuplicateAnalysis analysis;
    analysis.complete = false;
    analysis.uncertain = true;
    analysis.truncated = true;
    analysis.cancelled = true;
    analysis.candidates_seen = 6;
    analysis.candidates_retained = 4;
    analysis.files_hashed = 4;
    analysis.bytes_read = 168;
    analysis.groups.push_back(first);
    analysis.groups.push_back(second);
    analysis.issues.push_back(DuplicateIssue{
        std::filesystem::path(special), key(special), DuplicateIssueKind::ReadError, special});
    analysis.issues.push_back(DuplicateIssue{{}, {}, DuplicateIssueKind::HashMismatch, {}});

    std::ostringstream text;
    diskmap_cli::printDuplicateAnalysis(analysis, false, text);
    const std::string renderedText = text.str();
    CHECK(contains(renderedText,
                   "Duplicate analysis: 2 group(s), 4 retained candidate(s), conservative inventory"));
    CHECK(contains(renderedText, "Group 1: 42 bytes, certain, reclaimable"));
    CHECK(contains(renderedText, "Group 2: 0 bytes, candidate, not reclaimable"));
    CHECK(contains(renderedText, "/tmp/a  hash=hash"));
    CHECK(contains(renderedText, "/tmp/b  hash=hash"));
    CHECK(contains(renderedText, "issue: read-error"));
    CHECK(contains(renderedText, "issue: hash-mismatch"));

    std::ostringstream json;
    diskmap_cli::printDuplicateAnalysis(analysis, true, json);
    const std::string renderedJson = json.str();
    CHECK(contains(renderedJson, "\"schema\":\"diskmap.duplicates/v1\""));
    CHECK(contains(renderedJson, "\"complete\":false"));
    CHECK(contains(renderedJson, "\"uncertain\":true"));
    CHECK(contains(renderedJson, "\"truncated\":true"));
    CHECK(contains(renderedJson, "\"cancelled\":true"));
    CHECK(contains(renderedJson, "\"candidates_seen\":6"));
    CHECK(contains(renderedJson, "\"candidates_retained\":4"));
    CHECK(contains(renderedJson, "\"files_hashed\":4"));
    CHECK(contains(renderedJson, "\"bytes_read\":168"));
    CHECK(contains(renderedJson, "\"groups\":[{"));
    CHECK(contains(renderedJson, "},{\"size\":0"));
    CHECK(contains(renderedJson, "\"reclaimable\":true"));
    CHECK(contains(renderedJson, "\"hard_link_alias\":true"));
    CHECK(contains(renderedJson, "\"hard_link_count_known\":false"));
    CHECK(contains(renderedJson, "\"entries\":[{"));
    CHECK(contains(renderedJson, "},{\"path\":\"/tmp/b\""));
    CHECK(contains(renderedJson, "\"issues\":[{"));
    CHECK(contains(renderedJson, "},{\"kind\":\"hash-mismatch\""));
    CHECK(contains(renderedJson, "\"path\":\"\""));
    CHECK(contains(renderedJson, "\\\""));
    CHECK(contains(renderedJson, "\\\\"));
    CHECK(contains(renderedJson, "\\b"));
    CHECK(contains(renderedJson, "\\f"));
    CHECK(contains(renderedJson, "\\n"));
    CHECK(contains(renderedJson, "\\r"));
    CHECK(contains(renderedJson, "\\t"));
    CHECK(contains(renderedJson, "\\u0001"));
}

void testSnapshotDiffFilters() {
    SnapshotDiff diff;
    diff.complete = true;
    diff.changes.push_back(
        change(SnapshotChangeKind::Added, {}, "/added", true, "added reason"));
    diff.changes.push_back(
        change(SnapshotChangeKind::Removed, "/removed", {}, false, "removed reason"));
    diff.changes.push_back(change(SnapshotChangeKind::Grown, "/grown", "/grown", true, {}));
    diff.changes.push_back(
        change(SnapshotChangeKind::Uncertain, "/maybe", "/maybe", false, "unclear"));

    diskmap_cli::SnapshotDiffFilter kinds;
    kinds.kinds = {SnapshotChangeKind::Added, SnapshotChangeKind::Removed};
    std::ostringstream kindText;
    diskmap_cli::printSnapshotDiff(diff, false, kindText, kinds);
    CHECK(contains(kindText.str(), "2 change(s) shown of 4"));
    CHECK(contains(kindText.str(), "/added"));
    CHECK(!contains(kindText.str(), "/grown"));
    std::ostringstream kindJson;
    diskmap_cli::printSnapshotDiff(diff, true, kindJson, kinds);
    CHECK(contains(kindJson.str(), "\"kind\":\"added\""));
    CHECK(contains(kindJson.str(), "\"kind\":\"removed\""));
    CHECK(!contains(kindJson.str(), "\"kind\":\"grown\""));

    diskmap_cli::SnapshotDiffFilter certain;
    certain.certain_only = true;
    std::ostringstream certainText;
    diskmap_cli::printSnapshotDiff(diff, false, certainText, certain);
    CHECK(contains(certainText.str(), "2 change(s) shown of 4"));
    CHECK(!contains(certainText.str(), "/removed"));
    CHECK(!contains(certainText.str(), "/maybe"));

    // Deltas: added 22, removed 11, grown 11, uncertain |22 - 11| = 11.
    diskmap_cli::SnapshotDiffFilter delta;
    delta.min_delta_bytes = 11;
    std::ostringstream deltaText;
    diskmap_cli::printSnapshotDiff(diff, false, deltaText, delta);
    CHECK(contains(deltaText.str(), "4 change(s) shown of 4"));
    delta.min_delta_bytes = 12;
    std::ostringstream smaller;
    diskmap_cli::printSnapshotDiff(diff, false, smaller, delta);
    CHECK(contains(smaller.str(), "1 change(s) shown of 4"));
    CHECK(contains(smaller.str(), "/added"));
    delta.min_delta_bytes = 23;
    std::ostringstream tooBig;
    diskmap_cli::printSnapshotDiff(diff, false, tooBig, delta);
    CHECK(contains(tooBig.str(), "0 change(s) shown of 4"));

    // An unknown metric can never prove a minimum delta.
    SnapshotDiff unknownMetric;
    unknownMetric.changes.push_back(
        change(SnapshotChangeKind::Grown, "/g", "/g", true, {}, true, false));
    diskmap_cli::SnapshotDiffFilter anyDelta;
    anyDelta.min_delta_bytes = 1;
    std::ostringstream hidden;
    diskmap_cli::printSnapshotDiff(unknownMetric, true, hidden, anyDelta);
    CHECK(contains(hidden.str(), "\"changes\":[]"));
}

} // namespace

class TestStorageCli : public QObject
{
    Q_OBJECT

private slots:
    void run();
};

namespace {

void testCleanupPlanReports() {
    diskmap::CleanupPlan plan;
    plan.reclaimable_bytes = 8192;
    plan.reclaimable_bytes_known = true;
    plan.scan_generation = 7;

    diskmap::CleanupTarget target;
    target.key = key("/tmp/dup-a");
    target.path = target.key.normalized_path;
    target.kind = FsKind::RegularFile;
    target.logical_size = 42;
    target.allocated_size = 4096;
    target.allocated_size_known = true;
    target.hard_link_count = 1;
    target.hard_link_count_known = true;
    target.scan_generation = 7;
    plan.targets.push_back(target);

    diskmap::CleanupRejectedTarget rejected;
    rejected.key = key("/tmp/protected");
    rejected.path = rejected.key.normalized_path;
    rejected.reason = diskmap::CleanupSkipReason::ProtectedRoot;
    rejected.message = "path is protected";
    plan.rejected.push_back(rejected);

    std::ostringstream text;
    diskmap_cli::printCleanupPlan(plan, false, text);
    const std::string renderedText = text.str();
    CHECK(contains(renderedText,
                   "Cleanup plan (dry run, nothing moved): 1 target(s), 1 rejected, "
                   "reclaimable 8192 byte(s)"));
    CHECK(contains(renderedText, "target  /tmp/dup-a  42 byte(s)"));
    CHECK(contains(renderedText, "rejected  /tmp/protected  protected-root  path is protected"));

    std::ostringstream json;
    diskmap_cli::printCleanupPlan(plan, true, json);
    const std::string renderedJson = json.str();
    CHECK(contains(renderedJson, "\"reclaimable_bytes\":8192"));
    CHECK(contains(renderedJson, "\"reclaimable_bytes_known\":true"));
    CHECK(contains(renderedJson, "\"scan_generation\":7"));
    CHECK(contains(renderedJson, "\"path\":\"/tmp/dup-a\""));
    CHECK(contains(renderedJson, "\"allocated_size\":4096"));
    CHECK(contains(renderedJson, "\"reason\":\"protected-root\""));

    const std::size_t open = renderedJson.find('{');
    CHECK(open != std::string::npos);
    CHECK(renderedJson.back() == '\n');
}

} // namespace

void TestStorageCli::run() {
    testEmptyReports();
    testSnapshotReports();
    testSnapshotDiffFilters();
    testDuplicateReports();
    testCleanupPlanReports();
}

QTEST_GUILESS_MAIN(TestStorageCli)

#include "test_storage_cli.moc"
