// Linux-only recoverable Trash tests.  Every fixture lives below one private
// temporary directory and the API is exercised without Qt or a shell.

#include "assert.hpp"
#include "diskmap/cleanup.hpp"
#include "diskmap/fs_source.hpp"
#include "diskmap/trash.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

class ScopedTempDirectory {
public:
    ScopedTempDirectory() {
        std::error_code error;
        const fs::path parent = fs::temp_directory_path(error);
        if (error) {
            return;
        }
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned attempt = 0; attempt != 100; ++attempt) {
            const fs::path candidate =
                parent / ("diskmap_trash_test_" + std::to_string(stamp) + "_"
                          + std::to_string(attempt));
            error.clear();
            if (fs::create_directory(candidate, error)) {
                path_ = candidate;
                return;
            }
            if (error && error != std::make_error_code(std::errc::file_exists)) {
                return;
            }
        }
    }

    ~ScopedTempDirectory() {
        if (!path_.empty()) {
            std::error_code error;
            // remove_all removes a symlink entry itself and does not traverse
            // its target, keeping teardown within this fixture.
            fs::remove_all(path_, error);
        }
    }

    bool valid() const { return !path_.empty(); }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

bool writeFile(const fs::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return output.good();
}

std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

diskmap::CleanupTarget targetFor(const fs::path& path,
                                 std::uint64_t generation = 1) {
    diskmap::RealFsSource source;
    const fs::path absolute = fs::absolute(path).lexically_normal();
    const diskmap::FsMetadata metadata = source.inspect(absolute, false);
    diskmap::CleanupTarget target;
    target.path = absolute;
    target.kind = metadata.kind;
    target.identity = metadata.identity;
    target.logical_size = metadata.logical_size;
    target.allocated_size = metadata.allocated_size;
    target.allocated_size_known = metadata.allocated_size_known;
    target.hard_link_count = metadata.hard_link_count;
    target.hard_link_count_known = metadata.hard_link_count_known;
    target.symlink = metadata.kind == diskmap::FsKind::Symlink;
    target.scan_generation = generation;
    target.key.normalized_path = absolute.generic_string();
    target.key.kind = metadata.kind;
    target.key.identity = metadata.identity;
    return target;
}

diskmap::CleanupPlan planFor(std::vector<diskmap::CleanupTarget> targets,
                             std::uint64_t generation = 1) {
    diskmap::CleanupPlan plan;
    plan.targets = std::move(targets);
    plan.scan_generation = generation;
    return plan;
}

diskmap::TrashOptions optionsFor(const fs::path& root) {
    diskmap::TrashOptions options;
    options.data_home = root / "xdg data home";
    return options;
}

bool linuxTrashReady(const diskmap::CleanupTarget& target,
                     const diskmap::TrashOptions& options) {
#if defined(__linux__)
    const diskmap::TrashCapability capability =
        diskmap::inspectTrashCapability(target, options);
    CHECK(capability.available);
    CHECK(capability.status == diskmap::TrashStatus::Ready);
    return capability.available && capability.status == diskmap::TrashStatus::Ready;
#else
    (void)target;
    (void)options;
    return false;
#endif
}

void checkNoPermanentDelete(const fs::path& path,
                            const diskmap::TrashOptions& options) {
    const diskmap::CleanupTarget target = targetFor(path);
    diskmap::CleanupPlan plan = planFor({target});
    diskmap::TrashOptions bounded = options;
    bounded.max_targets = 0;
    const std::vector<diskmap::TrashReceipt> receipts =
        diskmap::movePlanToTrash(plan, bounded);
    CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
    if (!receipts.empty()) {
        CHECK(receipts[0].status == diskmap::TrashStatus::InvalidRequest);
    }
    CHECK(fs::is_regular_file(path));
}

void testCapabilityAndInvalidRestore(const fs::path& root) {
    const fs::path path = root / "capability.txt";
    CHECK(writeFile(path, "capability"));
    const diskmap::TrashOptions options = optionsFor(root);
    const diskmap::CleanupTarget target = targetFor(path);
    const diskmap::TrashCapability capability =
        diskmap::inspectTrashCapability(target, options);
#if defined(__linux__)
    CHECK(capability.available);
    CHECK(capability.status == diskmap::TrashStatus::Ready);
    CHECK(capability.trash_root == options.data_home / "Trash");
#else
    CHECK(!capability.available);
    CHECK(capability.status == diskmap::TrashStatus::UnsupportedPlatform);
#endif

    diskmap::CleanupTarget invalid = target;
    invalid.identity.valid = false;
    CHECK(diskmap::inspectTrashCapability(invalid, options).status
          == diskmap::TrashStatus::InvalidRequest);
    invalid = target;
    invalid.path = "relative-file";
    CHECK(diskmap::inspectTrashCapability(invalid, options).status
          == diskmap::TrashStatus::InvalidRequest);

    const diskmap::TrashReceipt empty = diskmap::restoreFromTrash("", options);
    CHECK(empty.status == diskmap::TrashStatus::InvalidRequest);
    const diskmap::TrashReceipt traversal =
        diskmap::restoreFromTrash("diskmap-../outside", options);
    CHECK(traversal.status == diskmap::TrashStatus::InvalidRequest);

    // A missing token is distinct from an unavailable backend.  Provision
    // the private Trash directories first so this assertion exercises token
    // lookup rather than directory setup failure.
    std::error_code setupError;
    fs::create_directories(options.data_home / "Trash" / "files", setupError);
    CHECK(!setupError);
    setupError.clear();
    fs::create_directories(options.data_home / "Trash" / "info", setupError);
    CHECK(!setupError);
    const diskmap::TrashReceipt missing =
        diskmap::restoreFromTrash("diskmap-missing-token", options);
#if defined(__linux__)
    CHECK(missing.status == diskmap::TrashStatus::MissingToken);
#else
    CHECK(missing.status == diskmap::TrashStatus::UnsupportedPlatform);
#endif
    checkNoPermanentDelete(path, options);

    // Backend setup failure must also leave the reviewed source untouched;
    // there is no copy-then-delete or permanent-delete fallback.
    const fs::path blockedDataHome = root / "data home is a file";
    CHECK(writeFile(blockedDataHome, "not a directory"));
    diskmap::TrashOptions broken = options;
    broken.data_home = blockedDataHome;
    const auto failedBackend = diskmap::movePlanToTrash(planFor({target}), broken);
    CHECK_EQ(failedBackend.size(), static_cast<std::size_t>(1));
    if (!failedBackend.empty()) {
        CHECK(failedBackend[0].status == diskmap::TrashStatus::IoError);
    }
    CHECK(fs::is_regular_file(path));
}

void testMoveInfoEncodingAndTokenRestore(const fs::path& root) {
    const fs::path path = root / "name with % percent\nnewline.txt";
    const std::string contents = "recoverable payload";
    CHECK(writeFile(path, contents));
    const diskmap::TrashOptions options = optionsFor(root);
    const diskmap::CleanupTarget target = targetFor(path);
    if (!linuxTrashReady(target, options)) {
        return;
    }

    const std::vector<diskmap::TrashReceipt> receipts =
        diskmap::movePlanToTrash(planFor({target}), options);
    CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
    if (receipts.size() != 1) {
        return;
    }
    const diskmap::TrashReceipt& moved = receipts[0];
    CHECK(moved.status == diskmap::TrashStatus::Moved);
    CHECK(moved.succeeded());
    CHECK(!moved.restore_token.empty());
    CHECK(moved.restore_token.find('/') == std::string::npos);
    CHECK(!fs::exists(path));
    CHECK(fs::is_regular_file(moved.trashed_path));
    CHECK_EQ(readFile(moved.trashed_path), contents);

    const fs::path infoPath =
        options.data_home / "Trash" / "info" / (moved.restore_token + ".trashinfo");
    CHECK(fs::is_regular_file(infoPath));
    const std::string info = readFile(infoPath);
    CHECK(info.rfind("[Trash Info]\nPath=", 0) == 0);
    CHECK(info.find("%20") != std::string::npos);
    CHECK(info.find("%25") != std::string::npos);
    CHECK(info.find("%0A") != std::string::npos);
    CHECK(info.find("\nDeletionDate=") != std::string::npos);

    const diskmap::TrashReceipt restored =
        diskmap::restoreFromTrash(moved.restore_token, options);
    CHECK(restored.status == diskmap::TrashStatus::Restored);
    CHECK(restored.succeeded());
    CHECK(fs::is_regular_file(path));
    CHECK_EQ(readFile(path), contents);
    CHECK(!fs::exists(moved.trashed_path));
    CHECK(!fs::exists(infoPath));
    CHECK(diskmap::restoreFromTrash(moved.restore_token, options).status
          == diskmap::TrashStatus::MissingToken);
}

void testRestoreNeverOverwrites(const fs::path& root) {
    const fs::path path = root / "destination.txt";
    CHECK(writeFile(path, "original"));
    const diskmap::TrashOptions options = optionsFor(root);
    const diskmap::CleanupTarget target = targetFor(path);
    if (!linuxTrashReady(target, options)) {
        return;
    }
    const auto receipts = diskmap::movePlanToTrash(planFor({target}), options);
    CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
    if (receipts.size() != 1 || receipts[0].status != diskmap::TrashStatus::Moved) {
        return;
    }
    const auto& moved = receipts[0];
    CHECK(writeFile(path, "replacement"));
    const diskmap::TrashReceipt conflict =
        diskmap::restoreFromTrash(moved.restore_token, options);
    CHECK(conflict.status == diskmap::TrashStatus::DestinationExists);
    CHECK_EQ(readFile(path), "replacement");
    CHECK(fs::exists(moved.trashed_path));
    CHECK(fs::exists(options.data_home / "Trash" / "info"
                     / (moved.restore_token + ".trashinfo")));

    std::error_code error;
    fs::remove(path, error);
    CHECK(!error);
    const diskmap::TrashReceipt restored =
        diskmap::restoreFromTrash(moved.restore_token, options);
    CHECK(restored.status == diskmap::TrashStatus::Restored);
    CHECK_EQ(readFile(path), "original");
}

void testMutationRevalidation(const fs::path& root) {
    const diskmap::TrashOptions options = optionsFor(root);

    // Size mutation retains identity but must be rejected immediately before
    // the anchored rename.
    {
        const fs::path path = root / "mutated-size.txt";
        CHECK(writeFile(path, "before"));
        const auto target = targetFor(path);
        if (!linuxTrashReady(target, options)) {
            return;
        }
        CHECK(writeFile(path, "after with a different size"));
        const auto receipts = diskmap::movePlanToTrash(planFor({target}), options);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (!receipts.empty()) {
            CHECK(receipts[0].status == diskmap::TrashStatus::RevalidationFailed);
        }
        CHECK(fs::is_regular_file(path));
    }

    // Replacing the pathname with a separately-created inode is an identity
    // mutation even when the replacement has the same content and size.
    {
        const fs::path path = root / "mutated-identity.txt";
        const fs::path replacement = root / "mutated-identity.new";
        CHECK(writeFile(path, "same-size"));
        const auto target = targetFor(path);
        CHECK(writeFile(replacement, "same-size"));
        std::error_code error;
        fs::rename(replacement, path, error);
        CHECK(!error);
        const auto receipts = diskmap::movePlanToTrash(planFor({target}), options);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (!receipts.empty()) {
            CHECK(receipts[0].status == diskmap::TrashStatus::RevalidationFailed);
        }
        CHECK(fs::is_regular_file(path));
    }

    // A type replacement must never be treated as the reviewed regular file.
    {
        const fs::path path = root / "mutated-type";
        CHECK(writeFile(path, "file"));
        const auto target = targetFor(path);
        std::error_code error;
        fs::remove(path, error);
        CHECK(!error);
        fs::create_directory(path, error);
        CHECK(!error);
        const auto receipts = diskmap::movePlanToTrash(planFor({target}), options);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (!receipts.empty()) {
            CHECK(receipts[0].status == diskmap::TrashStatus::RevalidationFailed);
        }
        CHECK(fs::is_directory(path));
        fs::remove(path, error);
    }

    // Adding a hard-link alias changes nlink and is rejected even though the
    // selected pathname still points at the same inode.
    {
        const fs::path path = root / "mutated-links.txt";
        const fs::path alias = root / "mutated-links.alias";
        CHECK(writeFile(path, "hardlink"));
        const auto target = targetFor(path);
        std::error_code error;
        fs::create_hard_link(path, alias, error);
        if (error) {
            std::printf("SKIP hard-link mutation test: %s\n", error.message().c_str());
        } else {
            const auto receipts = diskmap::movePlanToTrash(planFor({target}), options);
            CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
            if (!receipts.empty()) {
                CHECK(receipts[0].status == diskmap::TrashStatus::RevalidationFailed);
            }
            CHECK(fs::is_regular_file(path));
            fs::remove(alias, error);
        }
    }
}

void testStaleIncompleteAndBoundedPlans(const fs::path& root) {
    const fs::path path = root / "plan-safety.txt";
    CHECK(writeFile(path, "safe"));
    const auto target = targetFor(path, 2);
    const diskmap::TrashOptions options = optionsFor(root);
    if (!linuxTrashReady(target, options)) {
        return;
    }

    diskmap::CleanupPlan stale = planFor({target}, 1);
    const auto staleReceipts = diskmap::movePlanToTrash(stale, options);
    CHECK_EQ(staleReceipts.size(), static_cast<std::size_t>(1));
    if (!staleReceipts.empty()) {
        CHECK(staleReceipts[0].status == diskmap::TrashStatus::InvalidRequest);
    }
    CHECK(fs::is_regular_file(path));

    diskmap::CleanupTarget unknown = target;
    unknown.identity.valid = false;
    const auto unknownReceipts =
        diskmap::movePlanToTrash(planFor({unknown}, 2), options);
    CHECK_EQ(unknownReceipts.size(), static_cast<std::size_t>(1));
    if (!unknownReceipts.empty()) {
        CHECK(unknownReceipts[0].status == diskmap::TrashStatus::InvalidRequest);
    }
    CHECK(fs::is_regular_file(path));

    diskmap::CleanupPlan bounded = planFor({target, target}, 2);
    diskmap::TrashOptions one = options;
    one.max_targets = 1;
    const auto boundedReceipts = diskmap::movePlanToTrash(bounded, one);
    CHECK_EQ(boundedReceipts.size(), static_cast<std::size_t>(1));
    if (!boundedReceipts.empty()) {
        CHECK(boundedReceipts[0].status == diskmap::TrashStatus::InvalidRequest);
    }
    CHECK(fs::is_regular_file(path));
}

void testSymlinkMovesLinkOnly(const fs::path& root) {
    const fs::path targetPath = root / "symlink-target.txt";
    const fs::path linkPath = root / "link name %";
    CHECK(writeFile(targetPath, "target remains"));
    std::error_code error;
    fs::create_symlink(targetPath, linkPath, error);
    if (error) {
        std::printf("SKIP symlink Trash test: %s\n", error.message().c_str());
        return;
    }

    const diskmap::TrashOptions options = optionsFor(root);
    const auto target = targetFor(linkPath);
    if (!linuxTrashReady(target, options)) {
        return;
    }
    const auto receipts = diskmap::movePlanToTrash(planFor({target}), options);
    CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
    if (receipts.size() != 1) {
        return;
    }
    const auto& moved = receipts[0];
    CHECK(moved.status == diskmap::TrashStatus::Moved);
    CHECK(!fs::exists(linkPath));
    CHECK(fs::is_symlink(moved.trashed_path));
    CHECK(fs::exists(targetPath));
    CHECK(fs::read_symlink(moved.trashed_path) == targetPath);

    const auto restored = diskmap::restoreFromTrash(moved.restore_token, options);
    CHECK(restored.status == diskmap::TrashStatus::Restored);
    CHECK(fs::is_symlink(linkPath));
    CHECK(fs::read_symlink(linkPath) == targetPath);
    CHECK_EQ(readFile(targetPath), "target remains");
}

void testMultiTargetPartialOutcome(const fs::path& root) {
    const fs::path firstPath = root / "first.txt";
    const fs::path secondPath = root / "second.txt";
    CHECK(writeFile(firstPath, "first"));
    CHECK(writeFile(secondPath, "second"));
    const diskmap::TrashOptions options = optionsFor(root);
    const auto first = targetFor(firstPath);
    const auto second = targetFor(secondPath);
    if (!linuxTrashReady(first, options)) {
        return;
    }
    CHECK(writeFile(secondPath, "second target changed"));
    const auto receipts = diskmap::movePlanToTrash(planFor({first, second}), options);
    CHECK_EQ(receipts.size(), static_cast<std::size_t>(2));
    if (receipts.size() != 2) {
        return;
    }
    CHECK(receipts[0].status == diskmap::TrashStatus::Moved);
    CHECK(receipts[1].status == diskmap::TrashStatus::RevalidationFailed);
    CHECK(!fs::exists(firstPath));
    CHECK(fs::is_regular_file(secondPath));
    const auto restored = diskmap::restoreFromTrash(receipts[0].restore_token, options);
    CHECK(restored.status == diskmap::TrashStatus::Restored);
    CHECK_EQ(readFile(firstPath), "first");
    CHECK_EQ(readFile(secondPath), "second target changed");
}

} // namespace

int main() {
#if !defined(__linux__)
    std::printf("SKIP recoverable Trash tests: Linux backend is unavailable\n");
    return 0;
#else
    ScopedTempDirectory temp;
    CHECK(temp.valid());
    if (!temp.valid()) {
        return testSummary();
    }

    testCapabilityAndInvalidRestore(temp.path());
    testMoveInfoEncodingAndTokenRestore(temp.path());
    testRestoreNeverOverwrites(temp.path());
    testMutationRevalidation(temp.path());
    testStaleIncompleteAndBoundedPlans(temp.path());
    testSymlinkMovesLinkOnly(temp.path());
    testMultiTargetPartialOutcome(temp.path());
    return testSummary();
#endif
}
