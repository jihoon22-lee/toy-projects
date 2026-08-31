#include "diskmap/format.hpp"
#include "diskmap/fs_node.hpp"
#include "diskmap/fs_source.hpp"
#include "diskmap/scanner.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <limits>
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
    bool help = false;
    bool valid = true;
    std::string error;
};

void printUsage(std::ostream& out) {
    out << "Usage: diskmap <path> [options]\n"
        << "  --max-depth N       limit scan traversal depth\n"
        << "  --follow-symlinks   follow symlinked directories\n"
        << "  --min-size BYTES    skip files smaller than BYTES\n"
        << "  --one-file-system   do not cross filesystem boundaries\n"
        << "  --exclude GLOB      skip matching entries (repeatable)\n"
        << "  --depth N           limit printed tree depth\n"
        << "  --top N             show the N largest files (default 10)\n"
        << "  --json              emit the tree as JSON instead of text\n"
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
    if (arg != "--exclude") {
        return false;
    }
    std::string pattern;
    if (!takeStringOption(args, index, pattern)) {
        markInvalid(options, "--exclude expects a non-empty glob");
        return true;
    }
    options.exclude_patterns.push_back(std::move(pattern));
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

// Returns the short JSON escape for c, or nullptr when c has none.
// A lookup keeps each mapping on one line; the previous switch repeated a
// three-line case/append/break block per character, which reads as a clone.
const char* shortJsonEscape(unsigned char c) {
    switch (c) {
        case '"': return "\\\"";
        case '\\': return "\\\\";
        case '\b': return "\\b";
        case '\f': return "\\f";
        case '\n': return "\\n";
        case '\r': return "\\r";
        case '\t': return "\\t";
        default: return nullptr;
    }
}

std::string jsonEscape(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (unsigned char c : text) {
        const char* shortForm = shortJsonEscape(c);
        if (shortForm != nullptr) {
            escaped += shortForm;
        } else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            escaped += buf;
        } else {
            escaped += static_cast<char>(c);
        }
    }
    return escaped;
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
    out << "{\"name\":\"" << jsonEscape(node.name) << "\",\"is_dir\":"
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

int runDiskmap(const CliOptions& options) {
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
    diskmap::sortBySizeDesc(result.root);
    printScanErrors(result.errors);

    if (scanFailedFatally(result)) {
        std::cerr << "fatal: " << result.fatal_error << "\n";
        return 1;
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
    if (!options.valid || options.path.empty()) {
        if (!options.error.empty()) {
            std::cerr << "error: " << options.error << "\n";
        }
        printUsage(std::cerr);
        return 1;
    }
    return runDiskmap(options);
}
