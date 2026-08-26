// Tests for diskmap::humanBytes / formatPercent / truncateMiddle
// (src/core/format.hpp / format.cpp).

#include "assert.hpp"
#include "../src/core/format.hpp"

#include <cstdint>
#include <limits>
#include <string>

using diskmap::formatPercent;
using diskmap::humanBytes;
using diskmap::truncateMiddle;

int main() {
    // --- humanBytes: every unit threshold, base-1024 ---
    CHECK_EQ(humanBytes(0), std::string("0 B"));
    CHECK_EQ(humanBytes(1), std::string("1 B"));
    CHECK_EQ(humanBytes(500), std::string("500 B"));
    CHECK_EQ(humanBytes(1023), std::string("1023 B")); // just under the KB boundary

    CHECK_EQ(humanBytes(1024), std::string("1.0 KB"));      // exact boundary
    CHECK_EQ(humanBytes(1536), std::string("1.5 KB"));      // 1024 * 1.5
    CHECK_EQ(humanBytes(1024ull * 1024), std::string("1.0 MB"));
    CHECK_EQ(humanBytes(1500000), std::string("1.4 MB"));   // non-exact rounding
    CHECK_EQ(humanBytes(1024ull * 1024 * 1024), std::string("1.0 GB"));
    CHECK_EQ(humanBytes(1024ull * 1024 * 1024 * 1024), std::string("1.0 TB"));
    CHECK_EQ(humanBytes(2ull * 1024 * 1024 * 1024 * 1024 * 1024), std::string("2.0 PB"));

    // PB is the top unit: even far beyond it, the unit stays "PB" (the loop
    // stops advancing once it reaches the last table entry).
    CHECK_EQ(humanBytes(std::numeric_limits<std::uint64_t>::max()), std::string("16384.0 PB"));

    // --- formatPercent: 0..1 ratio -> one-decimal percentage ---
    CHECK_EQ(formatPercent(0.0), std::string("0.0%"));
    CHECK_EQ(formatPercent(1.0), std::string("100.0%"));
    CHECK_EQ(formatPercent(0.1234), std::string("12.3%"));
    CHECK_EQ(formatPercent(0.999), std::string("99.9%"));
    CHECK_EQ(formatPercent(0.5), std::string("50.0%"));

    // --- truncateMiddle: never exceeds maxLen ---
    // no truncation needed: text at or under the limit is returned as-is.
    CHECK_EQ(truncateMiddle("short", 10), std::string("short"));
    CHECK_EQ(truncateMiddle("exact", 5), std::string("exact"));
    CHECK_EQ(truncateMiddle("", 0), std::string(""));

    // maxLen <= ellipsis length (3): falls back to a plain prefix cut.
    CHECK_EQ(truncateMiddle("abcd", 0), std::string(""));
    CHECK_EQ(truncateMiddle("abcd", 1), std::string("a"));
    CHECK_EQ(truncateMiddle("abcd", 2), std::string("ab"));
    CHECK_EQ(truncateMiddle("abcd", 3), std::string("abc"));

    // maxLen just above ellipsis length: suffix can shrink to 0 chars.
    CHECK_EQ(truncateMiddle("abcdef", 4), std::string("a..."));

    // normal middle truncation, both odd and even remaining budgets.
    CHECK_EQ(truncateMiddle("abcdefghij", 7), std::string("ab...ij"));
    {
        const std::string result = truncateMiddle("this_is_a_very_long_filename.txt", 20);
        CHECK_EQ(result.size(), static_cast<std::size_t>(20));
        CHECK(result.find("...") != std::string::npos);
    }

    // never exceeds maxLen, for a range of budgets on a fixed long string.
    const std::string longText = "the_quick_brown_fox_jumps_over_the_lazy_dog";
    for (std::size_t maxLen = 0; maxLen <= 12; ++maxLen) {
        const std::string result = truncateMiddle(longText, maxLen);
        CHECK(result.size() <= maxLen);
    }

    return testSummary();
}
