#include "diskmap/fs_node.hpp"
#include "diskmap/fs_source.hpp"
#include "diskmap/scanner.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/resource.h>
#endif

namespace {

constexpr std::uint64_t kDevice = 71;
constexpr std::uint64_t kAllocationBytes = 4096;
constexpr std::uint64_t kSizeCycle = 4096;
constexpr std::size_t kMaximumEntries = 10'000'000;

class GeneratedFsSource final : public diskmap::FsSource {
public:
    GeneratedFsSource(std::size_t entries,
                      std::size_t cancelAfter,
                      diskmap::ScanCancellationToken* cancellation)
        : entries_(entries), cancelAfter_(cancelAfter), cancellation_(cancellation) {}

    std::vector<diskmap::DirEntry> list(
        const std::filesystem::path& path,
        std::string& error,
        const diskmap::CancellationCheck& cancelled = {}) const override {
        error.clear();
        if (path != std::filesystem::path("/generated")) {
            error = "unexpected generated path";
            return {};
        }

        std::vector<diskmap::DirEntry> result;
        result.reserve(entries_);
        for (std::size_t index = 0; index < entries_; ++index) {
            if ((cancelled && cancelled()) || shouldCancel(index)) {
                break;
            }
            result.push_back(makeEntry(index));
            ++generated_;
        }
        return result;
    }

    diskmap::FsMetadata inspect(const std::filesystem::path& path, bool follow) const override {
        diskmap::FsMetadata metadata;
        if (path != std::filesystem::path("/generated") || follow) {
            metadata.error = "unexpected generated root inspection";
            return metadata;
        }
        metadata.kind = diskmap::FsKind::Directory;
        metadata.identity = diskmap::FileIdentity{kDevice, 1, true};
        metadata.complete = true;
        return metadata;
    }

    std::size_t generated() const { return generated_; }

private:
    bool shouldCancel(std::size_t index) const {
        if (cancelAfter_ == 0 || index < cancelAfter_) {
            return false;
        }
        cancellation_->cancel();
        return true;
    }

    static diskmap::DirEntry makeEntry(std::size_t index) {
        diskmap::DirEntry entry;
        entry.name = "file-" + std::to_string(index);
        entry.size = static_cast<std::uint64_t>(index % kSizeCycle) + 1;
        entry.metadata.kind = diskmap::FsKind::RegularFile;
        entry.metadata.identity =
            diskmap::FileIdentity{kDevice, static_cast<std::uint64_t>(index) + 2, true};
        entry.metadata.logical_size = entry.size;
        entry.metadata.allocated_size = kAllocationBytes;
        entry.metadata.allocated_size_known = true;
        entry.metadata.hard_link_count = 1;
        entry.metadata.hard_link_count_known = true;
        entry.metadata.complete = true;
        return entry;
    }

    std::size_t entries_;
    std::size_t cancelAfter_;
    diskmap::ScanCancellationToken* cancellation_;
    mutable std::size_t generated_ = 0;
};

struct Options {
    std::size_t entries = 1'000'000;
    std::size_t cancel_after = 0;
    std::uint64_t generation = 1;
    bool valid = true;
};

bool parseUnsigned(const std::string& value, std::uint64_t& output) {
    if (value.empty() || value.front() == '-') {
        return false;
    }
    std::size_t consumed = 0;
    try {
        output = std::stoull(value, &consumed);
    } catch (const std::exception&) {
        return false;
    }
    return consumed == value.size();
}

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (index + 1 >= argc) {
            options.valid = false;
            break;
        }
        std::uint64_t value = 0;
        if (!parseUnsigned(argv[++index], value)) {
            options.valid = false;
            break;
        }
        if (argument == "--entries" && value <= std::numeric_limits<std::size_t>::max()) {
            options.entries = static_cast<std::size_t>(value);
        } else if (argument == "--cancel-after"
                   && value <= std::numeric_limits<std::size_t>::max()) {
            options.cancel_after = static_cast<std::size_t>(value);
        } else if (argument == "--generation") {
            options.generation = value;
        } else {
            options.valid = false;
            break;
        }
    }
    if (options.entries == 0 || options.entries > kMaximumEntries
        || options.cancel_after > options.entries) {
        options.valid = false;
    }
    return options;
}

std::uint64_t expectedLogicalSize(std::size_t entries) {
    const std::uint64_t fullCycles = static_cast<std::uint64_t>(entries) / kSizeCycle;
    const std::uint64_t remainder = static_cast<std::uint64_t>(entries) % kSizeCycle;
    const std::uint64_t cycleSum = kSizeCycle * (kSizeCycle + 1) / 2;
    return fullCycles * cycleSum + remainder * (remainder + 1) / 2;
}

double peakRssMiB() {
#ifndef _WIN32
    struct rusage usage {};
    if (::getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }
#if defined(__APPLE__)
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.0;
#endif
#else
    return 0.0;
#endif
}

bool checkFullResult(const diskmap::ScanResult& result, const Options& options) {
    if (result.cancelled || result.error_count != 0 || result.files_scanned != options.entries
        || result.generation != options.generation
        || diskmap::countNodes(result.root) != options.entries + 1
        || result.root.size != expectedLogicalSize(options.entries)
        || !result.root.allocated_size_known || !result.root.reclaimable_size_known) {
        return false;
    }
    const std::uint64_t expectedStorage =
        static_cast<std::uint64_t>(options.entries) * kAllocationBytes;
    return result.root.allocated_size == expectedStorage
           && result.root.reclaimable_size == expectedStorage
           && result.root.scan_generation == options.generation;
}

bool checkCancelledResult(const diskmap::ScanResult& result,
                          const Options& options,
                          std::size_t generated) {
    return result.cancelled && !result.root.complete && result.error_count == 0
           && result.generation == options.generation && generated == options.cancel_after
           && result.dirs_scanned == 0 && result.files_scanned == 0;
}

} // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    if (!options.valid) {
        std::cerr << "usage: diskmap-scan-benchmark --entries N "
                     "[--cancel-after N] [--generation N]\n";
        return 2;
    }

    diskmap::ScanCancellationToken cancellation;
    GeneratedFsSource source(options.entries, options.cancel_after, &cancellation);
    diskmap::ScanOptions scanOptions;
    scanOptions.generation = options.generation;

    const auto started = std::chrono::steady_clock::now();
    diskmap::ScanResult result =
        diskmap::scan(source, "/generated", scanOptions, nullptr, &cancellation);
    if (!result.cancelled) {
        diskmap::sortBySizeDesc(result.root);
    }
    const auto finished = std::chrono::steady_clock::now();
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(finished - started).count();
    const bool correctness = options.cancel_after == 0
                                 ? checkFullResult(result, options)
                                 : checkCancelledResult(result, options, source.generated());
    const double entriesPerSecond =
        elapsedMs > 0.0 ? static_cast<double>(source.generated()) * 1000.0 / elapsedMs : 0.0;

    std::cout << std::fixed << std::setprecision(3)
              << "{\"schema\":\"diskmap-scan-benchmark/v1\","
              << "\"entries_requested\":" << options.entries << ','
              << "\"entries_generated\":" << source.generated() << ','
              << "\"cancel_after\":" << options.cancel_after << ','
              << "\"generation\":" << options.generation << ','
              << "\"cancelled\":" << (result.cancelled ? "true" : "false") << ','
              << "\"correctness\":" << (correctness ? "true" : "false") << ','
              << "\"elapsed_ms\":" << elapsedMs << ','
              << "\"entries_per_s\":" << entriesPerSecond << ','
              << "\"peak_rss_mib\":" << peakRssMiB() << ','
              << "\"nodes_retained\":" << diskmap::countNodes(result.root) << ','
              << "\"logical_bytes\":" << result.root.size << ','
              << "\"allocated_bytes\":" << result.root.allocated_size << "}\n";
    return correctness ? 0 : 1;
}
