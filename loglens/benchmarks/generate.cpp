// Deterministic input generator for the opt-in LogLens large-file benchmark.
//
// The default dimensions are deliberately chosen so the line-size split is
// exact in binary units:
//
//   741,824 * 1,074 + 258,176 * 1,073 == 1,073,741,824
//
// Keeping this as a small, dependency-free executable makes it useful on a
// benchmark runner without requiring Python or any project runtime.

#include <array>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::uint64_t kDefaultBytes = 1ULL << 30;
constexpr std::uint64_t kDefaultRecords = 1'000'000;
constexpr std::uint64_t kDefaultShortLineBytes = 1'073;
constexpr std::uint64_t kDefaultLongLineBytes = 1'074;
constexpr std::uint64_t kDefaultLongLineCount = 741'824;
constexpr std::uint64_t kMaxRecords = 10'000'000;
constexpr std::size_t kWriteBufferBytes = 1U << 20;
constexpr std::size_t kPayloadBufferBytes = 64U << 10;
constexpr std::string_view kTimestamp = "2026-08-26T04:15:22.123Z";
constexpr std::string_view kSource = "benchmark";
constexpr std::string_view kPayloadAlphabet =
    "abcdefghijklmnopqrstuvwxyz0123456789";
constexpr std::array<std::string_view, 4> kLevels = {
    "INFO", "WARN", "ERROR", "DEBUG",
};

static_assert(kDefaultLongLineCount < kDefaultRecords);
static_assert(kDefaultLongLineCount * kDefaultLongLineBytes
                  + (kDefaultRecords - kDefaultLongLineCount) * kDefaultShortLineBytes
              == kDefaultBytes);

struct Config {
    std::uint64_t bytes = kDefaultBytes;
    std::uint64_t records = kDefaultRecords;
    std::string output = "loglens-benchmark.log";
};

struct Verification {
    std::uint64_t bytes = 0;
    std::uint64_t newlines = 0;
};

[[noreturn]] void usageError(const std::string& message) {
    throw std::invalid_argument(message +
                                "\nusage: loglens-bench-generate [--bytes N] "
                                "[--records N] [--output PATH]");
}

std::uint64_t parsePositiveInteger(std::string_view text, std::string_view option) {
    if (text.empty()) {
        usageError(std::string(option) + " requires a positive decimal integer");
    }
    // from_chars intentionally does not accept a sign or surrounding
    // whitespace. Rejecting those spellings keeps malformed CI inputs from
    // silently selecting a different benchmark size.
    for (const char value : text) {
        if (value < '0' || value > '9') {
            usageError(std::string(option) + " must be a positive decimal integer");
        }
    }

    std::uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec == std::errc::result_out_of_range || parsed.ptr != text.data() + text.size() ||
        result == 0) {
        usageError(std::string(option) + " is outside the supported positive range");
    }
    return result;
}

std::string requireValue(int& index, int argc, char** argv, std::string_view option) {
    if (index + 1 >= argc) {
        usageError(std::string(option) + " requires a value");
    }
    ++index;
    const std::string value = argv[index];
    if (value.empty()) {
        usageError(std::string(option) + " requires a non-empty value");
    }
    return value;
}

std::string optionValue(std::string_view argument, std::string_view option) {
    const std::size_t equals = argument.find('=');
    if (equals == std::string_view::npos || argument.substr(0, equals) != option) {
        return {};
    }
    const std::string_view value = argument.substr(equals + 1);
    if (value.empty()) {
        usageError(std::string(option) + " requires a value");
    }
    return std::string(value);
}

Config parseArguments(int argc, char** argv) {
    Config config;
    bool bytesSeen = false;
    bool recordsSeen = false;
    bool outputSeen = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            std::cout << "usage: loglens-bench-generate [--bytes N] [--records N] "
                         "[--output PATH]\n"
                         "defaults: 1073741824 bytes, 1000000 records, "
                         "loglens-benchmark.log\n";
            std::exit(0);
        }

        std::string value;
        if (argument == "--bytes") {
            if (bytesSeen) {
                usageError("--bytes was specified more than once");
            }
            value = requireValue(index, argc, argv, "--bytes");
            config.bytes = parsePositiveInteger(value, "--bytes");
            bytesSeen = true;
        } else if (argument.substr(0, 8) == "--bytes=") {
            if (bytesSeen) {
                usageError("--bytes was specified more than once");
            }
            value = optionValue(argument, "--bytes");
            config.bytes = parsePositiveInteger(value, "--bytes");
            bytesSeen = true;
        } else if (argument == "--records") {
            if (recordsSeen) {
                usageError("--records was specified more than once");
            }
            value = requireValue(index, argc, argv, "--records");
            config.records = parsePositiveInteger(value, "--records");
            recordsSeen = true;
        } else if (argument.substr(0, 10) == "--records=") {
            if (recordsSeen) {
                usageError("--records was specified more than once");
            }
            value = optionValue(argument, "--records");
            config.records = parsePositiveInteger(value, "--records");
            recordsSeen = true;
        } else if (argument == "--output") {
            if (outputSeen) {
                usageError("--output was specified more than once");
            }
            config.output = requireValue(index, argc, argv, "--output");
            outputSeen = true;
        } else if (argument.substr(0, 9) == "--output=") {
            if (outputSeen) {
                usageError("--output was specified more than once");
            }
            config.output = optionValue(argument, "--output");
            outputSeen = true;
        } else {
            usageError("unknown argument '" + std::string(argument) + "'");
        }
    }

    if (config.records > kMaxRecords) {
        usageError("--records cannot exceed 10000000 (the index is exactly 7 digits)");
    }
    if (config.output == "-") {
        usageError("--output '-' is not supported because stdout is reserved for JSON summary");
    }
    return config;
}

std::string formatIndex(std::uint64_t index) {
    std::array<char, 7> digits{};
    for (std::size_t position = digits.size(); position > 0; --position) {
        digits[position - 1] = static_cast<char>('0' + (index % 10));
        index /= 10;
    }
    if (index != 0) {
        throw std::logic_error("record index does not fit the fixed seven-digit field");
    }
    return std::string(digits.data(), digits.size());
}

std::string makePrefix(std::uint64_t recordIndex) {
    const std::string index = formatIndex(recordIndex);
    std::string prefix;
    prefix.reserve(kTimestamp.size() + kLevels[0].size() + kSource.size() + index.size() + 8);
    prefix.append(kTimestamp);
    prefix.push_back(' ');
    prefix.append(kLevels[recordIndex % kLevels.size()]);
    prefix.append(" [");
    prefix.append(kSource);
    prefix.append("] record-");
    prefix.append(index);
    prefix.push_back(' ');
    return prefix;
}

void flushBuffer(std::ofstream& output, std::string& buffer) {
    if (buffer.empty()) {
        return;
    }
    output.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (!output) {
        throw std::runtime_error("failed while writing benchmark output");
    }
    buffer.clear();
}

void appendBytes(std::ofstream& output, std::string& buffer, std::string_view bytes) {
    while (!bytes.empty()) {
        const std::size_t available = kWriteBufferBytes - buffer.size();
        const std::size_t count = std::min(available, bytes.size());
        buffer.append(bytes.data(), count);
        bytes.remove_prefix(count);
        if (buffer.size() == kWriteBufferBytes) {
            flushBuffer(output, buffer);
        }
    }
}

void appendByte(std::ofstream& output, std::string& buffer, char byte) {
    buffer.push_back(byte);
    if (buffer.size() == kWriteBufferBytes) {
        flushBuffer(output, buffer);
    }
}

void appendPayload(std::ofstream& output, std::string& buffer, std::uint64_t recordIndex,
                   std::uint64_t payloadBytes) {
    std::array<char, kPayloadBufferBytes> chunk{};
    std::uint64_t offset = 0;
    const std::uint64_t seed = recordIndex * 131U + 17U;
    while (offset < payloadBytes) {
        const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(
            payloadBytes - offset, static_cast<std::uint64_t>(chunk.size())));
        for (std::size_t position = 0; position < count; ++position) {
            const std::uint64_t alphabetIndex =
                (seed + offset + static_cast<std::uint64_t>(position)) % kPayloadAlphabet.size();
            chunk[position] = kPayloadAlphabet[static_cast<std::size_t>(alphabetIndex)];
        }
        appendBytes(output, buffer, std::string_view(chunk.data(), count));
        offset += count;
    }
}

void generate(const Config& config) {
    if (config.records == 0 || config.bytes == 0) {
        throw std::invalid_argument("bytes and records must be positive");
    }
    if (config.records > kMaxRecords) {
        throw std::invalid_argument("records exceed the fixed seven-digit index range");
    }

    const std::uint64_t baseLineBytes = config.bytes / config.records;
    const std::uint64_t longLineCount = config.bytes % config.records;
    if (baseLineBytes == 0 || (longLineCount != 0 && baseLineBytes == std::numeric_limits<std::uint64_t>::max())) {
        throw std::invalid_argument("requested byte count cannot provide one byte per record");
    }

    // Every record has a newline and at least one deterministic payload byte.
    // Check every level actually used because ERROR and DEBUG have a longer
    // header than INFO/WARN.
    for (std::uint64_t record = 0; record < config.records; ++record) {
        const std::string prefix = makePrefix(record);
        const std::uint64_t lineBytes =
            baseLineBytes + (record < longLineCount ? std::uint64_t{1} : std::uint64_t{0});
        const std::uint64_t minimumBytes = static_cast<std::uint64_t>(prefix.size()) + 2U;
        if (lineBytes < minimumBytes) {
            throw std::invalid_argument("requested line size is shorter than the PlainIso header");
        }
    }

    std::ofstream output(config.output, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot open output '" + config.output + "'");
    }
    output.exceptions(std::ios::badbit);

    std::string buffer;
    buffer.reserve(kWriteBufferBytes);
    for (std::uint64_t record = 0; record < config.records; ++record) {
        const std::string prefix = makePrefix(record);
        const std::uint64_t lineBytes =
            baseLineBytes + (record < longLineCount ? std::uint64_t{1} : std::uint64_t{0});
        const std::uint64_t payloadBytes =
            lineBytes - static_cast<std::uint64_t>(prefix.size()) - 1U;
        appendBytes(output, buffer, prefix);
        appendPayload(output, buffer, record, payloadBytes);
        appendByte(output, buffer, '\n');
    }
    flushBuffer(output, buffer);
    output.close();
    if (!output) {
        throw std::runtime_error("failed while closing benchmark output");
    }
}

Verification verify(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot reopen output '" + path + "' for verification");
    }

    Verification result;
    std::array<char, kWriteBufferBytes> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count <= 0) {
            break;
        }
        const std::uint64_t chunkBytes = static_cast<std::uint64_t>(count);
        if (result.bytes > std::numeric_limits<std::uint64_t>::max() - chunkBytes) {
            throw std::runtime_error("verification byte counter overflowed");
        }
        result.bytes += chunkBytes;
        for (std::streamsize index = 0; index < count; ++index) {
            if (buffer[static_cast<std::size_t>(index)] == '\n') {
                if (result.newlines == std::numeric_limits<std::uint64_t>::max()) {
                    throw std::runtime_error("verification newline counter overflowed");
                }
                ++result.newlines;
            }
        }
    }
    if (input.bad()) {
        throw std::runtime_error("failed while verifying benchmark output");
    }
    return result;
}

std::string jsonEscape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char byte : value) {
        switch (byte) {
            case '"': escaped.append("\\\""); break;
            case '\\': escaped.append("\\\\"); break;
            case '\b': escaped.append("\\b"); break;
            case '\f': escaped.append("\\f"); break;
            case '\n': escaped.append("\\n"); break;
            case '\r': escaped.append("\\r"); break;
            case '\t': escaped.append("\\t"); break;
            default:
                if (byte < 0x20U) {
                    escaped.append("\\u00");
                    constexpr char hex[] = "0123456789abcdef";
                    escaped.push_back(hex[(byte >> 4U) & 0x0FU]);
                    escaped.push_back(hex[byte & 0x0FU]);
                } else {
                    escaped.push_back(static_cast<char>(byte));
                }
                break;
        }
    }
    return escaped;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Config config = parseArguments(argc, argv);
        generate(config);
        const Verification result = verify(config.output);
        if (result.bytes != config.bytes || result.newlines != config.records) {
            throw std::runtime_error("generated output failed byte/newline verification");
        }

        std::cout << "{\"schema\":\"loglens-benchmark-generator/v1\","
                     "\"output\":\""
                  << jsonEscape(config.output) << "\",\"bytes\":" << result.bytes
                  << ",\"records\":" << config.records << ",\"newlines\":"
                  << result.newlines << ",\"verified_bytes\":" << result.bytes
                  << ",\"verified_newlines\":" << result.newlines << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}
