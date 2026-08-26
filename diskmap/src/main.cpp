#include "core/format.hpp"
#include "core/fs_node.hpp"
#include "core/fs_source.hpp"
#include "core/scanner.hpp"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct CliOptions {
    std::string path;
    int depth = -1;
    std::size_t top = 10;
    bool json = false;
    bool help = false;
    bool valid = true;
};

void printUsage(std::ostream& out) {
    out << "Usage: diskmap <path> [--depth N] [--top N] [--json] [--help]\n"
        << "  --depth N  limit printed tree depth (the scan always covers the full tree)\n"
        << "  --top N    show the N largest files (default 10)\n"
        << "  --json     emit the tree as JSON instead of text\n";
}

bool parseNonNegativeInt(const std::string& value, int& out) {
    if (value.empty()) {
        return false;
    }
    std::size_t consumed = 0;
    int parsed = 0;
    try {
        parsed = std::stoi(value, &consumed);
    } catch (const std::exception&) {
        return false;
    }
    if (consumed != value.size() || parsed < 0) {
        return false;
    }
    out = parsed;
    return true;
}

// Consumes the option's value (if present) and reports whether it parsed.
bool takeIntOption(const std::vector<std::string>& args, std::size_t& index, int& out) {
    if (index + 1 >= args.size()) {
        return false;
    }
    ++index;
    return parseNonNegativeInt(args[index], out);
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
    return false;
}

// Handles an option that consumes the following argument as its value.
// Returns false when arg is not one, advancing index only when it is.
bool applyValueOption(const std::vector<std::string>& args,
                      std::size_t& index,
                      const std::string& arg,
                      CliOptions& options) {
    if (arg == "--depth") {
        options.valid = takeIntOption(args, index, options.depth) && options.valid;
        return true;
    }
    if (arg != "--top") {
        return false;
    }
    int parsedValue = 0;
    const bool parsed = takeIntOption(args, index, parsedValue);
    options.valid = parsed && options.valid;
    options.top = parsed ? static_cast<std::size_t>(parsedValue) : options.top;
    return true;
}

// Handles a non-option argument: the first one is the path, a second is an error.
void applyPositional(const std::string& arg, CliOptions& options) {
    const bool looksLikeOption = !arg.empty() && arg[0] == '-';
    if (looksLikeOption || !options.path.empty()) {
        options.valid = false;
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
        if (applyValueOption(args, i, arg, options)) {
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
    return result.dirs_scanned == 0 && !result.errors.empty();
}

int runDiskmap(const CliOptions& options) {
    diskmap::RealFsSource source;
    // Always scan the full tree: --depth caps how much is PRINTED, the way
    // du --max-depth does. Limiting the scan instead would make the reported
    // totals shrink with --depth, which is exactly the number a disk-usage
    // tool must get right.
    const diskmap::ScanOptions scanOptions;

    diskmap::ScanResult result = diskmap::scan(source, options.path, scanOptions);
    diskmap::sortBySizeDesc(result.root);
    printScanErrors(result.errors);

    if (scanFailedFatally(result)) {
        std::cerr << "fatal: unable to scan '" << options.path << "'\n";
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
        printUsage(std::cerr);
        return 1;
    }
    return runDiskmap(options);
}
