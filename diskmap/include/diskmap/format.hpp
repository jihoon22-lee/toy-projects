#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace diskmap {

// Renders a byte count using base-1024 units ("0 B" .. "1.5 MiB" .. "2.0 PiB").
std::string humanBytes(std::uint64_t bytes);

// Renders a 0..1 ratio as a one-decimal percentage string, e.g. "12.3%".
std::string formatPercent(double ratio);

// Shortens a string to at most maxLen bytes, inserting "..." near the
// middle when truncation is required. Never throws, never exceeds maxLen.
std::string truncateMiddle(const std::string& text, std::size_t maxLen);

} // namespace diskmap

#include <optional>
namespace diskmap {
// Decimal SI (MB) and binary IEC (MiB) units; fractional values must denote whole bytes.
std::optional<std::uint64_t> parseHumanBytes(const std::string& value);
}
