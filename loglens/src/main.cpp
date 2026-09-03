#include "loglens/filter_expr.hpp"
#include "loglens/log_parser.hpp"
#include "loglens/log_record.hpp"
#include "loglens/log_source.hpp"
#include "loglens/log_stats.hpp"
#include "loglens/ring_buffer.hpp"

#include <algorithm>
#include <charconv>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

struct CliOptions {
    std::string path;
    std::string filter;
    std::string level;
    std::string format = "auto";
    bool stats = false;
    bool help = false;
    bool valid = true;
    std::uint64_t bucket_ms = 60000;
    std::size_t top = 10;
    std::size_t capacity = loglens::kDefaultRecordCapacity;
};

struct ActiveFilters {
    std::optional<loglens::Filter> level;
    std::optional<loglens::Filter> expression;

    bool matches(const loglens::LogRecord& record) const {
        return (!level || level->matches(record))
               && (!expression || expression->matches(record));
    }
};

struct FilterArgument {
    std::string parserText;
    std::size_t userOffset = 0;
    std::size_t userSize = 0;
    const char* optionName = "--filter";
};

void printUsage(std::ostream& out) {
    out << "Usage: loglens <file> [options]\n"
        << "  --filter EXPR   e.g. \"level>=WARN AND message~timeout\"\n"
        << "  --level LEVEL   shorthand for level>=LEVEL\n"
        << "  --format F      auto|plain|syslog|json|raw (default auto)\n"
        << "  --stats         print level histogram and top patterns\n"
        << "  --bucket MS     histogram bucket size (default 60000)\n"
        << "  --top N         number of patterns to show (default 10)\n"
        << "  --capacity N    retained record limit (default 8192)\n"
        << "  --help          show this message\n";
}

bool takeValue(const std::vector<std::string>& args, std::size_t& index, std::string& out) {
    if (index + 1 >= args.size()) {
        return false;
    }
    ++index;
    out = args[index];
    return true;
}

bool applyFlag(const std::string& arg, CliOptions& options) {
    if (arg == "--help") {
        options.help = true;
        return true;
    }
    if (arg == "--stats") {
        options.stats = true;
        return true;
    }
    return false;
}

// Each string-valued option is a field assignment, so a table keeps them from
// repeating the same three-line block per option.
bool applyStringOption(const std::vector<std::string>& args, std::size_t& index,
                       const std::string& arg, CliOptions& options) {
    std::string* target = nullptr;
    if (arg == "--filter") {
        target = &options.filter;
    } else if (arg == "--level") {
        target = &options.level;
    } else if (arg == "--format") {
        target = &options.format;
    }
    if (target == nullptr) {
        return false;
    }
    options.valid = takeValue(args, index, *target) && options.valid;
    return true;
}

bool applyNumberOption(const std::vector<std::string>& args, std::size_t& index,
                       const std::string& arg, CliOptions& options) {
    if (arg != "--bucket" && arg != "--top" && arg != "--capacity") {
        return false;
    }
    std::string raw;
    if (!takeValue(args, index, raw)) {
        options.valid = false;
        return true;
    }
    std::uint64_t value = 0;
    const char* first = raw.data();
    const char* last = first + raw.size();
    const std::from_chars_result parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc() || parsed.ptr != last || value == 0) {
        options.valid = false;
        return true;
    }
    if (arg == "--bucket") {
        options.bucket_ms = value;
    } else if (arg == "--top") {
        if (value > std::numeric_limits<std::size_t>::max()) {
            options.valid = false;
            return true;
        }
        options.top = static_cast<std::size_t>(value);
    } else {
        if (value > loglens::kMaxRecordCapacity) {
            options.valid = false;
            return true;
        }
        options.capacity = static_cast<std::size_t>(value);
    }
    return true;
}

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
        if (applyFlag(arg, options) || applyStringOption(args, i, arg, options) ||
            applyNumberOption(args, i, arg, options)) {
            continue;
        }
        applyPositional(arg, options);
    }
    return options;
}

loglens::Format resolveFormat(const std::string& name) {
    if (name == "plain") {
        return loglens::Format::PlainIso;
    }
    if (name == "syslog") {
        return loglens::Format::Syslog;
    }
    if (name == "json") {
        return loglens::Format::JsonLine;
    }
    if (name == "raw") {
        return loglens::Format::Raw;
    }
    return loglens::Format::Auto;
}

bool parseFilterArgument(const FilterArgument& argument,
                         std::optional<loglens::Filter>& output) {
    loglens::ParseError error;
    output = loglens::Filter::parse(argument.parserText, error);
    if (output) {
        return true;
    }
    const auto userPosition = [&argument](std::size_t parserPosition) {
        if (parserPosition <= argument.userOffset) {
            return std::size_t{0};
        }
        return std::min(parserPosition - argument.userOffset, argument.userSize);
    };
    std::cerr << "fatal: bad " << argument.optionName << " at bytes ["
              << userPosition(error.position) << "," << userPosition(error.end)
              << "): " << error.message << "\n";
    return false;
}

bool initializeFilters(const CliOptions& options, ActiveFilters& filters) {
    constexpr std::size_t kLevelPrefixBytes = 7;
    if (!options.level.empty()
        && !parseFilterArgument(
            {"level>=" + options.level, kLevelPrefixBytes, options.level.size(), "--level"},
            filters.level)) {
        return false;
    }
    return options.filter.empty()
           || parseFilterArgument(
               {options.filter, 0, options.filter.size(), "--filter"}, filters.expression);
}

// Applies the parser's explicit append/extend contract to the same bounded FIFO
// used by the GUI. Absolute record IDs keep continuation updates correct after
// the oldest entries have wrapped out.
void applyDeltas(const std::vector<loglens::RecordDelta>& deltas,
                 loglens::RingBuffer& records) {
    for (const loglens::RecordDelta& delta : deltas) {
        if (delta.kind == loglens::RecordDelta::Kind::Append) {
            if (delta.record_index == records.totalPushed()) {
                records.push(delta.record);
            }
            continue;
        }
        records.replace(delta.record_index, delta.record);
    }
}

void printRetentionSummary(const loglens::RingBuffer& records, std::ostream& out) {
    out << records.totalPushed() << " seen, " << records.droppedCount() << " dropped, lines ";
    if (records.empty()) {
        out << "none";
    } else {
        out << records.at(0).line_number << '-' << records.at(records.size() - 1).line_number;
    }
    out << ", capacity " << records.capacity() << "\n";
}

void printRecords(const loglens::RingBuffer& records, const ActiveFilters& filters,
                  std::ostream& out) {
    std::size_t shown = 0;
    for (std::size_t index = 0; index < records.size(); ++index) {
        const loglens::LogRecord& record = records.at(index);
        if (!filters.matches(record)) {
            continue;
        }
        out << record.line_number << "  " << loglens::levelName(record.level) << "  "
            << record.source << "  " << record.message;
        if (record.omitted_bytes > 0) {
            out << "  [" << record.omitted_bytes << " source byte(s) omitted]";
        }
        out << "\n";
        ++shown;
    }
    out << "\n" << shown << " / " << records.size() << " line(s)\n";
    printRetentionSummary(records, out);
}

void printStats(const loglens::RingBuffer& records,
                const ActiveFilters& filters, const CliOptions& options,
                std::ostream& out) {
    loglens::Stats stats;
    for (std::size_t index = 0; index < records.size(); ++index) {
        const loglens::LogRecord& record = records.at(index);
        if (filters.matches(record)) {
            stats.add(record);
        }
    }
    out << "Buckets (" << options.bucket_ms << " ms):\n";
    for (const loglens::Bucket& bucket : stats.buckets(options.bucket_ms)) {
        out << "  " << bucket.start_ms;
        for (std::size_t i = 0; i < loglens::kLevelCount; ++i) {
            out << "  " << bucket.level_counts[i];
        }
        out << "\n";
    }
    out << "\nTop patterns:\n";
    for (const auto& entry : stats.topPatterns(options.top)) {
        out << "  " << entry.second << "  " << entry.first << "\n";
    }
    out << "\n" << stats.total() << " matching line(s)\n";
    printRetentionSummary(records, out);
}

int run(const CliOptions& options) {
    ActiveFilters filters;
    if (!initializeFilters(options, filters)) {
        return 1;
    }

    const loglens::Format format = resolveFormat(options.format);
    loglens::RecordAssembler assembler(format);
    loglens::RingBuffer records(options.capacity);
    loglens::FileTailer tailer(options.path);
    std::optional<std::uint64_t> snapshotEnd;
    while (true) {
        loglens::SourceChunk chunk =
            snapshotEnd ? tailer.pollChunk(*snapshotEnd) : tailer.pollChunk();
        if (!chunk.ok()) {
            std::cerr << "fatal: " << chunk.error.message << "\n";
            return 1;
        }
        if (chunk.generation != assembler.generation()) {
            assembler.reset(chunk.generation);
            records.clear();
            snapshotEnd = chunk.snapshot_end;
        } else if (!snapshotEnd) {
            snapshotEnd = chunk.snapshot_end;
        }
        applyDeltas(assembler.consumeBytes(chunk.bytes), records);
        if (!chunk.more_available) {
            break;
        }
    }
    // A one-shot CLI invocation is an explicit EOF decision: expose a final
    // record even when the file does not end in a newline. Follow mode never
    // calls flush(), so an append can still complete the same partial line.
    applyDeltas(assembler.flush(), records);

    if (options.stats) {
        printStats(records, filters, options, std::cout);
    } else {
        printRecords(records, filters, std::cout);
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
    return run(options);
}
