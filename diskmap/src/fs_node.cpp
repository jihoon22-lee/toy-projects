#include "diskmap/fs_node.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace diskmap {

namespace {

using IdentityKey = std::pair<std::uint64_t, std::uint64_t>;

struct IdentityStorage {
    std::uint64_t allocated = 0;
    bool allocated_seen = false;
    bool allocated_known = true;
    std::uint64_t hard_links = 0;
    bool hard_links_seen = false;
    bool hard_links_known = true;
    std::uint64_t owned_references = 0;
};

struct StorageFacts {
    std::map<IdentityKey, IdentityStorage> identities;
    std::uint64_t unidentified_allocated = 0;
    std::uint64_t unidentified_reclaimable = 0;
    bool unidentified_allocated_known = true;
    bool unidentified_reclaimable_known = true;
    bool complete = true;
};

bool addSize(std::uint64_t& total, std::uint64_t value) {
    if (value > std::numeric_limits<std::uint64_t>::max() - total) {
        total = std::numeric_limits<std::uint64_t>::max();
        return false;
    }
    total += value;
    return true;
}

const FsMetadata& effectiveMetadata(const FsNode& node) {
    return node.followed && node.has_target_metadata ? node.target_metadata : node.metadata;
}

void observeIdentity(IdentityStorage& storage, const FsNode& node, const FsMetadata& metadata) {
    if (!metadata.allocated_size_known) {
        storage.allocated_known = false;
    } else if (!storage.allocated_seen) {
        storage.allocated = metadata.allocated_size;
        storage.allocated_seen = true;
    } else if (storage.allocated != metadata.allocated_size) {
        storage.allocated = std::max(storage.allocated, metadata.allocated_size);
        storage.allocated_known = false;
    }

    if (!metadata.hard_link_count_known || metadata.hard_link_count == 0) {
        storage.hard_links_known = false;
    } else if (!storage.hard_links_seen) {
        storage.hard_links = metadata.hard_link_count;
        storage.hard_links_seen = true;
    } else if (storage.hard_links != metadata.hard_link_count) {
        storage.hard_links = std::max(storage.hard_links, metadata.hard_link_count);
        storage.hard_links_known = false;
    }

    if (!node.followed) {
        if (storage.owned_references == std::numeric_limits<std::uint64_t>::max()) {
            storage.hard_links_known = false;
        } else {
            ++storage.owned_references;
        }
    }
}

void addUnidentified(StorageFacts& facts, const FsNode& node, const FsMetadata& metadata) {
    const bool oneOwnedReference = !node.followed && metadata.hard_link_count_known
                                   && metadata.hard_link_count == 1;
    if (!metadata.allocated_size_known || !oneOwnedReference) {
        facts.unidentified_allocated_known = false;
    } else if (!addSize(facts.unidentified_allocated, metadata.allocated_size)) {
        facts.unidentified_allocated_known = false;
    }

    if (!metadata.allocated_size_known || !metadata.hard_link_count_known
        || metadata.hard_link_count == 0 || (metadata.hard_link_count > 1 && !node.followed)) {
        facts.unidentified_reclaimable_known = false;
        return;
    }
    if (oneOwnedReference
        && !addSize(facts.unidentified_reclaimable, metadata.allocated_size)) {
        facts.unidentified_reclaimable_known = false;
    }
}

void mergeIdentity(IdentityStorage& destination, const IdentityStorage& source) {
    if (!source.allocated_known) {
        destination.allocated_known = false;
    }
    if (source.allocated_seen) {
        if (!destination.allocated_seen) {
            destination.allocated = source.allocated;
            destination.allocated_seen = true;
        } else if (destination.allocated != source.allocated) {
            destination.allocated = std::max(destination.allocated, source.allocated);
            destination.allocated_known = false;
        }
    }
    if (!source.hard_links_known) {
        destination.hard_links_known = false;
    }
    if (source.hard_links_seen) {
        if (!destination.hard_links_seen) {
            destination.hard_links = source.hard_links;
            destination.hard_links_seen = true;
        } else if (destination.hard_links != source.hard_links) {
            destination.hard_links = std::max(destination.hard_links, source.hard_links);
            destination.hard_links_known = false;
        }
    }
    if (!addSize(destination.owned_references, source.owned_references)) {
        destination.hard_links_known = false;
    }
}

void mergeFacts(StorageFacts& destination, StorageFacts source) {
    destination.complete = destination.complete && source.complete;
    if (!addSize(destination.unidentified_allocated, source.unidentified_allocated)) {
        destination.unidentified_allocated_known = false;
    }
    if (!addSize(destination.unidentified_reclaimable, source.unidentified_reclaimable)) {
        destination.unidentified_reclaimable_known = false;
    }
    destination.unidentified_allocated_known =
        destination.unidentified_allocated_known && source.unidentified_allocated_known;
    destination.unidentified_reclaimable_known =
        destination.unidentified_reclaimable_known && source.unidentified_reclaimable_known;
    for (const auto& item : source.identities) {
        mergeIdentity(destination.identities[item.first], item.second);
    }
}

void assignStorage(FsNode& node, const StorageFacts& facts) {
    node.allocated_size = facts.unidentified_allocated;
    node.reclaimable_size = facts.unidentified_reclaimable;
    node.allocated_size_known = facts.complete && facts.unidentified_allocated_known;
    node.reclaimable_size_known = facts.complete && facts.unidentified_reclaimable_known;

    for (const auto& item : facts.identities) {
        const IdentityStorage& storage = item.second;
        if (!storage.allocated_seen || !storage.allocated_known
            || !addSize(node.allocated_size, storage.allocated)) {
            node.allocated_size_known = false;
        }
        if (!storage.allocated_seen || !storage.allocated_known || !storage.hard_links_seen
            || !storage.hard_links_known || storage.owned_references > storage.hard_links) {
            node.reclaimable_size_known = false;
            continue;
        }
        if (storage.owned_references == storage.hard_links
            && !addSize(node.reclaimable_size, storage.allocated)) {
            node.reclaimable_size_known = false;
        }
    }
}

StorageFacts aggregateStorageAt(FsNode& node, int depth) {
    StorageFacts facts;
    facts.complete = node.complete;
    if (!node.is_dir) {
        const FsMetadata& metadata = effectiveMetadata(node);
        facts.complete = facts.complete && metadata.complete;
        if (metadata.identity.valid) {
            observeIdentity(facts.identities[{metadata.identity.device, metadata.identity.file}],
                            node, metadata);
        } else {
            addUnidentified(facts, node, metadata);
        }
        assignStorage(node, facts);
        return facts;
    }

    if (depth >= kMaxTreeDepth) {
        facts.complete = false;
        assignStorage(node, facts);
        return facts;
    }
    for (FsNode& child : node.children) {
        mergeFacts(facts, aggregateStorageAt(child, depth + 1));
    }
    assignStorage(node, facts);
    return facts;
}

std::uint64_t aggregateSizesAt(FsNode& node, int depth) {
    if (!node.is_dir || depth >= kMaxTreeDepth) {
        return node.size;
    }
    std::uint64_t total = 0;
    for (FsNode& child : node.children) {
        addSize(total, aggregateSizesAt(child, depth + 1));
    }
    node.size = total;
    return total;
}

bool byDescendingSizeThenName(const FsNode& a, const FsNode& b) {
    if (a.size != b.size) {
        return a.size > b.size;
    }
    return a.name < b.name;
}

void sortBySizeDescAt(FsNode& node, int depth) {
    if (depth >= kMaxTreeDepth) {
        return;
    }
    std::stable_sort(node.children.begin(), node.children.end(), byDescendingSizeThenName);
    for (FsNode& child : node.children) {
        sortBySizeDescAt(child, depth + 1);
    }
}

std::size_t countNodesAt(const FsNode& node, int depth) {
    std::size_t count = 1;
    if (depth >= kMaxTreeDepth) {
        return count;
    }
    for (const FsNode& child : node.children) {
        count += countNodesAt(child, depth + 1);
    }
    return count;
}

void collectFilesAt(const FsNode& node, int depth, std::vector<const FsNode*>& out) {
    if (!node.is_dir) {
        out.push_back(&node);
        return;
    }
    if (depth >= kMaxTreeDepth) {
        return;
    }
    for (const FsNode& child : node.children) {
        collectFilesAt(child, depth + 1, out);
    }
}

bool byDescendingFileSize(const FsNode* a, const FsNode* b) {
    return a->size > b->size;
}

} // namespace

std::uint64_t aggregateSizes(FsNode& node) {
    return aggregateSizesAt(node, 0);
}

void aggregateStorage(FsNode& node) {
    aggregateStorageAt(node, 0);
}

void sortBySizeDesc(FsNode& node) {
    sortBySizeDescAt(node, 0);
}

const FsNode* findChild(const FsNode& node, const std::string& name) {
    for (const FsNode& child : node.children) {
        if (child.name == name) {
            return &child;
        }
    }
    return nullptr;
}

std::size_t countNodes(const FsNode& node) {
    return countNodesAt(node, 0);
}

std::vector<const FsNode*> topFiles(const FsNode& node, std::size_t n) {
    std::vector<const FsNode*> files;
    collectFilesAt(node, 0, files);
    std::stable_sort(files.begin(), files.end(), byDescendingFileSize);
    if (files.size() > n) {
        files.resize(n);
    }
    return files;
}

} // namespace diskmap
