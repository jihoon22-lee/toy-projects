// Tests for diskmap::scan (src/core/scanner.hpp / scanner.cpp), using
// FakeFsSource (tests/fake_fs.hpp) so the walk never touches a real
// filesystem.

#include "assert.hpp"
#include "fake_fs.hpp"
#include "diskmap/scanner.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using diskmap::FsNode;
using diskmap::ScanOptions;
using diskmap::ScanResult;
using diskmap::scan;
using diskmap_test::FakeFsSource;
using diskmap_test::makeDirEntry;
using diskmap_test::makeFileEntry;

int main() {
    // --- basic walk: nested dirs, min_size filtering, error collection ---
    {
        FakeFsSource fs;
        fs.addListing("/fake/root", {
            makeDirEntry("dirA"),
            makeDirEntry("dirB"),
            makeFileEntry("file1.txt", 50),
            makeFileEntry("file2.txt", 5), // below min_size, should be dropped
            makeFileEntry("symlinkFile", 999, /*symlink=*/true),
        });
        fs.addListing("/fake/root/dirA", {
            makeFileEntry("fileA1", 100),
            makeFileEntry("fileA2", 10), // exactly at min_size: kept (< is exclusive)
        });
        fs.addError("/fake/root/dirB", "permission denied: dirB");

        ScanOptions options;
        options.min_size = 10;
        std::vector<std::pair<std::size_t, std::size_t>> progressCalls;
        const ScanResult result = scan(fs, "/fake/root", options,
                                        [&](std::size_t dirs, std::size_t files) {
                                            progressCalls.emplace_back(dirs, files);
                                        });

        CHECK_EQ(result.root.name, std::string("root")); // last path component
        CHECK_EQ(result.root.path, std::string("/fake/root"));
        CHECK(result.root.is_dir);

        // dirB errored before listing, so it never got expanded/counted;
        // dirA and root both listed successfully.
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(2));
        // files_scanned: file1.txt + fileA1 + fileA2 (file2.txt filtered, symlink skipped)
        CHECK_EQ(result.files_scanned, static_cast<std::size_t>(3));

        CHECK_EQ(result.errors.size(), static_cast<std::size_t>(1));
        if (!result.errors.empty()) {
            CHECK(result.errors[0].find("dirB") != std::string::npos);
        }

        const FsNode* dirA = diskmap::findChild(result.root, "dirA");
        const FsNode* dirB = diskmap::findChild(result.root, "dirB");
        const FsNode* file1 = diskmap::findChild(result.root, "file1.txt");
        CHECK(dirA != nullptr);
        CHECK(dirB != nullptr);
        CHECK(file1 != nullptr);
        CHECK(diskmap::findChild(result.root, "file2.txt") == nullptr);   // filtered by min_size
        CHECK(diskmap::findChild(result.root, "symlinkFile") == nullptr); // symlink, not followed

        if (dirA) {
            CHECK_EQ(dirA->path, std::string("/fake/root/dirA"));
            CHECK_EQ(dirA->size, static_cast<std::uint64_t>(110)); // 100 + 10
            CHECK_EQ(dirA->children.size(), static_cast<std::size_t>(2));
            CHECK_EQ(dirA->metadata.logical_size, static_cast<std::uint64_t>(0));
        }
        if (dirB) {
            CHECK_EQ(dirB->path, std::string("/fake/root/dirB"));
            CHECK_EQ(dirB->size, static_cast<std::uint64_t>(0)); // never expanded
            CHECK_EQ(dirB->children.size(), static_cast<std::size_t>(0));
            CHECK(!dirB->complete);
            CHECK_EQ(dirB->error, std::string("permission denied: dirB"));
        }
        if (file1) {
            CHECK_EQ(file1->path, std::string("/fake/root/file1.txt"));
            CHECK_EQ(file1->size, static_cast<std::uint64_t>(50));
            CHECK_EQ(file1->metadata.logical_size, static_cast<std::uint64_t>(50));
        }
        CHECK_EQ(result.root.size, static_cast<std::uint64_t>(160)); // 50 + 110

        // top files across the whole tree, largest first.
        const std::vector<const FsNode*> top = diskmap::topFiles(result.root, 10);
        CHECK_EQ(top.size(), static_cast<std::size_t>(3));
        CHECK_EQ(top[0]->name, std::string("fileA1")); // 100
        CHECK_EQ(top[1]->name, std::string("file1.txt")); // 50
        CHECK_EQ(top[2]->name, std::string("fileA2")); // 10

        // progress is reported once per directory actually listed (root, dirA),
        // and the last call reflects the final totals.
        CHECK_EQ(progressCalls.size(), static_cast<std::size_t>(2));
        if (progressCalls.size() == 2) {
            CHECK_EQ(progressCalls.back().first, static_cast<std::size_t>(2));
            CHECK_EQ(progressCalls.back().second, static_cast<std::size_t>(3));
        }
    }

    // --- follow_symlinks toggles whether symlinked entries are kept ---
    {
        FakeFsSource fs;
        fs.addListing("/sym/root", {
            makeFileEntry("plain.txt", 3),
            makeFileEntry("linked.txt", 7, /*symlink=*/true),
        });

        ScanOptions withoutFollow;
        const ScanResult skipped = scan(fs, "/sym/root", withoutFollow);
        CHECK(diskmap::findChild(skipped.root, "linked.txt") == nullptr);
        CHECK(diskmap::findChild(skipped.root, "plain.txt") != nullptr);
        CHECK_EQ(skipped.files_scanned, static_cast<std::size_t>(1));

        ScanOptions withFollow;
        withFollow.follow_symlinks = true;
        const ScanResult followed = scan(fs, "/sym/root", withFollow);
        const FsNode* linked = diskmap::findChild(followed.root, "linked.txt");
        CHECK(linked != nullptr);
        if (linked) {
            CHECK_EQ(linked->size, static_cast<std::uint64_t>(7));
        }
        CHECK_EQ(followed.files_scanned, static_cast<std::size_t>(2));
    }

    // --- root.name derives from the last path component, trailing slash included ---
    {
        FakeFsSource fs;
        fs.addListing("/a/b/named", {});
        fs.addListing("/a/b/named/", {}); // scanner trims the trailing slash before use
        fs.addListing("just_a_name", {});

        CHECK_EQ(scan(fs, "/a/b/named", ScanOptions{}).root.name, std::string("named"));
        CHECK_EQ(scan(fs, "/a/b/named/", ScanOptions{}).root.name, std::string("named"));
        CHECK_EQ(scan(fs, "just_a_name", ScanOptions{}).root.name, std::string("just_a_name"));
    }

    // --- empty rootPath: joinPath's base.empty() branch builds bare names ---
    {
        FakeFsSource fs;
        fs.addListing("", {makeDirEntry("child")});
        fs.addListing("child", {makeFileEntry("leaf.txt", 3)}); // joinPath("", "child") == "child"

        const ScanResult result = scan(fs, "", ScanOptions{});
        CHECK_EQ(result.root.name, std::string(""));
        const FsNode* child = diskmap::findChild(result.root, "child");
        CHECK(child != nullptr);
        if (child) {
            CHECK_EQ(child->size, static_cast<std::uint64_t>(3));
        }
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(2));
    }

    // --- max_depth: 0 stops after root's own listing, 1 allows one more level ---
    {
        FakeFsSource fs;
        fs.addListing("/depth/root", {makeDirEntry("subdirA")});
        fs.addListing("/depth/root/subdirA", {makeDirEntry("subsubdir")});
        fs.addListing("/depth/root/subdirA/subsubdir", {makeFileEntry("deepfile", 42)});

        {
            ScanOptions opts;
            opts.max_depth = 0;
            const ScanResult result = scan(fs, "/depth/root", opts);
            CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(1)); // root only
            const FsNode* subdirA = diskmap::findChild(result.root, "subdirA");
            CHECK(subdirA != nullptr);
            if (subdirA) {
                CHECK_EQ(subdirA->children.size(), static_cast<std::size_t>(0)); // never expanded
            }
        }
        {
            ScanOptions opts;
            opts.max_depth = 1;
            const ScanResult result = scan(fs, "/depth/root", opts);
            CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(2)); // root, subdirA
            const FsNode* subdirA = diskmap::findChild(result.root, "subdirA");
            CHECK(subdirA != nullptr);
            if (subdirA) {
                const FsNode* subsubdir = diskmap::findChild(*subdirA, "subsubdir");
                CHECK(subsubdir != nullptr);
                if (subsubdir) {
                    CHECK_EQ(subsubdir->children.size(), static_cast<std::size_t>(0)); // pruned
                }
            }
        }
        {
            ScanOptions opts; // default max_depth = -1, unlimited
            const ScanResult result = scan(fs, "/depth/root", opts);
            CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(3));
            CHECK_EQ(result.root.size, static_cast<std::uint64_t>(42));
        }
    }

    // --- errors don't stop the walk: a mid-tree failure still lets siblings finish ---
    {
        FakeFsSource fs;
        fs.addListing("/multi/root", {
            makeDirEntry("ok1"),
            makeDirEntry("bad"),
            makeDirEntry("ok2"),
        });
        fs.addListing("/multi/root/ok1", {makeFileEntry("f1", 1)});
        fs.addError("/multi/root/bad", "boom");
        fs.addListing("/multi/root/ok2", {makeFileEntry("f2", 2)});

        const ScanResult result = scan(fs, "/multi/root", ScanOptions{});
        CHECK_EQ(result.errors.size(), static_cast<std::size_t>(1));
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(3)); // root, ok1, ok2
        CHECK_EQ(result.files_scanned, static_cast<std::size_t>(2));
        CHECK_EQ(result.root.size, static_cast<std::uint64_t>(3));
    }

    // --- reasonably deep chain: confirms the walk copes with many levels ---
    // (the implementation is documented as an iterative stack-based walk,
    // not recursive, so this should complete without incident).
    {
        FakeFsSource fs;
        constexpr int kChainLength = 500;
        std::string path = "/chain";
        fs.addListing(path, {makeDirEntry("d")});
        for (int i = 0; i < kChainLength; ++i) {
            const std::string next = path + "/d";
            if (i + 1 == kChainLength) {
                fs.addListing(next, {makeFileEntry("leaf", 1)});
            } else {
                fs.addListing(next, {makeDirEntry("d")});
            }
            path = next;
        }

        const ScanResult result = scan(fs, "/chain", ScanOptions{});
        CHECK_EQ(result.dirs_scanned, static_cast<std::size_t>(kChainLength + 1));
        CHECK_EQ(result.files_scanned, static_cast<std::size_t>(1));
        CHECK_EQ(result.root.size, static_cast<std::uint64_t>(1));
    }

    return testSummary();
}
