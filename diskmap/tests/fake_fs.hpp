#pragma once

// Shared test fixtures: a scriptable FsSource plus small FsNode tree builders.
// Only *.cpp files under tests/ are compiled as test binaries, so this extra
// header is safe to share across test files without becoming its own test.

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../src/core/fs_node.hpp"
#include "../src/core/fs_source.hpp"

namespace diskmap_test {

// An FsSource backed by canned directory listings, keyed by path. Any path
// registered via addError() (or any path with no listing at all) reports an
// error through list()'s out-parameter instead of returning entries.
class FakeFsSource : public diskmap::FsSource {
public:
    void addListing(const std::string& path, std::vector<diskmap::DirEntry> entries) {
        listings_[path] = std::move(entries);
    }

    void addError(const std::string& path, std::string message = "simulated listing error") {
        errors_[path] = std::move(message);
    }

    std::vector<diskmap::DirEntry> list(const std::string& path, std::string& error) const override {
        error.clear();

        const auto errIt = errors_.find(path);
        if (errIt != errors_.end()) {
            error = errIt->second;
            return {};
        }

        const auto it = listings_.find(path);
        if (it == listings_.end()) {
            error = "fake fs: no listing registered for '" + path + "'";
            return {};
        }
        return it->second;
    }

private:
    std::map<std::string, std::vector<diskmap::DirEntry>> listings_;
    std::map<std::string, std::string> errors_;
};

inline diskmap::DirEntry makeFileEntry(std::string name, std::uint64_t size, bool symlink = false) {
    diskmap::DirEntry entry;
    entry.name = std::move(name);
    entry.is_dir = false;
    entry.is_symlink = symlink;
    entry.size = size;
    return entry;
}

inline diskmap::DirEntry makeDirEntry(std::string name, bool symlink = false) {
    diskmap::DirEntry entry;
    entry.name = std::move(name);
    entry.is_dir = true;
    entry.is_symlink = symlink;
    entry.size = 0;
    return entry;
}

// Builds a plain (non-directory) FsNode leaf, e.g. for treemap/fs_node tests.
inline diskmap::FsNode makeFileNode(std::string name, std::uint64_t size) {
    diskmap::FsNode node;
    node.name = std::move(name);
    node.is_dir = false;
    node.size = size;
    return node;
}

// Builds a directory FsNode from a set of already-built children. Size is
// left at 0 here; call aggregateSizes() on the root if you need it filled in.
inline diskmap::FsNode makeDirNode(std::string name, std::vector<diskmap::FsNode> children = {}) {
    diskmap::FsNode node;
    node.name = std::move(name);
    node.is_dir = true;
    node.children = std::move(children);
    return node;
}

} // namespace diskmap_test
