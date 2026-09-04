// Linux-only recoverable Trash tests.  Every fixture lives below one private
// temporary directory and the API is exercised without Qt or a shell.

#include "assert.hpp"
#include "diskmap/cleanup.hpp"
#include "diskmap/fs_source.hpp"
#include "diskmap/trash.hpp"

#if defined(__linux__)
#include "../src/trash_linux_internal.hpp"
#endif

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

namespace fs = std::filesystem;

namespace {

class ScopedTempDirectory {
public:
    explicit ScopedTempDirectory(fs::path parent = {}) {
        std::error_code error;
        if (parent.empty()) {
            parent = fs::temp_directory_path(error);
        }
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

std::uint64_t deviceFor(const fs::path& path) {
#if defined(__linux__)
    struct stat status{};
    if (::stat(path.c_str(), &status) == 0) {
        return static_cast<std::uint64_t>(status.st_dev);
    }
#else
    (void)path;
#endif
    return 0;
}

bool writeFile(const fs::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return output.good();
}

#if defined(__linux__)
enum class MutationScenarioKind {
    BlockMetadataReserve,
    BlockPayloadMove,
    MutateMoved,
    MutateMovedAndBlockRollback,
    BlockMetadataFinalize,
    DetachTrashInfoAfterFinalize,
    DeleteMovedAndBlockRollback,
    MutateMovedAndDetachParent,
    DetachParentDuringMoveRollback,
    BlockPayloadRestore,
    DetachRestoreParent,
    DetachRestoreParentAndBlockRollback,
    DetachTrashFilesDuringRestore,
};

struct MutationScenario {
    MutationScenarioKind kind = MutationScenarioKind::MutateMoved;
    fs::path auxiliary_path;
    fs::path observed_path;
    std::error_code error;
    bool invoked = false;
};

thread_local MutationScenario* currentMutationScenario = nullptr;

void mutateTrashOperation(diskmap::detail::TrashMutationTestPoint point,
                          const fs::path& original,
                          const fs::path& trashed) noexcept {
    MutationScenario& scenario = *currentMutationScenario;
    scenario.invoked = true;
    if (point == diskmap::detail::TrashMutationTestPoint::BeforeMetadataReserve) {
        if (scenario.kind == MutationScenarioKind::BlockMetadataReserve) {
            scenario.auxiliary_path = trashed;
            if (!writeFile(trashed, "metadata reservation collision")) {
                scenario.error = std::make_error_code(std::errc::io_error);
            }
        }
        return;
    }
    if (point == diskmap::detail::TrashMutationTestPoint::BeforePayloadMove) {
        if (scenario.kind == MutationScenarioKind::BlockPayloadMove) {
            scenario.auxiliary_path = trashed;
            if (!writeFile(trashed, "payload destination collision")) {
                scenario.error = std::make_error_code(std::errc::io_error);
            }
        }
        return;
    }
    if (point == diskmap::detail::TrashMutationTestPoint::BeforeMoveRollback) {
        if (scenario.kind != MutationScenarioKind::DetachParentDuringMoveRollback) {
            return;
        }
        fs::rename(original.parent_path(), scenario.auxiliary_path, scenario.error);
        if (!scenario.error) {
            fs::create_directory(original.parent_path(), scenario.error);
        }
        return;
    }
    if (point == diskmap::detail::TrashMutationTestPoint::MetadataFinalized) {
        if (scenario.kind != MutationScenarioKind::DetachTrashInfoAfterFinalize) {
            return;
        }
        const fs::path info = trashed.parent_path();
        const std::string infoName = trashed.filename().string();
        constexpr const char* kInfoSuffix = ".trashinfo";
        const std::string token =
            infoName.substr(0, infoName.size() - std::char_traits<char>::length(kInfoSuffix));
        scenario.observed_path = info.parent_path() / "files" / token;
        fs::rename(info, scenario.auxiliary_path, scenario.error);
        if (scenario.error) {
            return;
        }
        fs::create_directory(info, scenario.error);
        if (!scenario.error
            && !writeFile(scenario.auxiliary_path / (token + ".tmp"),
                          "metadata rollback collision")) {
            scenario.error = std::make_error_code(std::errc::io_error);
        }
        return;
    }
    if (point == diskmap::detail::TrashMutationTestPoint::PayloadMoved) {
        if (scenario.kind == MutationScenarioKind::BlockMetadataFinalize) {
            scenario.auxiliary_path =
                trashed.parent_path().parent_path() / "info"
                / (trashed.filename().string() + ".trashinfo");
            if (!writeFile(scenario.auxiliary_path, "reserved by a concurrent writer")) {
                scenario.error = std::make_error_code(std::errc::io_error);
            }
            return;
        }
        if (scenario.kind == MutationScenarioKind::DeleteMovedAndBlockRollback) {
            if (!fs::remove(trashed, scenario.error) || scenario.error
                || !writeFile(original, "replacement after payload removal")) {
                if (!scenario.error) {
                    scenario.error = std::make_error_code(std::errc::io_error);
                }
            }
            return;
        }
        if (scenario.kind == MutationScenarioKind::MutateMovedAndDetachParent) {
            if (!writeFile(trashed, "changed before detached-parent rollback")) {
                scenario.error = std::make_error_code(std::errc::io_error);
                return;
            }
            fs::rename(original.parent_path(), scenario.auxiliary_path,
                       scenario.error);
            if (!scenario.error) {
                fs::create_directory(original.parent_path(), scenario.error);
            }
            return;
        }
        if (scenario.kind == MutationScenarioKind::DetachParentDuringMoveRollback) {
            if (!writeFile(trashed, "changed before rollback identity recheck")) {
                scenario.error = std::make_error_code(std::errc::io_error);
            }
            return;
        }
        if (scenario.kind != MutationScenarioKind::MutateMoved
            && scenario.kind != MutationScenarioKind::MutateMovedAndBlockRollback) {
            return;
        }
        if (scenario.kind == MutationScenarioKind::MutateMovedAndBlockRollback
            && (!fs::remove(trashed, scenario.error) || scenario.error)) {
            if (!scenario.error) {
                scenario.error = std::make_error_code(std::errc::io_error);
            }
            return;
        }
        if (!writeFile(trashed, "changed after the reviewed move")) {
            scenario.error = std::make_error_code(std::errc::io_error);
            return;
        }
        if (scenario.kind == MutationScenarioKind::MutateMovedAndBlockRollback
            && !writeFile(original, "replacement at the reviewed name")) {
            scenario.error = std::make_error_code(std::errc::io_error);
        }
        return;
    }
    if (point == diskmap::detail::TrashMutationTestPoint::BeforePayloadRestore) {
        if (scenario.kind == MutationScenarioKind::BlockPayloadRestore
            && !writeFile(original, "restore destination collision")) {
            scenario.error = std::make_error_code(std::errc::io_error);
        }
        return;
    }
    if (scenario.kind == MutationScenarioKind::DetachTrashFilesDuringRestore) {
        const fs::path files = trashed.parent_path();
        fs::rename(files, scenario.auxiliary_path, scenario.error);
        if (!scenario.error) {
            fs::create_directory(files, scenario.error);
        }
        return;
    }
    if (scenario.kind != MutationScenarioKind::DetachRestoreParent
        && scenario.kind != MutationScenarioKind::DetachRestoreParentAndBlockRollback) {
        return;
    }
    fs::rename(original.parent_path(), scenario.auxiliary_path, scenario.error);
    if (scenario.error) {
        return;
    }
    fs::create_directory(original.parent_path(), scenario.error);
    if (!scenario.error
        && scenario.kind == MutationScenarioKind::DetachRestoreParentAndBlockRollback
        && !writeFile(trashed, "rollback destination blocker")) {
        scenario.error = std::make_error_code(std::errc::io_error);
    }
}

class ScopedTrashMutationHook {
public:
    explicit ScopedTrashMutationHook(MutationScenario& scenario) {
        currentMutationScenario = &scenario;
        diskmap::detail::setTrashMutationTestHook(&mutateTrashOperation);
    }

    ~ScopedTrashMutationHook() {
        diskmap::detail::setTrashMutationTestHook(nullptr);
        currentMutationScenario = nullptr;
    }
};
#endif

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

bool ensurePrivateTrash(const diskmap::TrashOptions& options) {
    std::error_code error;
    fs::create_directories(options.data_home / "Trash" / "files", error);
    if (error) {
        return false;
    }
    fs::create_directories(options.data_home / "Trash" / "info", error);
    return !error;
}

std::string kindName(diskmap::FsKind kind) {
    switch (kind) {
    case diskmap::FsKind::RegularFile: return "file";
    case diskmap::FsKind::Directory: return "directory";
    case diskmap::FsKind::Symlink: return "symlink";
    case diskmap::FsKind::Other: return "other";
    }
    return "other";
}

std::string restoreInfo(const std::string& encodedPath,
                        const diskmap::CleanupTarget& target,
                        const std::string& kind = {}) {
    return "[Trash Info]\nPath=" + encodedPath
           + "\nDeletionDate=2026-09-04T00:00:00\nX-DiskMap-Device="
           + std::to_string(target.identity.device) + "\nX-DiskMap-File="
           + std::to_string(target.identity.file) + "\nX-DiskMap-Kind="
           + (kind.empty() ? kindName(target.kind) : kind) + "\n";
}

bool writeRestoreInfo(const diskmap::TrashOptions& options,
                      const std::string& token,
                      const std::string& content) {
    return writeFile(options.data_home / "Trash" / "info"
                         / (token + ".trashinfo"),
                     content);
}

std::string restoreInfoFields(const std::string& encodedPath,
                              const std::string& device,
                              const std::string& file,
                              const std::string& kind) {
    return "[Trash Info]\nPath=" + encodedPath
           + "\nDeletionDate=2026-09-04T00:00:00\nX-DiskMap-Device="
           + device + "\nX-DiskMap-File=" + file + "\nX-DiskMap-Kind="
           + kind + "\n";
}

class ScopedEnvironment {
public:
    ScopedEnvironment(const char* name, const std::string& value)
        : name_(name), had_value_(std::getenv(name) != nullptr),
          old_value_(had_value_ ? std::getenv(name) : "") {
        ::setenv(name_.c_str(), value.c_str(), 1);
    }

    ScopedEnvironment(const char* name, std::nullptr_t)
        : name_(name), had_value_(std::getenv(name) != nullptr),
          old_value_(had_value_ ? std::getenv(name) : "") {
        ::unsetenv(name_.c_str());
    }

    ~ScopedEnvironment() {
        if (had_value_) {
            ::setenv(name_.c_str(), old_value_.c_str(), 1);
        } else {
            ::unsetenv(name_.c_str());
        }
    }

private:
    std::string name_;
    bool had_value_;
    std::string old_value_;
};

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
    const auto blockedCapability =
        diskmap::inspectTrashCapability(target, broken);
    CHECK(!blockedCapability.available);
    CHECK(blockedCapability.status == diskmap::TrashStatus::IoError);
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

void testStatusTokensAndEnvironment(const fs::path& root) {
    const std::vector<std::pair<diskmap::TrashStatus, const char*>> statuses = {
        {diskmap::TrashStatus::Ready, "ready"},
        {diskmap::TrashStatus::Moved, "moved"},
        {diskmap::TrashStatus::Restored, "restored"},
        {diskmap::TrashStatus::UnsupportedPlatform, "unsupported-platform"},
        {diskmap::TrashStatus::InvalidRequest, "invalid-request"},
        {diskmap::TrashStatus::RevalidationFailed, "revalidation-failed"},
        {diskmap::TrashStatus::DifferentFilesystem, "different-filesystem"},
        {diskmap::TrashStatus::DestinationExists, "destination-exists"},
        {diskmap::TrashStatus::MissingToken, "missing-token"},
        {diskmap::TrashStatus::IoError, "io-error"},
    };
    for (const auto& item : statuses) {
        CHECK_EQ(std::string(diskmap::trashStatusName(item.first)),
                 std::string(item.second));
    }
    CHECK_EQ(std::string(diskmap::trashStatusName(
                 static_cast<diskmap::TrashStatus>(255))),
             std::string("unknown"));

    const diskmap::TrashOptions options = optionsFor(root);
    CHECK(ensurePrivateTrash(options));
    const std::vector<std::string> invalidTokens = {
        "",
        "not-diskmap",
        "diskmap-../escape",
        "diskmap-!",
        "diskmap-" + std::string(121, 'a'),
    };
    for (const std::string& token : invalidTokens) {
        CHECK(diskmap::restoreFromTrash(token, options).status
              == diskmap::TrashStatus::InvalidRequest);
    }
    CHECK(diskmap::restoreFromTrash("diskmap-", options).status
          == diskmap::TrashStatus::MissingToken);

    const fs::path source = root / "environment-source.txt";
    CHECK(writeFile(source, "environment"));
    const diskmap::CleanupTarget target = targetFor(source);

    // Empty options use XDG_DATA_HOME first, then HOME/.local/share, and
    // refuse to guess when both environment variables are unavailable.
    const fs::path xdg = root / "environment-xdg";
    const fs::path home = root / "environment-home";
    std::error_code error;
    fs::create_directory(home, error);
    CHECK(!error);
    {
        ScopedEnvironment xdgEnvironment("XDG_DATA_HOME", xdg.string());
        ScopedEnvironment homeEnvironment("HOME", home.string());
        diskmap::TrashOptions emptyOptions;
        const auto capability = diskmap::inspectTrashCapability(target, emptyOptions);
        CHECK(capability.status == diskmap::TrashStatus::Ready);
        CHECK(capability.trash_root == xdg / "Trash");
    }
    {
        ScopedEnvironment xdgEnvironment("XDG_DATA_HOME", nullptr);
        ScopedEnvironment homeEnvironment("HOME", home.string());
        diskmap::TrashOptions emptyOptions;
        const auto capability = diskmap::inspectTrashCapability(target, emptyOptions);
        CHECK(capability.status == diskmap::TrashStatus::Ready);
        CHECK(capability.trash_root == home / ".local" / "share" / "Trash");
    }
    {
        ScopedEnvironment xdgEnvironment("XDG_DATA_HOME", nullptr);
        ScopedEnvironment homeEnvironment("HOME", nullptr);
        diskmap::TrashOptions emptyOptions;
        CHECK(diskmap::inspectTrashCapability(target, emptyOptions).status
              == diskmap::TrashStatus::InvalidRequest);
    }
}

void testFilesystemRefusalsAndLimits(const fs::path& root) {
    const diskmap::TrashOptions options = optionsFor(root);
    const fs::path sourcePath = root / "refusal-source.txt";
    CHECK(writeFile(sourcePath, "refusal"));
    const diskmap::CleanupTarget target = targetFor(sourcePath);

    // Empty plans are a no-op, while a zero execution bound rejects before
    // opening or mutating any source entry.
    const auto emptyReceipts = diskmap::movePlanToTrash(planFor({}, 1), options);
    CHECK(emptyReceipts.empty());
    checkNoPermanentDelete(sourcePath, options);

    diskmap::CleanupTarget invalid = target;
    invalid.path = "/";
    auto receipts = diskmap::movePlanToTrash(planFor({invalid}), options);
    CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
    if (!receipts.empty()) {
        CHECK(receipts[0].status == diskmap::TrashStatus::InvalidRequest);
    }
    CHECK(fs::is_regular_file(sourcePath));

    invalid = target;
    invalid.path = "relative-source";
    receipts = diskmap::movePlanToTrash(planFor({invalid}), options);
    CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
    if (!receipts.empty()) {
        CHECK(receipts[0].status == diskmap::TrashStatus::InvalidRequest);
    }
    CHECK(fs::is_regular_file(sourcePath));

    // The anchored backend checks the entry type and allocation evidence even
    // when a caller supplies a malformed reviewed target directly.
    const fs::path directoryPath = root / "wrong-type-directory";
    std::error_code error;
    CHECK(fs::create_directory(directoryPath, error));
    CHECK(!error);
    invalid = targetFor(directoryPath);
    invalid.kind = diskmap::FsKind::RegularFile;
    receipts = diskmap::movePlanToTrash(planFor({invalid}), options);
    CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
    if (!receipts.empty()) {
        CHECK(receipts[0].status == diskmap::TrashStatus::RevalidationFailed);
    }
    CHECK(fs::is_directory(directoryPath));
    fs::remove(directoryPath, error);
    CHECK(!error);

    const fs::path allocationPath = root / "wrong-allocation.txt";
    CHECK(writeFile(allocationPath, "allocation"));
    invalid = targetFor(allocationPath);
    invalid.allocated_size += 512;
    receipts = diskmap::movePlanToTrash(planFor({invalid}), options);
    CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
    if (!receipts.empty()) {
        CHECK(receipts[0].status == diskmap::TrashStatus::RevalidationFailed);
    }
    CHECK(fs::is_regular_file(allocationPath));

    // A reviewed source that disappears is a revalidation error, and a
    // missing parent is reported before any rename is attempted.
    const fs::path missingPath = root / "missing-before-move.txt";
    CHECK(writeFile(missingPath, "missing"));
    invalid = targetFor(missingPath);
    fs::remove(missingPath, error);
    CHECK(!error);
    receipts = diskmap::movePlanToTrash(planFor({invalid}), options);
    CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
    if (!receipts.empty()) {
        CHECK(receipts[0].status == diskmap::TrashStatus::RevalidationFailed);
    }

    const fs::path missingParent = root / "removed-parent" / "child.txt";
    CHECK(fs::create_directories(missingParent.parent_path(), error));
    CHECK(!error);
    CHECK(writeFile(missingParent, "parent disappears"));
    invalid = targetFor(missingParent);
    fs::remove_all(missingParent.parent_path(), error);
    CHECK(!error);
    receipts = diskmap::movePlanToTrash(planFor({invalid}), options);
    CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
    if (!receipts.empty()) {
        CHECK(receipts[0].status == diskmap::TrashStatus::RevalidationFailed);
    }

    // Directory setup fails closed both when the data home is a file and when
    // the second Trash directory is blocked.
    const fs::path blockedInfoRoot = root / "blocked-info-home";
    CHECK(fs::create_directories(blockedInfoRoot / "Trash" / "files", error));
    CHECK(!error);
    CHECK(writeFile(blockedInfoRoot / "Trash" / "info", "not a directory"));
    diskmap::TrashOptions blockedInfo = options;
    blockedInfo.data_home = blockedInfoRoot;
    const fs::path blockedSource = root / "blocked-info-source.txt";
    CHECK(writeFile(blockedSource, "blocked"));
    const auto blockedInfoCapability =
        diskmap::inspectTrashCapability(targetFor(blockedSource), blockedInfo);
    CHECK(!blockedInfoCapability.available);
    CHECK(blockedInfoCapability.status == diskmap::TrashStatus::IoError);
    receipts = diskmap::movePlanToTrash(
        planFor({targetFor(blockedSource)}), blockedInfo);
    CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
    if (!receipts.empty()) {
        CHECK(receipts[0].status == diskmap::TrashStatus::IoError);
    }
    CHECK(fs::is_regular_file(blockedSource));

    const fs::path symlinkDataTarget = root / "real-data-home";
    const fs::path symlinkDataHome = root / "symlink-data-home";
    CHECK(fs::create_directory(symlinkDataTarget, error));
    CHECK(!error);
    error.clear();
    fs::create_symlink(symlinkDataTarget, symlinkDataHome, error);
    if (error) {
        std::printf("SKIP symlink data-home refusal: %s\n", error.message().c_str());
    } else {
        diskmap::TrashOptions symlinkOptions = options;
        symlinkOptions.data_home = symlinkDataHome;
        const fs::path symlinkSource = root / "symlink-data-source.txt";
        CHECK(writeFile(symlinkSource, "symlink data home"));
        const auto symlinkCapability = diskmap::inspectTrashCapability(
            targetFor(symlinkSource), symlinkOptions);
        CHECK(!symlinkCapability.available);
        CHECK(symlinkCapability.status == diskmap::TrashStatus::IoError);
        receipts = diskmap::movePlanToTrash(
            planFor({targetFor(symlinkSource)}), symlinkOptions);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (!receipts.empty()) {
            CHECK(receipts[0].status == diskmap::TrashStatus::IoError);
        }
        CHECK(fs::is_regular_file(symlinkSource));
        CHECK(!fs::exists(symlinkDataTarget / "Trash"));
    }

#if defined(__linux__)
    // /dev/shm is normally a separate tmpfs in CI.  If the host does not
    // provide it, the safety assertion is skipped rather than assuming mount
    // topology.
    ScopedTempDirectory alternate("/dev/shm");
    if (alternate.valid() && deviceFor(root) != deviceFor(alternate.path())) {
        diskmap::TrashOptions different = options;
        different.data_home = alternate.path() / "xdg-data";
        const auto capability = diskmap::inspectTrashCapability(target, different);
        CHECK(capability.status == diskmap::TrashStatus::DifferentFilesystem);
        receipts = diskmap::movePlanToTrash(planFor({target}), different);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (!receipts.empty()) {
            CHECK(receipts[0].status == diskmap::TrashStatus::DifferentFilesystem);
        }
        CHECK(fs::is_regular_file(sourcePath));
    } else {
        std::printf("SKIP different-filesystem Trash test: no alternate device\n");
    }

    // A FIFO exercises the unsupported stat kind while the reviewed target is
    // intentionally made inconsistent; it must remain in place.
    const fs::path fifoPath = root / "reviewed-fifo";
    if (::mkfifo(fifoPath.c_str(), 0600) != 0) {
        std::printf("SKIP FIFO Trash validation: cannot create FIFO\n");
    } else {
        invalid = targetFor(fifoPath);
        invalid.kind = diskmap::FsKind::RegularFile;
        receipts = diskmap::movePlanToTrash(planFor({invalid}), options);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (!receipts.empty()) {
            CHECK(receipts[0].status == diskmap::TrashStatus::RevalidationFailed);
        }
        CHECK(fs::exists(fifoPath));
        fs::remove(fifoPath, error);
        CHECK(!error);
    }
#endif
}

void testMetadataAndRestoreFailures(const fs::path& root) {
    const diskmap::TrashOptions options = optionsFor(root);
    CHECK(ensurePrivateTrash(options));
    const fs::path files = options.data_home / "Trash" / "files";

#if defined(__linux__)
    // A raced or malicious metadata FIFO must fail immediately; opening it
    // for restore must never wait indefinitely for a writer.
    {
        const std::string token = "diskmap-metadata-fifo";
        const fs::path fifo = options.data_home / "Trash" / "info"
                              / (token + ".trashinfo");
        if (::mkfifo(fifo.c_str(), 0600) == 0) {
            const auto receipt = diskmap::restoreFromTrash(token, options);
            CHECK(receipt.status == diskmap::TrashStatus::IoError);
            std::error_code error;
            fs::remove(fifo, error);
            CHECK(!error);
        } else {
            std::printf("SKIP restore metadata FIFO test: cannot create FIFO\n");
        }
    }
#endif

    // Lower-case hexadecimal escapes and directory kind metadata both remain
    // valid restore inputs, while the destination is still chosen solely by
    // trusted metadata rather than by the opaque token.
    {
        const std::string token = "diskmap-manual-lower";
        const fs::path payload = files / token;
        const fs::path destination = root / "lowercase-restore.txt";
        CHECK(writeFile(payload, "lowercase payload"));
        const auto target = targetFor(payload);
        std::string encoded = destination.generic_string();
        const std::size_t base = encoded.rfind("/lowercase");
        CHECK(base != std::string::npos);
        if (base != std::string::npos) {
            encoded.replace(base + 1, 1, "%6c");
        }
        CHECK(writeRestoreInfo(options, token, restoreInfo(encoded, target)));
        const auto restored = diskmap::restoreFromTrash(token, options);
        CHECK(restored.status == diskmap::TrashStatus::Restored);
        CHECK(fs::is_regular_file(destination));
        CHECK_EQ(readFile(destination), "lowercase payload");
    }

    {
        const std::string token = "diskmap-manual-directory";
        const fs::path payload = files / token;
        const fs::path destination = root / "directory-restore";
        std::error_code error;
        CHECK(fs::create_directory(payload, error));
        CHECK(!error);
        CHECK(writeFile(payload / "child.txt", "directory payload"));
        const auto target = targetFor(payload);
        CHECK(writeRestoreInfo(options, token,
                               restoreInfo(destination.generic_string(), target)));
        const auto restored = diskmap::restoreFromTrash(token, options);
        CHECK(restored.status == diskmap::TrashStatus::Restored);
        CHECK(fs::is_directory(destination));
        CHECK_EQ(readFile(destination / "child.txt"), "directory payload");
    }

    // Valid metadata without its data entry is a missing token, distinct from
    // malformed metadata and from an unavailable Trash location.
    {
        const fs::path seed = root / "metadata-seed.txt";
        CHECK(writeFile(seed, "seed"));
        const auto target = targetFor(seed);
        std::error_code error;
        fs::remove(seed, error);
        CHECK(!error);
        const std::string token = "diskmap-missing-payload";
        CHECK(writeRestoreInfo(options, token,
                               restoreInfo((root / "missing-payload.txt").generic_string(),
                                           target)));
        CHECK(diskmap::restoreFromTrash(token, options).status
              == diskmap::TrashStatus::MissingToken);
    }

    // The identity recorded in metadata is checked before restore, and a
    // mismatch never consumes the parked entry.
    {
        const std::string token = "diskmap-metadata-identity";
        const fs::path payload = files / token;
        CHECK(writeFile(payload, "identity payload"));
        auto target = targetFor(payload);
        target.identity.file += 1;
        CHECK(writeRestoreInfo(options, token,
                               restoreInfo((root / "identity.txt").generic_string(), target)));
        CHECK(diskmap::restoreFromTrash(token, options).status
              == diskmap::TrashStatus::RevalidationFailed);
        CHECK(fs::is_regular_file(payload));
    }

    auto malformed = [&](const std::string& token, const std::string& content) {
        const fs::path payload = files / token;
        CHECK(writeFile(payload, "malformed payload"));
        CHECK(writeRestoreInfo(options, token, content));
        const auto receipt = diskmap::restoreFromTrash(token, options);
        CHECK(receipt.status == diskmap::TrashStatus::IoError);
        CHECK(fs::is_regular_file(payload));
    };

    const std::string absoluteDestination = (root / "malformed.txt").generic_string();
    malformed("diskmap-bad-header", "not a trash info file\n");
    malformed("diskmap-short-percent",
              "[Trash Info]\nPath=" + absoluteDestination + "%\n");
    malformed("diskmap-bad-hex",
              "[Trash Info]\nPath=" + absoluteDestination + "%G0\n");
    malformed("diskmap-nul-percent",
              "[Trash Info]\nPath=" + absoluteDestination + "%00\n");
    malformed("diskmap-relative-path",
              "[Trash Info]\nPath=relative.txt\n");
    malformed("diskmap-root-path", "[Trash Info]\nPath=/.\n");
    malformed("diskmap-missing-identity",
              "[Trash Info]\nPath=" + absoluteDestination + "\n");
    malformed("diskmap-invalid-number",
              restoreInfoFields(absoluteDestination, "abc", "1", "file"));
    malformed("diskmap-overflow-number",
              restoreInfoFields(absoluteDestination,
                                "184467440737095516160", "1", "file"));
    malformed("diskmap-invalid-kind",
              restoreInfoFields(absoluteDestination, "1", "1", "other"));

    // Metadata bounds and file type are enforced before parsing content.
    {
        const std::string token = "diskmap-oversized-metadata";
        const fs::path payload = files / token;
        CHECK(writeFile(payload, "oversized payload"));
        CHECK(writeRestoreInfo(options, token, std::string(16 * 1024 + 1, 'x')));
        CHECK(diskmap::restoreFromTrash(token, options).status
              == diskmap::TrashStatus::IoError);
        CHECK(fs::is_regular_file(payload));
    }
    // A metadata document exactly at the bound is still readable; the
    // post-boundary EOF probe must not turn it into an overflow failure.
    {
        const std::string token = "diskmap-bounded-metadata";
        const fs::path payload = files / token;
        const fs::path destination = root / "bounded-metadata-restore.txt";
        CHECK(writeFile(payload, "bounded metadata payload"));
        const auto target = targetFor(payload);
        std::string content = restoreInfo(destination.generic_string(), target);
        constexpr std::size_t kMetadataBound = 16 * 1024;
        CHECK(content.size() < kMetadataBound);
        if (content.size() < kMetadataBound) {
            content.append(kMetadataBound - content.size(), 'x');
            CHECK_EQ(content.size(), kMetadataBound);
            CHECK(writeRestoreInfo(options, token, content));
            const auto restored = diskmap::restoreFromTrash(token, options);
            CHECK(restored.status == diskmap::TrashStatus::Restored);
            CHECK(fs::is_regular_file(destination));
            CHECK_EQ(readFile(destination), "bounded metadata payload");
            CHECK(!fs::exists(payload));
        }
    }
    {
        const std::string token = "diskmap-directory-metadata";
        const fs::path payload = files / token;
        const fs::path info = options.data_home / "Trash" / "info"
                              / (token + ".trashinfo");
        CHECK(writeFile(payload, "directory metadata payload"));
        std::error_code error;
        fs::remove(info, error);
        CHECK(!error);
        CHECK(fs::create_directory(info, error));
        CHECK(!error);
        CHECK(diskmap::restoreFromTrash(token, options).status
              == diskmap::TrashStatus::IoError);
        CHECK(fs::is_regular_file(payload));
    }

    // A valid item with a missing destination parent fails closed and leaves
    // both the data and metadata available for a later retry.
    {
        const std::string token = "diskmap-missing-destination-parent";
        const fs::path payload = files / token;
        const fs::path destination = root / "gone-parent" / "child.txt";
        CHECK(writeFile(payload, "parent payload"));
        const auto target = targetFor(payload);
        CHECK(writeRestoreInfo(options, token,
                               restoreInfo(destination.generic_string(), target)));
        CHECK(diskmap::restoreFromTrash(token, options).status
              == diskmap::TrashStatus::IoError);
        CHECK(fs::is_regular_file(payload));
        CHECK(fs::exists(options.data_home / "Trash" / "info"
                         / (token + ".trashinfo")));
    }
}

void testCrashRecoveryMetadata(const fs::path& root) {
    const diskmap::TrashOptions options = optionsFor(root);
    CHECK(ensurePrivateTrash(options));
    const fs::path files = options.data_home / "Trash" / "files";
    const fs::path info = options.data_home / "Trash" / "info";

    // A crash after moving the payload and before finalizing the metadata
    // leaves only <token>.tmp.  That entry is still recoverable, and a
    // successful restore consumes the temporary metadata as well.
    {
        const std::string token = "diskmap-crash-recovery";
        const std::string contents = "crash recovery payload";
        const fs::path payload = files / token;
        const fs::path destination = root / "crash-recovered.txt";
        const fs::path temporaryInfo = info / (token + ".tmp");
        const fs::path finalInfo = info / (token + ".trashinfo");
        CHECK(writeFile(payload, contents));
        const auto target = targetFor(payload);
        CHECK(writeFile(temporaryInfo,
                        restoreInfo(destination.generic_string(), target)));
        CHECK(!fs::exists(finalInfo));

        const auto restored = diskmap::restoreFromTrash(token, options);
        CHECK(restored.status == diskmap::TrashStatus::Restored);
        CHECK(fs::is_regular_file(destination));
        CHECK_EQ(readFile(destination), contents);
        CHECK(!fs::exists(payload));
        CHECK(!fs::exists(temporaryInfo));
        CHECK(!fs::exists(finalInfo));
    }

    // A malformed crash marker must fail closed and leave both recovery
    // artifacts available for diagnosis or a later repair.
    {
        const std::string token = "diskmap-crash-malformed";
        const fs::path payload = files / token;
        const fs::path temporaryInfo = info / (token + ".tmp");
        CHECK(writeFile(payload, "malformed crash payload"));
        CHECK(writeFile(temporaryInfo, "not a trash info file\n"));
        const auto receipt = diskmap::restoreFromTrash(token, options);
        CHECK(receipt.status == diskmap::TrashStatus::IoError);
        CHECK(fs::is_regular_file(payload));
        CHECK(fs::is_regular_file(temporaryInfo));
    }

    // A payload without either metadata name is not restored or consumed.
    {
        const std::string token = "diskmap-crash-missing";
        const fs::path payload = files / token;
        CHECK(writeFile(payload, "missing crash metadata payload"));
        const auto receipt = diskmap::restoreFromTrash(token, options);
        CHECK(receipt.status == diskmap::TrashStatus::MissingToken);
        CHECK(fs::is_regular_file(payload));
    }
}

#if defined(__linux__)
void testConcurrentMutationLock(const fs::path& root) {
    const diskmap::TrashOptions options = optionsFor(root);
    CHECK(ensurePrivateTrash(options));
    const fs::path source = root / "concurrent-lock.txt";
    CHECK(writeFile(source, "serialized mutation"));
    const diskmap::CleanupTarget target = targetFor(source);

    const fs::path trashRoot = options.data_home / "Trash";
    const int lockedRoot = ::open(trashRoot.c_str(), O_RDONLY | O_DIRECTORY);
    CHECK(lockedRoot >= 0);
    if (lockedRoot < 0) {
        return;
    }
    CHECK(::flock(lockedRoot, LOCK_EX | LOCK_NB) == 0);
    const auto blocked = diskmap::movePlanToTrash(planFor({target}), options);
    CHECK_EQ(blocked.size(), static_cast<std::size_t>(1));
    if (!blocked.empty()) {
        CHECK(blocked[0].status == diskmap::TrashStatus::IoError);
    }
    CHECK(fs::is_regular_file(source));
    CHECK(::flock(lockedRoot, LOCK_UN) == 0);
    CHECK(::close(lockedRoot) == 0);

    const auto moved = diskmap::movePlanToTrash(planFor({target}), options);
    CHECK_EQ(moved.size(), static_cast<std::size_t>(1));
    if (moved.empty() || moved[0].status != diskmap::TrashStatus::Moved) {
        return;
    }

    const int restoreLock = ::open(trashRoot.c_str(), O_RDONLY | O_DIRECTORY);
    CHECK(restoreLock >= 0);
    if (restoreLock < 0) {
        return;
    }
    CHECK(::flock(restoreLock, LOCK_EX | LOCK_NB) == 0);
    const auto blockedRestore =
        diskmap::restoreFromTrash(moved[0].restore_token, options);
    CHECK(blockedRestore.status == diskmap::TrashStatus::IoError);
    CHECK(!fs::exists(source));
    CHECK(fs::is_regular_file(moved[0].trashed_path));
    CHECK(::flock(restoreLock, LOCK_UN) == 0);
    CHECK(::close(restoreLock) == 0);

    const auto restored =
        diskmap::restoreFromTrash(moved[0].restore_token, options);
    CHECK(restored.status == diskmap::TrashStatus::Restored);
    CHECK_EQ(readFile(source), "serialized mutation");
}

void testDeterministicRollbackRecovery(const fs::path& root) {
    const diskmap::TrashOptions options = optionsFor(root);
    CHECK(ensurePrivateTrash(options));

    // No-replace creation protects both the reserved metadata name and the
    // payload token from a concurrent claimant, leaving the source untouched.
    {
        const fs::path source = root / "metadata-reservation-collision.txt";
        CHECK(writeFile(source, "metadata reservation payload"));
        MutationScenario scenario;
        scenario.kind = MutationScenarioKind::BlockMetadataReserve;
        std::vector<diskmap::TrashReceipt> receipts;
        {
            ScopedTrashMutationHook hook(scenario);
            receipts = diskmap::movePlanToTrash(
                planFor({targetFor(source)}), options);
        }
        CHECK(scenario.invoked);
        CHECK(!scenario.error);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (!receipts.empty()) {
            CHECK(receipts[0].status == diskmap::TrashStatus::IoError);
        }
        CHECK_EQ(readFile(source), "metadata reservation payload");
        CHECK_EQ(readFile(scenario.auxiliary_path),
                 "metadata reservation collision");
    }
    {
        const fs::path source = root / "payload-token-collision.txt";
        CHECK(writeFile(source, "payload token collision source"));
        MutationScenario scenario;
        scenario.kind = MutationScenarioKind::BlockPayloadMove;
        std::vector<diskmap::TrashReceipt> receipts;
        {
            ScopedTrashMutationHook hook(scenario);
            receipts = diskmap::movePlanToTrash(
                planFor({targetFor(source)}), options);
        }
        CHECK(scenario.invoked);
        CHECK(!scenario.error);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (!receipts.empty()) {
            CHECK(receipts[0].status == diskmap::TrashStatus::DestinationExists);
        }
        CHECK_EQ(readFile(source), "payload token collision source");
        CHECK_EQ(readFile(scenario.auxiliary_path), "payload destination collision");
    }

    // A moved payload that changes before final validation is returned to the
    // reviewed name when that name remains free.
    {
        const fs::path source = root / "rollback-after-move.txt";
        CHECK(writeFile(source, "reviewed"));
        const diskmap::CleanupTarget target = targetFor(source);
        MutationScenario scenario;
        scenario.kind = MutationScenarioKind::MutateMoved;
        std::vector<diskmap::TrashReceipt> receipts;
        {
            ScopedTrashMutationHook hook(scenario);
            receipts = diskmap::movePlanToTrash(planFor({target}), options);
        }
        CHECK(scenario.invoked);
        CHECK(!scenario.error);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (!receipts.empty()) {
            CHECK(receipts[0].status == diskmap::TrashStatus::RevalidationFailed);
            CHECK(receipts[0].restore_token.empty());
        }
        CHECK_EQ(readFile(source), "changed after the reviewed move");
    }

    // If another entry claims the reviewed name, no-replace rollback must not
    // overwrite it. The changed payload stays recoverable under metadata that
    // is rewritten from its actual post-race identity.
    {
        const fs::path source = root / "blocked-rollback-after-move.txt";
        CHECK(writeFile(source, "reviewed"));
        const diskmap::CleanupTarget target = targetFor(source);
        MutationScenario scenario;
        scenario.kind = MutationScenarioKind::MutateMovedAndBlockRollback;
        std::vector<diskmap::TrashReceipt> receipts;
        {
            ScopedTrashMutationHook hook(scenario);
            receipts = diskmap::movePlanToTrash(planFor({target}), options);
        }
        CHECK(scenario.invoked);
        CHECK(!scenario.error);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (receipts.empty()) {
            return;
        }
        const diskmap::TrashReceipt& failed = receipts[0];
        CHECK(failed.status == diskmap::TrashStatus::RevalidationFailed);
        CHECK(!failed.restore_token.empty());
        CHECK(failed.message.find("rollback was blocked") != std::string::npos);
        CHECK_EQ(readFile(source), "replacement at the reviewed name");
        CHECK_EQ(readFile(failed.trashed_path), "changed after the reviewed move");

        std::error_code error;
        CHECK(fs::remove(source, error));
        CHECK(!error);
        const auto restored =
            diskmap::restoreFromTrash(failed.restore_token, options);
        CHECK(restored.status == diskmap::TrashStatus::Restored);
        CHECK_EQ(readFile(source), "changed after the reviewed move");
    }

    // A source parent renamed after the move must never receive a rollback via
    // its now-detached descriptor. Keep the payload and rewritten metadata in
    // Trash so the lexical destination can be retried safely.
    {
        const fs::path parent = root / "move-parent-race";
        const fs::path detached = root / "move-parent-race-detached";
        CHECK(fs::create_directory(parent));
        const fs::path source = parent / "payload.txt";
        CHECK(writeFile(source, "move parent race payload"));
        const diskmap::CleanupTarget target = targetFor(source);
        MutationScenario scenario;
        scenario.kind = MutationScenarioKind::MutateMovedAndDetachParent;
        scenario.auxiliary_path = detached;
        std::vector<diskmap::TrashReceipt> receipts;
        {
            ScopedTrashMutationHook hook(scenario);
            receipts = diskmap::movePlanToTrash(planFor({target}), options);
        }
        CHECK(scenario.invoked);
        CHECK(!scenario.error);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (receipts.empty()) {
            return;
        }
        CHECK(receipts[0].status == diskmap::TrashStatus::RevalidationFailed);
        CHECK(!receipts[0].restore_token.empty());
        CHECK(receipts[0].message.find("source directory changed")
              != std::string::npos);
        CHECK(!fs::exists(detached / "payload.txt"));
        CHECK(!fs::exists(source));
        CHECK_EQ(readFile(receipts[0].trashed_path),
                 "changed before detached-parent rollback");
        std::error_code error;
        CHECK(fs::remove(detached, error));
        CHECK(!error);
        const auto restored =
            diskmap::restoreFromTrash(receipts[0].restore_token, options);
        CHECK(restored.status == diskmap::TrashStatus::Restored);
        CHECK_EQ(readFile(source), "changed before detached-parent rollback");
    }

    // If the parent changes after the initial rollback anchor check, the
    // post-rename identity check moves the payload back out of the detached
    // directory and keeps it recoverable under the returned token.
    {
        const fs::path parent = root / "move-rollback-window";
        const fs::path detached = root / "move-rollback-window-detached";
        CHECK(fs::create_directory(parent));
        const fs::path source = parent / "payload.txt";
        CHECK(writeFile(source, "rollback window payload"));
        const diskmap::CleanupTarget target = targetFor(source);
        MutationScenario scenario;
        scenario.kind = MutationScenarioKind::DetachParentDuringMoveRollback;
        scenario.auxiliary_path = detached;
        std::vector<diskmap::TrashReceipt> receipts;
        {
            ScopedTrashMutationHook hook(scenario);
            receipts = diskmap::movePlanToTrash(planFor({target}), options);
        }
        CHECK(scenario.invoked);
        CHECK(!scenario.error);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (receipts.empty()) {
            return;
        }
        CHECK(receipts[0].status == diskmap::TrashStatus::RevalidationFailed);
        CHECK(!receipts[0].restore_token.empty());
        CHECK(receipts[0].message.find("returned to Trash") != std::string::npos);
        CHECK(!fs::exists(detached / "payload.txt"));
        CHECK(!fs::exists(source));
        CHECK_EQ(readFile(receipts[0].trashed_path),
                 "changed before rollback identity recheck");
        std::error_code error;
        CHECK(fs::remove(detached, error));
        CHECK(!error);
        const auto restored =
            diskmap::restoreFromTrash(receipts[0].restore_token, options);
        CHECK(restored.status == diskmap::TrashStatus::Restored);
        CHECK_EQ(readFile(source), "changed before rollback identity recheck");
    }

    // A concurrent final metadata name must not overwrite that entry. The
    // payload is rolled back and the collision remains available for review.
    {
        const fs::path source = root / "metadata-finalize-collision.txt";
        CHECK(writeFile(source, "metadata collision payload"));
        const diskmap::CleanupTarget target = targetFor(source);
        MutationScenario scenario;
        scenario.kind = MutationScenarioKind::BlockMetadataFinalize;
        std::vector<diskmap::TrashReceipt> receipts;
        {
            ScopedTrashMutationHook hook(scenario);
            receipts = diskmap::movePlanToTrash(planFor({target}), options);
        }
        CHECK(scenario.invoked);
        CHECK(!scenario.error);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (!receipts.empty()) {
            CHECK(receipts[0].status == diskmap::TrashStatus::IoError);
            CHECK(receipts[0].restore_token.empty());
        }
        CHECK_EQ(readFile(source), "metadata collision payload");
        CHECK_EQ(readFile(scenario.auxiliary_path),
                 "reserved by a concurrent writer");
    }

    // If an external actor removes the moved payload and also claims the old
    // name, rollback and recovery-metadata reconstruction both fail closed.
    // The replacement is preserved and no unusable restore token is promised.
    {
        const fs::path source = root / "removed-payload-rollback.txt";
        CHECK(writeFile(source, "payload removed during validation"));
        const diskmap::CleanupTarget target = targetFor(source);
        MutationScenario scenario;
        scenario.kind = MutationScenarioKind::DeleteMovedAndBlockRollback;
        std::vector<diskmap::TrashReceipt> receipts;
        {
            ScopedTrashMutationHook hook(scenario);
            receipts = diskmap::movePlanToTrash(planFor({target}), options);
        }
        CHECK(scenario.invoked);
        CHECK(!scenario.error);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (!receipts.empty()) {
            CHECK(receipts[0].status == diskmap::TrashStatus::RevalidationFailed);
            CHECK(receipts[0].restore_token.empty());
            CHECK(receipts[0].message.find("recovery metadata failed")
                  != std::string::npos);
            CHECK(!fs::exists(receipts[0].trashed_path));
        }
        CHECK_EQ(readFile(source), "replacement after payload removal");
    }

    // Metadata publication cannot produce a public recovery token when the
    // info directory is detached after finalize and rollback of the metadata
    // name is blocked inside that detached directory.
    {
        const fs::path source = root / "detached-info-finalize.txt";
        CHECK(writeFile(source, "detached info payload"));
        MutationScenario scenario;
        scenario.kind = MutationScenarioKind::DetachTrashInfoAfterFinalize;
        scenario.auxiliary_path = options.data_home / "Trash" / "info-detached";
        std::vector<diskmap::TrashReceipt> receipts;
        {
            ScopedTrashMutationHook hook(scenario);
            receipts = diskmap::movePlanToTrash(
                planFor({targetFor(source)}), options);
        }
        CHECK(scenario.invoked);
        CHECK(!scenario.error);
        CHECK_EQ(receipts.size(), static_cast<std::size_t>(1));
        if (receipts.empty()) {
            return;
        }
        CHECK(receipts[0].status == diskmap::TrashStatus::IoError);
        CHECK(receipts[0].restore_token.empty());
        CHECK(receipts[0].trashed_path.empty());
        CHECK(!fs::exists(source));
        CHECK_EQ(readFile(scenario.observed_path), "detached info payload");

        const fs::path info = options.data_home / "Trash" / "info";
        const std::string token = scenario.observed_path.filename().string();
        std::error_code error;
        CHECK(fs::remove(info, error));
        CHECK(!error);
        CHECK(fs::remove(scenario.auxiliary_path / (token + ".tmp"), error));
        CHECK(!error);
        fs::rename(scenario.auxiliary_path, info, error);
        CHECK(!error);
        const auto restored = diskmap::restoreFromTrash(token, options);
        CHECK(restored.status == diskmap::TrashStatus::Restored);
        CHECK_EQ(readFile(source), "detached info payload");
    }

    // Replacing the lexical destination directory after restore is detected.
    // The pinned original parent allows the payload to return to Trash, after
    // which an ordinary retry can restore it into the new directory.
    {
        const fs::path parent = root / "restore-parent-race";
        const fs::path detached = root / "restore-parent-race-detached";
        CHECK(fs::create_directory(parent));
        const fs::path source = parent / "payload.txt";
        CHECK(writeFile(source, "restore race payload"));
        const auto moved =
            diskmap::movePlanToTrash(planFor({targetFor(source)}), options);
        CHECK_EQ(moved.size(), static_cast<std::size_t>(1));
        if (moved.empty() || moved[0].status != diskmap::TrashStatus::Moved) {
            return;
        }
        MutationScenario scenario;
        scenario.kind = MutationScenarioKind::DetachRestoreParent;
        scenario.auxiliary_path = detached;
        diskmap::TrashReceipt failed;
        {
            ScopedTrashMutationHook hook(scenario);
            failed = diskmap::restoreFromTrash(moved[0].restore_token, options);
        }
        CHECK(scenario.invoked);
        CHECK(!scenario.error);
        CHECK(failed.status == diskmap::TrashStatus::RevalidationFailed);
        CHECK_EQ(failed.restore_token, moved[0].restore_token);
        CHECK_EQ(failed.trashed_path, moved[0].trashed_path);
        CHECK(!fs::exists(source));
        CHECK(fs::is_regular_file(moved[0].trashed_path));
        std::error_code error;
        CHECK(fs::remove(detached, error));
        CHECK(!error);
        const auto restored =
            diskmap::restoreFromTrash(moved[0].restore_token, options);
        CHECK(restored.status == diskmap::TrashStatus::Restored);
        CHECK_EQ(readFile(source), "restore race payload");
    }

    // A destination appearing after the absence check still wins: restore's
    // no-replace rename refuses it and leaves the Trash payload retryable.
    {
        const fs::path source = root / "restore-no-replace.txt";
        CHECK(writeFile(source, "restore no-replace payload"));
        const auto moved =
            diskmap::movePlanToTrash(planFor({targetFor(source)}), options);
        CHECK_EQ(moved.size(), static_cast<std::size_t>(1));
        if (moved.empty() || moved[0].status != diskmap::TrashStatus::Moved) {
            return;
        }
        MutationScenario scenario;
        scenario.kind = MutationScenarioKind::BlockPayloadRestore;
        diskmap::TrashReceipt failed;
        {
            ScopedTrashMutationHook hook(scenario);
            failed = diskmap::restoreFromTrash(moved[0].restore_token, options);
        }
        CHECK(scenario.invoked);
        CHECK(!scenario.error);
        CHECK(failed.status == diskmap::TrashStatus::DestinationExists);
        CHECK_EQ(readFile(source), "restore destination collision");
        CHECK(fs::is_regular_file(moved[0].trashed_path));
        std::error_code error;
        CHECK(fs::remove(source, error));
        CHECK(!error);
        const auto restored =
            diskmap::restoreFromTrash(moved[0].restore_token, options);
        CHECK(restored.status == diskmap::TrashStatus::Restored);
        CHECK_EQ(readFile(source), "restore no-replace payload");
    }

    // The same race with a claimed Trash token exercises the recovery outcome:
    // neither the restored payload nor its metadata is deleted when rollback
    // cannot use its no-replace destination.
    {
        const fs::path parent = root / "restore-rollback-blocked";
        const fs::path detached = root / "restore-rollback-blocked-detached";
        CHECK(fs::create_directory(parent));
        const fs::path source = parent / "payload.txt";
        CHECK(writeFile(source, "preserved restored payload"));
        const auto moved =
            diskmap::movePlanToTrash(planFor({targetFor(source)}), options);
        CHECK_EQ(moved.size(), static_cast<std::size_t>(1));
        if (moved.empty() || moved[0].status != diskmap::TrashStatus::Moved) {
            return;
        }
        MutationScenario scenario;
        scenario.kind = MutationScenarioKind::DetachRestoreParentAndBlockRollback;
        scenario.auxiliary_path = detached;
        diskmap::TrashReceipt failed;
        {
            ScopedTrashMutationHook hook(scenario);
            failed = diskmap::restoreFromTrash(moved[0].restore_token, options);
        }
        CHECK(scenario.invoked);
        CHECK(!scenario.error);
        CHECK(failed.status == diskmap::TrashStatus::IoError);
        CHECK(failed.restore_token.empty());
        CHECK(failed.message.find("rollback failed") != std::string::npos);
        CHECK_EQ(readFile(detached / "payload.txt"), "preserved restored payload");
        CHECK_EQ(readFile(moved[0].trashed_path), "rollback destination blocker");
        CHECK(fs::exists(options.data_home / "Trash" / "info"
                         / (moved[0].restore_token + ".trashinfo")));
    }

    // If the public Trash/files directory is replaced after restore, rollback
    // can preserve the payload only through its already-open descriptor. The
    // lexical path and token are withheld until the original directory is put
    // back, after which the caller's original token is retryable.
    {
        const fs::path source = root / "restore-trash-anchor-race.txt";
        CHECK(writeFile(source, "restore Trash anchor payload"));
        const auto moved =
            diskmap::movePlanToTrash(planFor({targetFor(source)}), options);
        CHECK_EQ(moved.size(), static_cast<std::size_t>(1));
        if (moved.empty() || moved[0].status != diskmap::TrashStatus::Moved) {
            return;
        }
        const fs::path files = options.data_home / "Trash" / "files";
        MutationScenario scenario;
        scenario.kind = MutationScenarioKind::DetachTrashFilesDuringRestore;
        scenario.auxiliary_path = options.data_home / "Trash" / "files-detached";
        diskmap::TrashReceipt failed;
        {
            ScopedTrashMutationHook hook(scenario);
            failed = diskmap::restoreFromTrash(moved[0].restore_token, options);
        }
        CHECK(scenario.invoked);
        CHECK(!scenario.error);
        CHECK(failed.status == diskmap::TrashStatus::IoError);
        CHECK(failed.restore_token.empty());
        CHECK(failed.trashed_path.empty());
        CHECK(!fs::exists(source));
        CHECK_EQ(readFile(scenario.auxiliary_path / moved[0].restore_token),
                 "restore Trash anchor payload");

        std::error_code error;
        CHECK(fs::remove(files, error));
        CHECK(!error);
        fs::rename(scenario.auxiliary_path, files, error);
        CHECK(!error);
        const auto restored =
            diskmap::restoreFromTrash(moved[0].restore_token, options);
        CHECK(restored.status == diskmap::TrashStatus::Restored);
        CHECK_EQ(readFile(source), "restore Trash anchor payload");
    }
}
#endif

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

    testStatusTokensAndEnvironment(temp.path());
    testFilesystemRefusalsAndLimits(temp.path());
    testMetadataAndRestoreFailures(temp.path());
    testCrashRecoveryMetadata(temp.path());
    testConcurrentMutationLock(temp.path());
    testDeterministicRollbackRecovery(temp.path());
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
