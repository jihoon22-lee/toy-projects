#include "diskmap/format.hpp"
#include "diskmap/fs_node.hpp"
#include "diskmap/fs_source.hpp"
#include "diskmap/scanner.hpp"
#include "diskmap/snapshot.hpp"
#include "storage_cli.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

struct CliOptions {
    std::string path;
    int depth = -1;
    int max_depth = -1;
    std::size_t top = 10;
    std::uint64_t min_size = 0;
    bool json = false;
    bool follow_symlinks = false;
    bool one_file_system = false;
    std::vector<std::string> exclude_patterns;
    std::string save_snapshot;
    std::string load_snapshot;
    std::string compare_snapshot;
    bool duplicates = false;
    bool help = false;
    bool valid = true;
    std::string error;
};

void printUsage(std::ostream& out) {
    out << "Usage: diskmap <path> [options]\n"
        << "   or: diskmap --load-snapshot FILE [options]\n"
        << "  --max-depth N       limit scan traversal depth\n"
        << "  --follow-symlinks   follow symlinked directories\n"
        << "  --min-size BYTES    skip files smaller than BYTES\n"
        << "  --one-file-system   do not cross filesystem boundaries\n"
        << "  --exclude GLOB      skip matching entries (repeatable)\n"
        << "  --depth N           limit printed tree depth\n"
        << "  --top N             show the N largest files (default 10)\n"
        << "  --json              emit the tree as JSON instead of text\n"
        << "  --save-snapshot FILE save the scan as a bounded snapshot\n"
        << "  --load-snapshot FILE inspect a saved snapshot without scanning\n"
        << "  --compare-snapshot FILE compare the scan with a saved snapshot\n"
        << "  --duplicates        inspect duplicate evidence (review-only)\n"
        << "  --help              show this message\n";
}

bool parseNonNegativeInt(const std::string& value, int& out) {
    if (value.empty()) {
        return false;
    }
    const char* first = value.data();
    const char* last = first + value.size();
    const std::from_chars_result parsed = std::from_chars(first, last, out);
    return parsed.ec == std::errc() && parsed.ptr == last && out >= 0;
}

bool parseNonNegativeUint64(const std::string& value, std::uint64_t& out) {
    if (value.empty()) {
        return false;
    }
    const char* first = value.data();
    const char* last = first + value.size();
    const std::from_chars_result parsed = std::from_chars(first, last, out);
    return parsed.ec == std::errc() && parsed.ptr == last;
}

void markInvalid(CliOptions& options, const std::string& message) {
    options.valid = false;
    if (options.error.empty()) {
        options.error = message;
    }
}

// Consumes the option's value (if present) and reports whether it parsed.
bool takeIntOption(const std::vector<std::string>& args, std::size_t& index, int& out) {
    if (index + 1 >= args.size()) {
        return false;
    }
    ++index;
    return parseNonNegativeInt(args[index], out);
}

bool takeUint64Option(const std::vector<std::string>& args,
                      std::size_t& index,
                      std::uint64_t& out) {
    if (index + 1 >= args.size()) {
        return false;
    }
    ++index;
    return parseNonNegativeUint64(args[index], out);
}

bool takeStringOption(const std::vector<std::string>& args,
                      std::size_t& index,
                      std::string& out) {
    if (index + 1 >= args.size()) {
        return false;
    }
    ++index;
    out = args[index];
    return !out.empty() && out.rfind("--", 0) != 0;
}

// Handles a valueless flag. Returns false when arg is not one.
bool applyFlag(const std::string& arg, CliOptions& options) {
    if (arg == "--help") {
        options.help = true;
        return true;
    }
    if (arg == "--json") {
        options.json = true;
        return true;
    }
    if (arg == "--follow-symlinks") {
        options.follow_symlinks = true;
        return true;
    }
    if (arg == "--one-file-system") {
        options.one_file_system = true;
        return true;
    }
    if (arg == "--duplicates") {
        options.duplicates = true;
        return true;
    }
    return false;
}

// Handles an option that consumes the following argument as its value.
// Returns false when arg is not one, advancing index only when it is.
bool applyValueOption(const std::vector<std::string>& args,
                      std::size_t& index,
                      const std::string& arg,
                      CliOptions& options) {
    if (arg == "--depth" || arg == "--max-depth") {
        int parsedValue = 0;
        if (!takeIntOption(args, index, parsedValue)) {
            markInvalid(options, arg + " expects a non-negative integer");
            return true;
        }
        if (arg == "--depth") {
            options.depth = parsedValue;
        } else {
            options.max_depth = parsedValue;
        }
        return true;
    }
    if (arg == "--top") {
        std::uint64_t parsedValue = 0;
        if (!takeUint64Option(args, index, parsedValue)
            || parsedValue > std::numeric_limits<std::size_t>::max()) {
            markInvalid(options, "--top expects a non-negative integer");
            return true;
        }
        options.top = static_cast<std::size_t>(parsedValue);
        return true;
    }
    if (arg != "--min-size") {
        return false;
    }
    std::uint64_t parsedValue = 0;
    if (!takeUint64Option(args, index, parsedValue)) {
        markInvalid(options, "--min-size expects a non-negative integer");
        return true;
    }
    options.min_size = parsedValue;
    return true;
}

bool applyStringOption(const std::vector<std::string>& args,
                       std::size_t& index,
                       const std::string& arg,
                       CliOptions& options) {
    std::string* destination = nullptr;
    if (arg == "--exclude") {
        std::string pattern;
        if (!takeStringOption(args, index, pattern)) {
            markInvalid(options, "--exclude expects a non-empty glob");
            return true;
        }
        options.exclude_patterns.push_back(std::move(pattern));
        return true;
    }
    if (arg == "--save-snapshot") {
        destination = &options.save_snapshot;
    } else if (arg == "--load-snapshot") {
        destination = &options.load_snapshot;
    } else if (arg == "--compare-snapshot") {
        destination = &options.compare_snapshot;
    } else {
        return false;
    }
    if (!takeStringOption(args, index, *destination)) {
        markInvalid(options, arg + " expects a non-empty file path");
        return true;
    }
    return true;
}

// Handles a non-option argument: the first one is the path, a second is an error.
void applyPositional(const std::string& arg, CliOptions& options) {
    const bool looksLikeOption = !arg.empty() && arg[0] == '-';
    if (looksLikeOption) {
        markInvalid(options, "unknown option: " + arg);
        return;
    }
    if (!options.path.empty()) {
        markInvalid(options, "only one path may be provided");
        return;
    }
    options.path = arg;
}

CliOptions parseArgs(int argc, char** argv) {
    CliOptions options;
    const std::vector<std::string> args(argv + 1, argv + argc);

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (applyFlag(arg, options)) {
            continue;
        }
        if (applyValueOption(args, i, arg, options)
            || applyStringOption(args, i, arg, options)) {
            continue;
        }
        applyPositional(arg, options);
    }
    return options;
}

int effectiveDepthCap(int requestedDepth) {
    if (requestedDepth < 0) {
        return diskmap::kMaxTreeDepth;
    }
    return std::min(requestedDepth, diskmap::kMaxTreeDepth);
}

void printTree(const diskmap::FsNode& node, int depth, int depthCap, std::ostream& out) {
    const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    out << indent << node.name << (node.is_dir ? "/" : "") << "  " << diskmap::humanBytes(node.size)
        << "\n";
    if (depth >= depthCap) {
        return;
    }
    for (const diskmap::FsNode& child : node.children) {
        printTree(child, depth + 1, depthCap, out);
    }
}

void printTopFiles(const diskmap::FsNode& root, std::size_t topN, std::ostream& out) {
    const std::vector<const diskmap::FsNode*> files = diskmap::topFiles(root, topN);
    out << "\nTop " << files.size() << " file(s):\n";
    for (const diskmap::FsNode* file : files) {
        const double ratio =
            root.size > 0 ? static_cast<double>(file->size) / static_cast<double>(root.size) : 0.0;
        out << "  " << diskmap::humanBytes(file->size) << "  (" << diskmap::formatPercent(ratio)
            << ")  " << file->name << "\n";
    }
}

void printJson(const diskmap::FsNode& node, int depth, int depthCap, std::ostream& out);

// Serializes the "children" array. Split out of printJson so neither function
// nests a loop inside a conditional inside a conditional.
void printJsonChildren(const diskmap::FsNode& node, int depth, int depthCap, std::ostream& out) {
    out << ",\"children\":[";
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        const char* separator = i > 0 ? "," : "";
        out << separator;
        printJson(node.children[i], depth + 1, depthCap, out);
    }
    out << "]";
}

void printJson(const diskmap::FsNode& node, int depth, int depthCap, std::ostream& out) {
    out << "{\"name\":\"" << diskmap_cli::escapeJsonStringContent(node.name)
        << "\",\"is_dir\":"
        << (node.is_dir ? "true" : "false") << ",\"size\":" << node.size;
    if (node.is_dir && !node.children.empty() && depth < depthCap) {
        printJsonChildren(node, depth, depthCap, out);
    }
    out << "}";
}

void printScanErrors(const std::vector<std::string>& errors) {
    for (const std::string& err : errors) {
        std::cerr << "error: " << err << "\n";
    }
}

bool scanFailedFatally(const diskmap::ScanResult& result) {
    return !result.fatal_error.empty();
}

void printSnapshotTree(diskmap::Snapshot snapshot,
                       const CliOptions& options,
                       std::ostream& out) {
    diskmap::sortBySizeDesc(snapshot.root);
    const int depthCap = effectiveDepthCap(options.depth);
    if (options.json) {
        // Snapshot JSON intentionally uses the versioned storage contract,
        // rather than the scan tree's display-only JSON shape.
        out << diskmap::serializeSnapshot(snapshot) << '\n';
    } else {
        printTree(snapshot.root, 0, depthCap, out);
        printTopFiles(snapshot.root, options.top, out);
    }
}

int runLoadedSnapshot(const CliOptions& options) {
    try {
        diskmap::Snapshot snapshot =
            diskmap::readSnapshotFile(options.load_snapshot);
        if (!options.save_snapshot.empty()) {
            diskmap::writeSnapshotAtomically(snapshot, options.save_snapshot);
        }
        if (options.duplicates) {
            const diskmap::ScanResult result =
                diskmap::scanEvidenceFromSnapshot(snapshot);
            const diskmap::DuplicateAnalysis analysis =
                diskmap::analyzeDuplicates(result);
            diskmap_cli::printDuplicateAnalysis(analysis, options.json, std::cout);
            return 0;
        }
        printSnapshotTree(std::move(snapshot), options, std::cout);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "snapshot: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "snapshot: unknown error\n";
        return 1;
    }
}

int runDiskmap(const CliOptions& options) {
    std::optional<diskmap::Snapshot> before;
    if (!options.compare_snapshot.empty()) {
        try {
            before = diskmap::readSnapshotFile(options.compare_snapshot);
        } catch (const std::exception& error) {
            std::cerr << "snapshot: " << error.what() << "\n";
            return 1;
        } catch (...) {
            std::cerr << "snapshot: unknown error\n";
            return 1;
        }
    }
    diskmap::RealFsSource source;
    // --depth caps how much is printed. --max-depth is a scanner bound and is
    // intentionally passed through so callers can trade complete totals for
    // bounded traversal when they need to.
    diskmap::ScanOptions scanOptions;
    scanOptions.max_depth = options.max_depth;
    scanOptions.follow_symlinks = options.follow_symlinks;
    scanOptions.min_size = options.min_size;
    scanOptions.one_file_system = options.one_file_system;
    scanOptions.exclude_patterns = options.exclude_patterns;

    diskmap::ScanResult result = diskmap::scan(source, options.path, scanOptions);
    if (scanFailedFatally(result)) {
        std::cerr << "fatal: " << result.fatal_error << "\n";
        return 1;
    }

    diskmap::sortBySizeDesc(result.root);
    printScanErrors(result.errors);

    const diskmap::Snapshot current = diskmap::snapshotFromNode(result.root);
    if (!options.save_snapshot.empty()) {
        try {
            diskmap::writeSnapshotAtomically(current, options.save_snapshot);
        } catch (const std::exception& error) {
            std::cerr << "snapshot: " << error.what() << "\n";
            return 1;
        } catch (...) {
            std::cerr << "snapshot: unknown error\n";
            return 1;
        }
    }
    if (before.has_value()) {
        diskmap::SnapshotDiffOptions diffOptions;
        const diskmap::SnapshotDiff diff =
            diskmap::diffSnapshots(*before, current, diffOptions);
        diskmap_cli::printSnapshotDiff(diff, options.json, std::cout);
        return 0;
    }
    if (options.duplicates) {
        const diskmap::DuplicateAnalysis analysis =
            diskmap::analyzeDuplicates(result);
        diskmap_cli::printDuplicateAnalysis(analysis, options.json, std::cout);
        return 0;
    }

    const int depthCap = effectiveDepthCap(options.depth);
    if (options.json) {
        printJson(result.root, 0, depthCap, std::cout);
        std::cout << "\n";
    } else {
        printTree(result.root, 0, depthCap, std::cout);
        printTopFiles(result.root, options.top, std::cout);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const CliOptions options = parseArgs(argc, argv);
    if (options.help) {
        printUsage(std::cout);
        return 0;
    }
    const bool hasLoadedSnapshot = !options.load_snapshot.empty();
    if (!options.valid) {
        if (!options.error.empty()) {
            std::cerr << "error: " << options.error << "\n";
        }
        printUsage(std::cerr);
        return 1;
    }
    if (hasLoadedSnapshot && !options.path.empty()) {
        std::cerr << "error: --load-snapshot cannot be combined with a scan path\n";
        printUsage(std::cerr);
        return 1;
    }
    if (!hasLoadedSnapshot && options.path.empty()) {
        std::cerr << "error: a scan path or --load-snapshot is required\n";
        printUsage(std::cerr);
        return 1;
    }
    if (hasLoadedSnapshot && !options.compare_snapshot.empty()) {
        std::cerr << "error: --compare-snapshot requires a live scan path\n";
        printUsage(std::cerr);
        return 1;
    }
    if (hasLoadedSnapshot) {
        return runLoadedSnapshot(options);
    }
    return runDiskmap(options);
}
