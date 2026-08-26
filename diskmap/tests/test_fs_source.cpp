// Tests for diskmap::RealFsSource (src/core/fs_source.hpp / fs_source.cpp).
//
// FakeFsSource (tests/fake_fs.hpp) is what scanner tests use to avoid
// touching a real filesystem, but RealFsSource itself only gets exercised
// here: against a real directory tree we create under a temp path, and
// against a path that doesn't exist so the error branch is covered too.

#include "assert.hpp"
#include "../src/core/fs_source.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using diskmap::DirEntry;
using diskmap::FsSource;
using diskmap::RealFsSource;

namespace fs = std::filesystem;

namespace {

const DirEntry* findEntry(const std::vector<DirEntry>& entries, const std::string& name) {
    for (const DirEntry& entry : entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

void writeFile(const fs::path& path, const std::string& contents) {
    std::ofstream out(path.string(), std::ios::binary);
    out << contents;
}

} // namespace

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path base =
        fs::temp_directory_path() / ("diskmap_fs_source_test_" + std::to_string(stamp));

    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base / "subdir", ec);
    CHECK(!ec);

    writeFile(base / "file.txt", "hello"); // 5 bytes
    writeFile(base / "subdir" / "nested.txt", "nested-file-contents");

    bool symlinkSupported = true;
    fs::create_symlink(base / "file.txt", base / "link_to_file", ec);
    if (ec) {
        symlinkSupported = false; // some sandboxes disallow symlinks; degrade gracefully
    }

    RealFsSource source;

    // --- success: listing a real directory picks up files, subdirs, symlinks ---
    std::string error;
    const std::vector<DirEntry> entries = source.list(base.string(), error);
    CHECK(error.empty());

    const DirEntry* fileEntry = findEntry(entries, "file.txt");
    CHECK(fileEntry != nullptr);
    if (fileEntry) {
        CHECK(!fileEntry->is_dir);
        CHECK(!fileEntry->is_symlink);
        CHECK_EQ(fileEntry->size, static_cast<std::uint64_t>(5));
    }

    const DirEntry* subEntry = findEntry(entries, "subdir");
    CHECK(subEntry != nullptr);
    if (subEntry) {
        CHECK(subEntry->is_dir);
        CHECK(!subEntry->is_symlink);
    }

    if (symlinkSupported) {
        const DirEntry* linkEntry = findEntry(entries, "link_to_file");
        CHECK(linkEntry != nullptr);
        if (linkEntry) {
            CHECK(linkEntry->is_symlink);
        }
    }

    // --- listing a nested subdirectory directly also works ---
    std::string subError;
    const std::vector<DirEntry> subEntries = source.list((base / "subdir").string(), subError);
    CHECK(subError.empty());
    const DirEntry* nestedEntry = findEntry(subEntries, "nested.txt");
    CHECK(nestedEntry != nullptr);
    if (nestedEntry) {
        CHECK_EQ(nestedEntry->size, static_cast<std::uint64_t>(20)); // "nested-file-contents"
    }

    // --- error branch: a path that does not exist ---
    std::string missingError;
    const std::vector<DirEntry> missingEntries =
        source.list((base / "does_not_exist_at_all").string(), missingError);
    CHECK(missingEntries.empty());
    CHECK(!missingError.empty());

    // --- through the FsSource base pointer, to exercise the virtual dtor chain ---
    {
        FsSource* polymorphic = new RealFsSource();
        std::string polyError;
        const std::vector<DirEntry> polyEntries = polymorphic->list(base.string(), polyError);
        CHECK(!polyEntries.empty());
        CHECK(polyError.empty());
        delete polymorphic;
    }

    fs::remove_all(base, ec);

    return testSummary();
}
