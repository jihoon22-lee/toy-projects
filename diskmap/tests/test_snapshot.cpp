// Tests for the bounded diskmap.snapshot/v1 contract and its diff projection.

#include "assert.hpp"
#include "diskmap/cleanup.hpp"
#include "diskmap/snapshot.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using diskmap::FileIdentity;
using diskmap::FsKind;
using diskmap::FsNode;
using diskmap::Snapshot;
using diskmap::SnapshotChange;
using diskmap::SnapshotChangeKind;
using diskmap::SnapshotError;
using diskmap::SnapshotLimits;

namespace {

FsNode file(std::string name, std::string path, std::uint64_t size, FileIdentity identity = {}) {
    FsNode node;
    node.name = std::move(name);
    node.path = std::move(path);
    node.size = size;
    node.logical_size_known = true;
    node.metadata.kind = FsKind::RegularFile;
    node.metadata.logical_size = size;
    node.metadata.identity = identity;
    node.metadata.complete = true;
    node.complete = true;
    return node;
}

FsNode directory(std::string name,
                 std::string path,
                 std::vector<FsNode> children,
                 FileIdentity identity = {}) {
    FsNode node;
    node.name = std::move(name);
    node.path = std::move(path);
    node.is_dir = true;
    node.logical_size_known = true;
    node.metadata.kind = FsKind::Directory;
    node.metadata.identity = identity;
    node.metadata.complete = true;
    node.complete = true;
    node.children = std::move(children);
    return node;
}

Snapshot snapshot(FsNode root) { return diskmap::snapshotFromNode(root); }

bool throwsSnapshotError(const std::function<void()>& operation) {
    try {
        operation();
    } catch (const SnapshotError&) {
        return true;
    }
    return false;
}

bool replaceFirst(std::string& value,
                  const std::string& search,
                  const std::string& replacement) {
    const std::size_t position = value.find(search);
    if (position == std::string::npos) {
        return false;
    }
    value.replace(position, search.size(), replacement);
    return true;
}

bool containsChange(const diskmap::SnapshotDiff& diff,
                    SnapshotChangeKind kind,
                    const std::string& path,
                    bool certain) {
    for (const SnapshotChange& change : diff.changes) {
        const std::string candidate = change.has_after ? change.after_key.normalized_path
                                                        : change.before_key.normalized_path;
        if (change.kind == kind && candidate == path && change.certain == certain) {
            return true;
        }
    }
    return false;
}

class SnapshotTempDirectory {
public:
    SnapshotTempDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
                / ("diskmap-snapshot-io-" + std::to_string(stamp));
        std::filesystem::create_directories(path_);
    }

    ~SnapshotTempDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void writeRaw(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path, std::ios::binary);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
}

bool hasSnapshotTemporary(const std::filesystem::path& directory) {
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; it != end;
         it.increment(error)) {
        if (error) {
            return true;
        }
        if (it->path().filename().string().rfind(".diskmap-snapshot-", 0) == 0) {
            return true;
        }
    }
    return static_cast<bool>(error);
}

void testSnapshotFileIo() {
    SnapshotTempDirectory temporary;
    const std::filesystem::path path = temporary.path() / "state.json";
    const Snapshot first = snapshot(directory(
        "root", "/file-io", {file("a", "/file-io/a", 3, FileIdentity{8, 1, true})},
        FileIdentity{8, 10, true}));
    diskmap::writeSnapshotAtomically(first, path);
    CHECK(!hasSnapshotTemporary(temporary.path()));
    const Snapshot loaded = diskmap::readSnapshotFile(path);
    CHECK_EQ(diskmap::serializeSnapshot(loaded), diskmap::serializeSnapshot(first));

#if defined(__linux__)
    struct stat status{};
    CHECK(::stat(path.c_str(), &status) == 0);
    if (::stat(path.c_str(), &status) == 0) {
        CHECK_EQ(static_cast<unsigned>(status.st_mode & 0777U), 0600U);
    }
#endif

    const Snapshot second = snapshot(directory(
        "root", "/file-io", {file("b", "/file-io/b", 4, FileIdentity{8, 2, true})},
        FileIdentity{8, 10, true}));
    diskmap::writeSnapshotAtomically(second, path);
    CHECK(!hasSnapshotTemporary(temporary.path()));
    CHECK_EQ(diskmap::serializeSnapshot(diskmap::readSnapshotFile(path)),
             diskmap::serializeSnapshot(second));

#if defined(__linux__)
    // Snapshot timestamp conversion clamps seconds outside the nanosecond
    // representation range instead of overflowing during destination checks.
    {
        constexpr std::int64_t kTimestampOverflowSeconds = 10'000'000'000LL;
        using SnapshotSeconds = decltype(std::declval<struct timespec>().tv_sec);
        const bool wideEnough =
            std::numeric_limits<SnapshotSeconds>::max() >= kTimestampOverflowSeconds
            && std::numeric_limits<SnapshotSeconds>::lowest()
                   <= -kTimestampOverflowSeconds;
        if (!wideEnough) {
            std::printf("SKIP snapshot timestamp overflow test: narrow time_t\n");
        } else {
            const auto setMtime = [&](std::int64_t seconds) {
                struct timespec times[2]{};
                times[0].tv_nsec = UTIME_OMIT;
                times[1].tv_sec = static_cast<SnapshotSeconds>(seconds);
                if (::utimensat(AT_FDCWD, path.c_str(), times, 0) != 0) {
                    return false;
                }
                struct stat observed{};
                return ::stat(path.c_str(), &observed) == 0
                       && observed.st_mtim.tv_sec
                              == static_cast<SnapshotSeconds>(seconds);
            };

            const bool futureSet = setMtime(kTimestampOverflowSeconds);
            if (!futureSet) {
                std::printf("SKIP future snapshot timestamp clamp: filesystem range\n");
            } else {
                diskmap::writeSnapshotAtomically(first, path);
                CHECK(!hasSnapshotTemporary(temporary.path()));

                const bool pastSet = setMtime(-kTimestampOverflowSeconds);
                if (!pastSet) {
                    std::printf("SKIP past snapshot timestamp clamp: filesystem range\n");
                }
                // Restore the baseline even when the filesystem cannot
                // represent the past timestamp. When it can, this write is
                // also the overflow-clamping exercise.
                diskmap::writeSnapshotAtomically(second, path);
                CHECK(!hasSnapshotTemporary(temporary.path()));
                CHECK_EQ(diskmap::serializeSnapshot(diskmap::readSnapshotFile(path)),
                         diskmap::serializeSnapshot(second));
            }
        }
    }
#endif

    const std::string firstJson = diskmap::serializeSnapshot(first);
    SnapshotLimits tooSmall;
    tooSmall.max_serialized_bytes = firstJson.size() - 1;
    CHECK(throwsSnapshotError([&] {
        diskmap::writeSnapshotAtomically(first, path, tooSmall);
    }));
    CHECK(!hasSnapshotTemporary(temporary.path()));
    CHECK_EQ(diskmap::serializeSnapshot(diskmap::readSnapshotFile(path)),
             diskmap::serializeSnapshot(second));

    const std::filesystem::path missing = temporary.path() / "missing.json";
    CHECK(throwsSnapshotError([&] { diskmap::readSnapshotFile(missing); }));
    const std::filesystem::path missingParent =
        temporary.path() / "missing-parent" / "state.json";
    CHECK(throwsSnapshotError([&] {
        diskmap::writeSnapshotAtomically(first, missingParent);
    }));
    CHECK(!std::filesystem::exists(missingParent));

    const std::filesystem::path parentFile = temporary.path() / "parent-file";
    writeRaw(parentFile, "not a directory");
    CHECK(throwsSnapshotError([&] {
        diskmap::writeSnapshotAtomically(first, parentFile / "state.json");
    }));
    CHECK(!hasSnapshotTemporary(temporary.path()));
    CHECK(throwsSnapshotError([&] { diskmap::readSnapshotFile(temporary.path()); }));

    const std::filesystem::path rootPath = temporary.path().root_path();
    if (!rootPath.empty()) {
        CHECK(throwsSnapshotError([&] { diskmap::readSnapshotFile(rootPath); }));
        CHECK(throwsSnapshotError([&] {
            diskmap::writeSnapshotAtomically(first, rootPath);
        }));
    }

    const std::filesystem::path alias = temporary.path() / "alias.json";
    std::error_code linkError;
    std::filesystem::create_symlink(path, alias, linkError);
    if (!linkError) {
        CHECK(throwsSnapshotError([&] { diskmap::readSnapshotFile(alias); }));
        CHECK(throwsSnapshotError([&] {
            diskmap::writeSnapshotAtomically(first, alias);
        }));
        CHECK(!hasSnapshotTemporary(temporary.path()));
        CHECK_EQ(diskmap::serializeSnapshot(diskmap::readSnapshotFile(path)),
                 diskmap::serializeSnapshot(second));
    }
    CHECK(throwsSnapshotError([&] {
        diskmap::writeSnapshotAtomically(first, temporary.path());
    }));

#if defined(__linux__)
    const std::filesystem::path fifo = temporary.path() / "state.fifo";
    if (::mkfifo(fifo.c_str(), 0600) == 0) {
        CHECK(throwsSnapshotError([&] { diskmap::readSnapshotFile(fifo); }));
        CHECK(throwsSnapshotError([&] {
            diskmap::writeSnapshotAtomically(first, fifo);
        }));
        CHECK(!hasSnapshotTemporary(temporary.path()));
    }

    const std::filesystem::path realParent = temporary.path() / "real-parent";
    const std::filesystem::path linkedParent = temporary.path() / "linked-parent";
    std::filesystem::create_directory(realParent);
    std::error_code parentLinkError;
    std::filesystem::create_directory_symlink(realParent, linkedParent,
                                               parentLinkError);
    if (!parentLinkError) {
        CHECK(throwsSnapshotError([&] {
            diskmap::writeSnapshotAtomically(first,
                                              linkedParent / "redirected.json");
        }));
        CHECK(!std::filesystem::exists(realParent / "redirected.json"));
        CHECK(!hasSnapshotTemporary(realParent));

        const std::filesystem::path directSnapshot = realParent / "direct.json";
        diskmap::writeSnapshotAtomically(first, directSnapshot);
        CHECK(throwsSnapshotError([&] {
            diskmap::readSnapshotFile(linkedParent / "direct.json");
        }));
    }

    const int lockedParent = ::open(temporary.path().c_str(), O_RDONLY | O_DIRECTORY);
    CHECK(lockedParent >= 0);
    if (lockedParent >= 0) {
        CHECK(::flock(lockedParent, LOCK_EX | LOCK_NB) == 0);
        CHECK(throwsSnapshotError([&] {
            diskmap::writeSnapshotAtomically(first,
                                              temporary.path() / "locked.json");
        }));
        CHECK(!std::filesystem::exists(temporary.path() / "locked.json"));
        CHECK(!hasSnapshotTemporary(temporary.path()));
        CHECK(::flock(lockedParent, LOCK_UN) == 0);
        CHECK(::close(lockedParent) == 0);
    }

    const std::filesystem::path unwritable = temporary.path() / "unwritable";
    std::filesystem::create_directory(unwritable);
    std::error_code permissionError;
    std::filesystem::permissions(unwritable,
                                 std::filesystem::perms::owner_read
                                     | std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace,
                                 permissionError);
    if (!permissionError && ::access(unwritable.c_str(), W_OK) != 0) {
        CHECK(throwsSnapshotError([&] {
            diskmap::writeSnapshotAtomically(first, unwritable / "state.json");
        }));
        CHECK(!hasSnapshotTemporary(unwritable));
    }
    if (!permissionError) {
        std::filesystem::permissions(unwritable,
                                     std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace,
                                     permissionError);
    }

    const std::filesystem::path inaccessible = temporary.path() / "inaccessible";
    std::filesystem::create_directory(inaccessible);
    permissionError.clear();
    std::filesystem::permissions(inaccessible, std::filesystem::perms::owner_read,
                                  std::filesystem::perm_options::replace,
                                  permissionError);
    if (!permissionError && ::access(inaccessible.c_str(), X_OK) != 0) {
        CHECK(throwsSnapshotError([&] {
            diskmap::writeSnapshotAtomically(first, inaccessible / "state.json");
        }));
        CHECK(throwsSnapshotError([&] {
            diskmap::writeSnapshotAtomically(first,
                                              inaccessible / "nested" / "state.json");
        }));
        CHECK(throwsSnapshotError([&] {
            diskmap::readSnapshotFile(inaccessible / "state.json");
        }));
    }
    if (!permissionError) {
        std::filesystem::permissions(inaccessible,
                                     std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace,
                                     permissionError);
    }

    const std::filesystem::path unreadable = temporary.path() / "unreadable.json";
    writeRaw(unreadable, firstJson);
    permissionError.clear();
    std::filesystem::permissions(unreadable, std::filesystem::perms::none,
                                  std::filesystem::perm_options::replace,
                                  permissionError);
    if (!permissionError && ::access(unreadable.c_str(), R_OK) != 0) {
        CHECK(throwsSnapshotError([&] { diskmap::readSnapshotFile(unreadable); }));
    }
    if (!permissionError) {
        std::filesystem::permissions(unreadable,
                                     std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace,
                                     permissionError);
    }
#endif

    const std::filesystem::path oversized = temporary.path() / "oversized.json";
    writeRaw(oversized, "{}\n");
    SnapshotLimits tiny;
    tiny.max_serialized_bytes = 2;
    CHECK(throwsSnapshotError([&] { diskmap::readSnapshotFile(oversized, tiny); }));
    const std::filesystem::path malformed = temporary.path() / "malformed.json";
    writeRaw(malformed, "{}");
    CHECK(throwsSnapshotError([&] { diskmap::readSnapshotFile(malformed); }));
}

} // namespace

int main() {
    // --- canonical round-trip retains FsNode facts but drops worker generation ---
    {
        FsNode root = directory("root", "/snapshot", {
            file("z.txt", "/snapshot/z.txt", 9, FileIdentity{7, 2, true}),
            file("a.txt", "/snapshot/a.txt", 3, FileIdentity{7, 1, true}),
        });
        root.scan_generation = 71;
        root.children[0].scan_generation = 72;
        root.children[0].metadata.modified_ns = -17;
        root.children[0].metadata.modified_time_known = true;
        root.children[0].metadata.permissions = 0640;
        root.children[0].metadata.permissions_known = true;

        const Snapshot original = snapshot(std::move(root));
        const std::string rendered = diskmap::serializeSnapshot(original);
        CHECK(rendered.find("diskmap.snapshot/v1") != std::string::npos);
        CHECK(rendered.find("scan_generation") == std::string::npos);
        CHECK(rendered.find("/snapshot/a.txt") < rendered.find("/snapshot/z.txt"));

        const Snapshot parsed = diskmap::parseSnapshot(rendered);
        CHECK_EQ(parsed.nodes_retained, static_cast<std::size_t>(3));
        CHECK_EQ(parsed.root.scan_generation, static_cast<std::uint64_t>(0));
        CHECK_EQ(parsed.root.children[1].scan_generation, static_cast<std::uint64_t>(0));
        CHECK_EQ(parsed.root.children[1].metadata.modified_ns, static_cast<std::int64_t>(-17));
        CHECK_EQ(parsed.root.children[1].metadata.permissions, static_cast<std::uint32_t>(0640));
        CHECK_EQ(diskmap::serializeSnapshot(parsed), rendered);
    }

    // --- model and input bounds retain a visible partial directory ---
    {
        const FsNode root = directory("root", "/bounded", {
            file("one", "/bounded/one", 1),
            file("two", "/bounded/two", 2),
            file("three", "/bounded/three", 3),
        });
        SnapshotLimits limits;
        limits.max_nodes = 2;
        const Snapshot bounded = diskmap::snapshotFromNode(root, limits);
        CHECK(bounded.truncated);
        CHECK(!bounded.complete);
        CHECK_EQ(bounded.nodes_retained, static_cast<std::size_t>(2));
        CHECK(!bounded.root.complete);
        CHECK_EQ(bounded.root.children.size(), static_cast<std::size_t>(1));
        CHECK(diskmap::parseSnapshot(diskmap::serializeSnapshot(bounded, limits), limits).truncated);

        SnapshotLimits oneNode;
        oneNode.max_nodes = 1;
        const Snapshot rootOnlyByNodes = diskmap::snapshotFromNode(root, oneNode);
        CHECK(rootOnlyByNodes.truncated);
        CHECK(rootOnlyByNodes.root.children.empty());

        const Snapshot deep = snapshot(directory(
            "root", "/depth-bound", {file("child", "/depth-bound/child", 1)}));
        SnapshotLimits depthZero;
        depthZero.max_depth = 0;
        CHECK(throwsSnapshotError([&] {
            diskmap::serializeSnapshot(deep, depthZero);
        }));
        const std::string deepJson = diskmap::serializeSnapshot(deep);
        CHECK(throwsSnapshotError([&] {
            diskmap::parseSnapshot(deepJson, depthZero);
        }));

        SnapshotLimits shallow;
        shallow.max_depth = 0;
        const Snapshot rootOnly = diskmap::snapshotFromNode(root, shallow);
        CHECK(rootOnly.truncated);
        CHECK(rootOnly.root.children.empty());
        CHECK(throwsSnapshotError([&] { diskmap::serializeSnapshot(bounded, SnapshotLimits{1}); }));

        SnapshotLimits tiny;
        tiny.max_serialized_bytes = 32;
        CHECK(throwsSnapshotError([&] { diskmap::serializeSnapshot(bounded, tiny); }));
    }

    // --- truncation appends evidence, and followed links require target facts ---
    {
        FsNode annotated = directory(
            "root", "/annotated", {file("child", "/annotated/child", 1)});
        annotated.error = "scanner warning";
        SnapshotLimits depthZero;
        depthZero.max_depth = 0;
        const Snapshot partial = diskmap::snapshotFromNode(annotated, depthZero);
        CHECK(partial.truncated);
        CHECK(partial.root.error.find("scanner warning; snapshot depth limit reached")
              != std::string::npos);

        FsNode followed = file("link", "/followed", 1);
        followed.followed = true;
        followed.has_target_metadata = true;
        followed.target_metadata.complete = false;
        const Snapshot followedSnapshot = diskmap::snapshotFromNode(followed);
        CHECK(!followedSnapshot.complete);
        CHECK(!followedSnapshot.truncated);
    }

    // --- strict parser rejects duplicate keys and unsupported schema ---
    {
        CHECK(throwsSnapshotError([] {
            diskmap::parseSnapshot("{\"complete\":true,\"complete\":true}");
        }));
        CHECK(throwsSnapshotError([] {
            diskmap::parseSnapshot("{\"complete\":true,\"node_count\":0,\"root\":{},"
                                   "\"schema_version\":\"diskmap.snapshot/v9\",\"truncated\":false}");
        }));
    }

    // --- complete snapshots classify path and identity changes deterministically ---
    {
        const Snapshot before = snapshot(directory("root", "/diff", {
            file("stable", "/diff/stable", 10, FileIdentity{1, 1, true}),
            file("grown", "/diff/grown", 10, FileIdentity{1, 2, true}),
            file("shrunk", "/diff/shrunk", 30, FileIdentity{1, 3, true}),
            file("removed", "/diff/removed", 4, FileIdentity{1, 4, true}),
            file("old-name", "/diff/old-name", 12, FileIdentity{1, 5, true}),
        }, FileIdentity{1, 100, true}));
        const Snapshot after = snapshot(directory("root", "/diff", {
            file("stable", "/diff/stable", 10, FileIdentity{1, 1, true}),
            file("grown", "/diff/grown", 20, FileIdentity{1, 2, true}),
            file("shrunk", "/diff/shrunk", 15, FileIdentity{1, 3, true}),
            file("added", "/diff/added", 8, FileIdentity{1, 6, true}),
            file("new-name", "/diff/new-name", 12, FileIdentity{1, 5, true}),
        }, FileIdentity{1, 100, true}));
        const diskmap::SnapshotDiff diff = diskmap::diffSnapshots(before, after);
        CHECK(diff.complete);
        CHECK(!diff.uncertain);
        CHECK_EQ(diff.compared_nodes, static_cast<std::size_t>(5));
        CHECK(containsChange(diff, SnapshotChangeKind::Grown, "/diff/grown", true));
        CHECK(containsChange(diff, SnapshotChangeKind::Shrunk, "/diff/shrunk", true));
        CHECK(containsChange(diff, SnapshotChangeKind::Removed, "/diff/removed", true));
        CHECK(containsChange(diff, SnapshotChangeKind::Added, "/diff/added", true));
        CHECK(containsChange(diff, SnapshotChangeKind::Moved, "/diff/new-name", true));
    }

    // --- incomplete and unknown evidence remains explicitly uncertain ---
    {
        FsNode oldRoot = directory("root", "/uncertain", {
            file("known", "/uncertain/known", 10, FileIdentity{2, 1, true}),
            file("missing", "/uncertain/missing", 2, FileIdentity{2, 2, true}),
        }, FileIdentity{2, 100, true});
        oldRoot.children[0].logical_size_known = false;
        oldRoot.complete = false;
        const Snapshot before = snapshot(std::move(oldRoot));
        const Snapshot after = snapshot(directory("root", "/uncertain", {
            file("known", "/uncertain/known", 20, FileIdentity{2, 1, true}),
        }, FileIdentity{2, 100, true}));

        const diskmap::SnapshotDiff diff = diskmap::diffSnapshots(before, after);
        CHECK(!diff.complete);
        CHECK(diff.uncertain);
        CHECK(containsChange(diff, SnapshotChangeKind::Uncertain, "/uncertain/known", false));
        CHECK(containsChange(diff, SnapshotChangeKind::Removed, "/uncertain/missing", false));
    }

    // Loaded snapshot-wide incompleteness must reach every retained node, so a
    // complete-looking child cannot bypass cleanup's fail-closed guard.
    {
        const auto verifyLoadedIncomplete = [](bool truncated) {
            Snapshot persisted = snapshot(directory(
                "root", "/offline", {file("a", "/offline/a", 1,
                                             FileIdentity{2, 121, true})},
                FileIdentity{2, 120, true}));
            persisted.complete = false;
            persisted.truncated = truncated;

            const Snapshot loaded = diskmap::parseSnapshot(
                diskmap::serializeSnapshot(persisted));
            CHECK(!loaded.complete);
            CHECK_EQ(loaded.truncated, truncated);
            CHECK(loaded.root.complete);
            CHECK(loaded.root.children.front().complete);

            const diskmap::ScanResult evidence =
                diskmap::scanEvidenceFromSnapshot(loaded, 77);
            CHECK_EQ(evidence.generation, std::uint64_t(77));
            CHECK(!evidence.root.complete);
            CHECK(evidence.root.error.find("snapshot inventory is incomplete")
                  != std::string::npos);
            CHECK(!evidence.root.children.front().complete);
            CHECK_EQ(evidence.root.scan_generation, std::uint64_t(77));
            CHECK_EQ(evidence.root.children.front().scan_generation,
                     std::uint64_t(77));

            const diskmap::NodeKey childKey =
                diskmap::nodeKey(evidence.root.children.front());
            const diskmap::CleanupPlan plan =
                diskmap::planCleanup(evidence, {childKey});
            CHECK(plan.targets.empty());
            CHECK_EQ(plan.rejected.size(), static_cast<std::size_t>(1));
            if (!plan.rejected.empty()) {
                CHECK_EQ(plan.rejected.front().key, childKey);
                CHECK_EQ(plan.rejected.front().reason,
                         diskmap::CleanupSkipReason::IncompleteScan);
            }
        };

        verifyLoadedIncomplete(false);
        verifyLoadedIncomplete(true);
    }

    // --- absence evidence requires the opposite snapshot to be complete ---
    {
        Snapshot incompleteBefore = snapshot(directory(
            "root", "/opposite-before", {}, FileIdentity{2, 200, true}));
        incompleteBefore.complete = false;
        const Snapshot completeAfter = snapshot(directory(
            "root", "/opposite-before",
            {file("added", "/opposite-before/added", 3, FileIdentity{2, 201, true})},
            FileIdentity{2, 200, true}));
        const diskmap::SnapshotDiff addedDiff =
            diskmap::diffSnapshots(incompleteBefore, completeAfter);
        CHECK(containsChange(addedDiff, SnapshotChangeKind::Added,
                             "/opposite-before/added", false));

        const Snapshot completeBefore = snapshot(directory(
            "root", "/opposite-after",
            {file("removed", "/opposite-after/removed", 3, FileIdentity{2, 202, true})},
            FileIdentity{2, 203, true}));
        Snapshot incompleteAfter = snapshot(directory(
            "root", "/opposite-after", {}, FileIdentity{2, 203, true}));
        incompleteAfter.complete = false;
        const diskmap::SnapshotDiff removedDiff =
            diskmap::diffSnapshots(completeBefore, incompleteAfter);
        CHECK(containsChange(removedDiff, SnapshotChangeKind::Removed,
                             "/opposite-after/removed", false));
    }

    // Equal placeholder bytes are not evidence of equality when the selected
    // metric is unknown on both sides.
    {
        FsNode beforeRoot = directory(
            "root", "/equal-unknown",
            {file("same", "/equal-unknown/same", 0,
                  FileIdentity{2, 130, true})},
            FileIdentity{2, 131, true});
        beforeRoot.children.front().logical_size_known = false;
        FsNode afterRoot = beforeRoot;
        const diskmap::SnapshotDiff diff = diskmap::diffSnapshots(
            snapshot(std::move(beforeRoot)), snapshot(std::move(afterRoot)));
        CHECK(!diff.complete);
        CHECK(diff.uncertain);
        CHECK(containsChange(diff, SnapshotChangeKind::Uncertain,
                             "/equal-unknown/same", false));
    }

    // --- absence certainty also requires a known size metric ---
    {
        FsNode unknownRemovedRoot = directory(
            "root", "/unknown-removed",
            {file("removed", "/unknown-removed/removed", 3, FileIdentity{2, 210, true})},
            FileIdentity{2, 211, true});
        unknownRemovedRoot.children.front().logical_size_known = false;
        const Snapshot unknownRemovedBefore = snapshot(std::move(unknownRemovedRoot));
        const Snapshot emptyAfter = snapshot(directory(
            "root", "/unknown-removed", {}, FileIdentity{2, 211, true}));
        const diskmap::SnapshotDiff removedDiff =
            diskmap::diffSnapshots(unknownRemovedBefore, emptyAfter);
        CHECK(containsChange(removedDiff, SnapshotChangeKind::Removed,
                             "/unknown-removed/removed", false));

        const Snapshot emptyBefore = snapshot(directory(
            "root", "/unknown-added", {}, FileIdentity{2, 212, true}));
        FsNode unknownAddedRoot = directory(
            "root", "/unknown-added",
            {file("added", "/unknown-added/added", 3, FileIdentity{2, 213, true})},
            FileIdentity{2, 212, true});
        unknownAddedRoot.children.front().logical_size_known = false;
        const Snapshot unknownAddedAfter = snapshot(std::move(unknownAddedRoot));
        const diskmap::SnapshotDiff addedDiff =
            diskmap::diffSnapshots(emptyBefore, unknownAddedAfter);
        CHECK(containsChange(addedDiff, SnapshotChangeKind::Added,
                             "/unknown-added/added", false));
    }

    // --- complete=true cannot hide incomplete/error/cycle evidence ---
    {
        FsNode incomplete = directory("root", "/invalid-complete", {},
                                      FileIdentity{3, 1, true});
        incomplete.complete = false;
        Snapshot snapshotValue;
        snapshotValue.root = incomplete;
        snapshotValue.complete = true;
        CHECK(throwsSnapshotError([&] { diskmap::serializeSnapshot(snapshotValue); }));

        Snapshot truncatedValue = snapshot(directory("root", "/truncated", {},
                                                     FileIdentity{3, 2, true}));
        truncatedValue.complete = true;
        truncatedValue.truncated = true;
        CHECK(throwsSnapshotError([&] { diskmap::serializeSnapshot(truncatedValue); }));

        const std::string partial = diskmap::serializeSnapshot(snapshot(incomplete));
        const std::size_t completeFlag = partial.find("\"complete\":false");
        CHECK(completeFlag != std::string::npos);
        if (completeFlag != std::string::npos) {
            std::string malformed = partial;
            malformed.replace(completeFlag, std::string("\"complete\":false").size(),
                              "\"complete\":true");
            CHECK(throwsSnapshotError([&] { diskmap::parseSnapshot(malformed); }));
        }

        const FsNode duplicate = directory("root", "/duplicate", {
            file("first", "/duplicate/same", 1),
            file("second", "/duplicate/same", 2),
        }, FileIdentity{4, 1, true});
        CHECK(throwsSnapshotError([&] { diskmap::snapshotFromNode(duplicate); }));

        FsNode malformedFile = file("file", "/malformed-file", 1);
        malformedFile.children.push_back(file("child", "/malformed-file/child", 1));
        CHECK(throwsSnapshotError([&] { diskmap::snapshotFromNode(malformedFile); }));
        Snapshot malformedSnapshot;
        malformedSnapshot.root = malformedFile;
        malformedSnapshot.complete = false;
        CHECK(throwsSnapshotError([&] {
            diskmap::serializeSnapshot(malformedSnapshot);
        }));

        FsNode cycleEvidence = directory("root", "/cycle-evidence", {},
                                         FileIdentity{4, 3, true});
        cycleEvidence.cycle_skipped = true;
        Snapshot cycleSnapshot;
        cycleSnapshot.root = cycleEvidence;
        cycleSnapshot.complete = true;
        CHECK(throwsSnapshotError([&] { diskmap::serializeSnapshot(cycleSnapshot); }));
        FsNode mountEvidence = directory("root", "/mount-evidence", {},
                                         FileIdentity{4, 4, true});
        mountEvidence.mount_boundary_skipped = true;
        Snapshot mountSnapshot;
        mountSnapshot.root = mountEvidence;
        mountSnapshot.complete = true;
        CHECK(throwsSnapshotError([&] { diskmap::serializeSnapshot(mountSnapshot); }));
        FsNode errorEvidence = directory("root", "/error-evidence", {},
                                         FileIdentity{4, 5, true});
        errorEvidence.error = "permission denied";
        Snapshot errorSnapshot;
        errorSnapshot.root = errorEvidence;
        errorSnapshot.complete = true;
        CHECK(throwsSnapshotError([&] { diskmap::serializeSnapshot(errorSnapshot); }));

        Snapshot duplicateSnapshot;
        duplicateSnapshot.root = directory("root", "/duplicate-direct", {
            file("first", "/duplicate-direct/same", 1),
            file("second", "/duplicate-direct/same", 2),
        }, FileIdentity{4, 2, true});
        CHECK(throwsSnapshotError([&] {
            diskmap::diffSnapshots(duplicateSnapshot, duplicateSnapshot);
        }));
    }

    // --- UTF-8 is validated, and escaped surrogate pairs normalize to UTF-8 ---
    {
        FsNode unicode = directory("root", "/utf8", {
            file("😀", "/utf8/😀", 1),
        }, FileIdentity{5, 1, true});
        const std::string rendered = diskmap::serializeSnapshot(snapshot(std::move(unicode)));
        const std::string emoji = "😀";
        const std::size_t emojiPosition = rendered.find(emoji);
        CHECK(emojiPosition != std::string::npos);
        if (emojiPosition != std::string::npos) {
            std::string escaped = rendered;
            escaped.replace(emojiPosition, emoji.size(), "\\uD83D\\uDE00");
            CHECK_EQ(diskmap::serializeSnapshot(diskmap::parseSnapshot(escaped)), rendered);

            std::string invalidJson = rendered;
            const std::size_t pathPosition = invalidJson.find("/utf8/");
            CHECK(pathPosition != std::string::npos);
            if (pathPosition != std::string::npos) {
                invalidJson.insert(pathPosition + std::string("/utf8/").size(), 1,
                                   static_cast<char>(0xff));
                CHECK(throwsSnapshotError([&] { diskmap::parseSnapshot(invalidJson); }));
            }
        }

        FsNode invalid = file("bad", std::string("/bad\xff", 5), 1);
        CHECK(throwsSnapshotError([&] { diskmap::serializeSnapshot(snapshot(std::move(invalid))); }));
        FsNode truncated = file("bad", std::string("/bad\xc2", 5), 1);
        CHECK(throwsSnapshotError(
            [&] { diskmap::serializeSnapshot(snapshot(std::move(truncated))); }));

        const std::string utf8BoundaryCases =
            "\xe0\xa0\x80"  // U+0800: lower bound for three-byte sequences.
            "\xed\x9f\xbf"  // U+D7FF: upper bound before surrogate code points.
            "\xee\x80\x80"  // U+E000: first code point after surrogates.
            "\xf4\x8f\xbf\xbf"; // U+10FFFF: upper Unicode scalar value.
        const Snapshot utf8Boundaries = snapshot(
            directory("root", "/utf8-boundaries",
                      {file(utf8BoundaryCases, "/utf8-boundaries/edge", 1)},
                      FileIdentity{5, 3, true}));
        const std::string utf8BoundaryJson = diskmap::serializeSnapshot(utf8Boundaries);
        CHECK_EQ(diskmap::serializeSnapshot(diskmap::parseSnapshot(utf8BoundaryJson)),
                 utf8BoundaryJson);

        FsNode unknownIdentityBefore = file("same", "/unknown-identity", 10);
        FsNode unknownIdentityAfter = file("same", "/unknown-identity", 20);
        const Snapshot unknownBefore = snapshot(directory(
            "root", "/unknown-identity-root", {std::move(unknownIdentityBefore)},
            FileIdentity{5, 2, true}));
        const Snapshot unknownAfter = snapshot(directory(
            "root", "/unknown-identity-root", {std::move(unknownIdentityAfter)},
            FileIdentity{5, 2, true}));
        const diskmap::SnapshotDiff unknownDiff =
            diskmap::diffSnapshots(unknownBefore, unknownAfter);
        CHECK(containsChange(unknownDiff, SnapshotChangeKind::Uncertain,
                             "/unknown-identity", false));
    }

    // --- parser exercises the complete JSON scalar/escape contract ---
    {
        FsNode symlink = file("link", "/parser-kinds/link", 1, FileIdentity{6, 2, true});
        symlink.metadata.kind = FsKind::Symlink;
        FsNode other = file("other", "/parser-kinds/other", 2, FileIdentity{6, 3, true});
        other.metadata.kind = FsKind::Other;
        const std::string specialName = "\"\\/\b\f\n\r\t";
        FsNode special = file(specialName, "/parser-kinds/special", 3,
                              FileIdentity{6, 4, true});
        const Snapshot allKinds = snapshot(directory(
            "root", "/parser-kinds",
            {file("regular", "/parser-kinds/regular", 1, FileIdentity{6, 1, true}),
             directory("nested", "/parser-kinds/nested",
                       {file("nested-file", "/parser-kinds/nested/file", 4,
                             FileIdentity{6, 5, true})},
                       FileIdentity{6, 6, true}),
             std::move(symlink), std::move(other), std::move(special)},
            FileIdentity{6, 7, true}));
        const std::string allKindsJson = diskmap::serializeSnapshot(allKinds);
        const Snapshot allKindsParsed = diskmap::parseSnapshot(allKindsJson);
        CHECK_EQ(diskmap::serializeSnapshot(allKindsParsed), allKindsJson);
        CHECK_EQ(allKindsParsed.root.children.size(), static_cast<std::size_t>(5));

        SnapshotLimits serializedBound;
        serializedBound.max_serialized_bytes = allKindsJson.size() - 1;
        CHECK(throwsSnapshotError([&] {
            diskmap::parseSnapshot(allKindsJson, serializedBound);
        }));
        SnapshotLimits parserDepth;
        parserDepth.max_depth = 1;
        CHECK(throwsSnapshotError([&] {
            diskmap::parseSnapshot(allKindsJson, parserDepth);
        }));

        std::string wrongChildren = allKindsJson;
        CHECK(replaceFirst(wrongChildren, "\"children\":[", "\"children\":{}"));
        CHECK(throwsSnapshotError([&] { diskmap::parseSnapshot(wrongChildren); }));
        std::string wrongMetadata = allKindsJson;
        CHECK(replaceFirst(wrongMetadata, "\"metadata\":{", "\"metadata\":[]"));
        CHECK(throwsSnapshotError([&] { diskmap::parseSnapshot(wrongMetadata); }));
        std::string truncatedComplete = allKindsJson;
        CHECK(replaceFirst(truncatedComplete, "\"truncated\":false",
                           "\"truncated\":true"));
        CHECK(throwsSnapshotError([&] { diskmap::parseSnapshot(truncatedComplete); }));

        std::string scalarJson = allKindsJson;
        CHECK(replaceFirst(scalarJson, "\"complete\":true", "\"complete\":1"));
        CHECK(throwsSnapshotError([&] { diskmap::parseSnapshot(scalarJson); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot(""); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("{}"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("[]"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("!"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("{}{}"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("{1:2}"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("{\"x\" 1}"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("{\"x\":1 \"y\":2}"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("[1 2]"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("tru"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("fal"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("nul"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("-"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("01"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("1.0"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("18446744073709551616"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("-9223372036854775809"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("\"\\\""); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("\"\\q\""); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("\"abc"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("\"\\"); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("\"\x01\""); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("\"\\u12\""); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("\"\\u12xz\""); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("\"\\uD800\""); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("\"\\uD800\\u0041\""); }));
        CHECK(throwsSnapshotError([] { diskmap::parseSnapshot("\"\\uDC00\""); }));
        SnapshotLimits shortString;
        shortString.max_string_bytes = 3;
        CHECK(throwsSnapshotError([&] { diskmap::parseSnapshot("\"abcd\"", shortString); }));
        SnapshotLimits shortEscape;
        shortEscape.max_string_bytes = 0;
        CHECK(throwsSnapshotError([&] {
            diskmap::parseSnapshot("\"\\u0041\"", shortEscape);
        }));

        const Snapshot unicodeSnapshot = snapshot(directory(
            "root", "/parser-unicode", {file("😀", "/parser-unicode/😀", 1)},
            FileIdentity{6, 8, true}));
        std::string unicodeEscapes = diskmap::serializeSnapshot(unicodeSnapshot);
        const std::size_t rawName = unicodeEscapes.find("😀");
        CHECK(rawName != std::string::npos);
        if (rawName != std::string::npos) {
            unicodeEscapes.replace(rawName, std::string("😀").size(),
                                   "\\u0041\\u00e9\\u20ac\\uD83D\\uDE00");
            const Snapshot parsed = diskmap::parseSnapshot(unicodeEscapes);
            CHECK(parsed.root.children.size() == static_cast<std::size_t>(1));
        }

        std::string nullValue = allKindsJson;
        CHECK(replaceFirst(nullValue, "\"error\":\"\"", "\"error\":null"));
        CHECK(throwsSnapshotError([&] { diskmap::parseSnapshot(nullValue); }));
        std::string invalidKind = allKindsJson;
        CHECK(replaceFirst(invalidKind, "\"kind\":\"regular_file\"",
                           "\"kind\":\"unknown\""));
        CHECK(throwsSnapshotError([&] { diskmap::parseSnapshot(invalidKind); }));
        std::string wrongNumber = allKindsJson;
        CHECK(replaceFirst(wrongNumber, "\"node_count\":7", "\"node_count\":true"));
        CHECK(throwsSnapshotError([&] { diskmap::parseSnapshot(wrongNumber); }));
        std::string wrongNodeCount = allKindsJson;
        CHECK(replaceFirst(wrongNodeCount, "\"node_count\":7", "\"node_count\":1"));
        CHECK(throwsSnapshotError([&] { diskmap::parseSnapshot(wrongNodeCount); }));
        std::string unknownKey = allKindsJson;
        CHECK(replaceFirst(unknownKey, "\"truncated\":false", "\"extra\":false"));
        CHECK(throwsSnapshotError([&] { diskmap::parseSnapshot(unknownKey); }));
        std::string nonDirectoryChildren = allKindsJson;
        CHECK(replaceFirst(nonDirectoryChildren, "\"is_dir\":true", "\"is_dir\":false"));
        CHECK(throwsSnapshotError([&] { diskmap::parseSnapshot(nonDirectoryChildren); }));

        SnapshotLimits oneNode;
        oneNode.max_nodes = 1;
        CHECK(throwsSnapshotError([&] { diskmap::parseSnapshot(allKindsJson, oneNode); }));
        SnapshotLimits noString;
        noString.max_string_bytes = 0;
        CHECK(throwsSnapshotError([&] { diskmap::serializeSnapshot(allKinds, noString); }));
        SnapshotLimits noNodes;
        noNodes.max_nodes = 0;
        CHECK(throwsSnapshotError([&] { diskmap::snapshotFromNode(allKinds.root, noNodes); }));
        SnapshotLimits noBytes;
        noBytes.max_serialized_bytes = 0;
        CHECK(throwsSnapshotError([&] { diskmap::serializeSnapshot(allKinds, noBytes); }));
    }

    testSnapshotFileIo();
    return testSummary();
}
