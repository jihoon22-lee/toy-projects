#include "json_line_parser.hpp"

#include <algorithm>
#include <iterator>
#include <limits>

namespace loglens::detail {

namespace {

struct DateTimeParts {
    unsigned year = 0;
    unsigned month = 0;
    unsigned day = 0;
    unsigned hour = 0;
    unsigned minute = 0;
    unsigned second = 0;
    unsigned milliseconds = 0;
    std::size_t cursor = 19;
    int offset_minutes = 0;
};

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool fixedDigits(std::string_view text, std::size_t offset, std::size_t count,
                 unsigned& value) {
    if (offset > text.size() || count > text.size() - offset) {
        return false;
    }
    value = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const char digit = text[offset + index];
        if (!isDigit(digit)) {
            return false;
        }
        value = value * 10U + static_cast<unsigned>(digit - '0');
    }
    return true;
}

bool leapYear(unsigned year) {
    return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

unsigned daysInMonth(unsigned year, unsigned month) {
    static const unsigned kDays[] = {0, 31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};
    if (month == 2U && leapYear(year)) {
        return 29U;
    }
    return month < std::size(kDays) ? kDays[month] : 0U;
}

// Howard Hinnant's civil-date conversion expressed with only integer
// arithmetic.  It avoids timegm()/mktime(), whose timezone rules and
// availability differ between POSIX, Qt's supported Windows toolchains, and
// other C++17 platforms.
std::int64_t daysFromCivil(const DateTimeParts& parts) {
    int adjustedYear = static_cast<int>(parts.year);
    adjustedYear -= parts.month <= 2U ? 1 : 0;
    const int era = (adjustedYear >= 0 ? adjustedYear : adjustedYear - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(adjustedYear - era * 400);
    const unsigned monthPrime =
        parts.month > 2U ? parts.month - 3U : parts.month + 9U;
    const unsigned dayOfYear =
        (153U * monthPrime + 2U) / 5U + parts.day - 1U;
    const unsigned dayOfEra = yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U
                              + dayOfYear;
    return static_cast<std::int64_t>(era) * 146097
           + static_cast<std::int64_t>(dayOfEra) - 719468;
}

bool civilToUnixSeconds(const DateTimeParts& parts, std::int64_t& output) {
    const std::int64_t days = daysFromCivil(parts);
    constexpr std::int64_t kSecondsPerDay = 86400;
    constexpr std::int64_t kMaxWholeDays =
        (std::numeric_limits<std::int64_t>::max() - (kSecondsPerDay - 1))
        / kSecondsPerDay;
    if (days < 0 || days > kMaxWholeDays) {
        return false;
    }
    const std::int64_t daySeconds =
        static_cast<std::int64_t>(parts.hour) * 3600
        + static_cast<std::int64_t>(parts.minute) * 60 + parts.second;
    output = days * kSecondsPerDay + daySeconds;
    return true;
}

} // namespace

namespace {

bool parseCalendarPart(std::string_view text, DateTimeParts& parts) {
    return fixedDigits(text, 0, 4, parts.year) && text.size() > 4
           && text[4] == '-' && fixedDigits(text, 5, 2, parts.month)
           && text.size() > 7 && text[7] == '-'
           && fixedDigits(text, 8, 2, parts.day);
}

bool parseClockPart(std::string_view text, DateTimeParts& parts) {
    return text.size() > 10 && (text[10] == 'T' || text[10] == 't')
           && fixedDigits(text, 11, 2, parts.hour) && text.size() > 13
           && text[13] == ':' && fixedDigits(text, 14, 2, parts.minute)
           && text.size() > 16 && text[16] == ':'
           && fixedDigits(text, 17, 2, parts.second);
}

bool parseDateTime(std::string_view text, DateTimeParts& parts) {
    return parseCalendarPart(text, parts) && parseClockPart(text, parts);
}

bool validDateTime(const DateTimeParts& parts) {
    if (parts.year < 1970U || parts.month < 1U || parts.month > 12U) {
        return false;
    }
    if (parts.day < 1U || parts.day > daysInMonth(parts.year, parts.month)) {
        return false;
    }
    return parts.hour <= 23U && parts.minute <= 59U && parts.second <= 59U;
}

bool parseFraction(std::string_view text, DateTimeParts& parts,
                   TimestampResult& result) {
    if (parts.cursor == text.size() || text[parts.cursor] != '.') {
        return true;
    }
    const std::size_t fractionStart = ++parts.cursor;
    while (parts.cursor < text.size() && isDigit(text[parts.cursor])) {
        ++parts.cursor;
    }
    const std::size_t fractionDigits = parts.cursor - fractionStart;
    if (fractionDigits == 0U || fractionDigits > 9U) {
        result.message = "timestamp fraction must contain one to nine digits";
        return false;
    }
    for (std::size_t index = 0; index < std::min<std::size_t>(3, fractionDigits);
         ++index) {
        parts.milliseconds =
            parts.milliseconds * 10U
            + static_cast<unsigned>(text[fractionStart + index] - '0');
    }
    if (fractionDigits == 1U) {
        parts.milliseconds *= 100U;
    } else if (fractionDigits == 2U) {
        parts.milliseconds *= 10U;
    }
    return true;
}

bool parseSignedOffset(std::string_view text, DateTimeParts& parts,
                       TimestampResult& result) {
    const bool negative = text[parts.cursor] == '-';
    ++parts.cursor;
    unsigned offsetHour = 0;
    unsigned offsetMinute = 0;
    if (!fixedDigits(text, parts.cursor, 2, offsetHour)) {
        result.message = "timestamp timezone hour is invalid";
        return false;
    }
    parts.cursor += 2;
    if (parts.cursor < text.size() && text[parts.cursor] == ':') {
        ++parts.cursor;
    }
    if (!fixedDigits(text, parts.cursor, 2, offsetMinute)) {
        result.message = "timestamp timezone minute is invalid";
        return false;
    }
    parts.cursor += 2;
    if (offsetHour > 23U || offsetMinute > 59U) {
        result.message = "timestamp timezone is outside the supported range";
        return false;
    }
    parts.offset_minutes = static_cast<int>(offsetHour * 60U + offsetMinute);
    if (negative) {
        parts.offset_minutes = -parts.offset_minutes;
    }
    return true;
}

bool parseTimezone(std::string_view text, DateTimeParts& parts,
                   TimestampResult& result) {
    if (parts.cursor >= text.size()) {
        result.message = "timestamp timezone is missing";
        return false;
    }
    if (text[parts.cursor] == 'Z' || text[parts.cursor] == 'z') {
        ++parts.cursor;
    } else if (text[parts.cursor] == '+' || text[parts.cursor] == '-') {
        if (!parseSignedOffset(text, parts, result)) {
            return false;
        }
    } else {
        result.message = "timestamp timezone must be Z or a signed offset";
        return false;
    }
    if (parts.cursor != text.size()) {
        result.message = "timestamp contains trailing timezone data";
        return false;
    }
    return true;
}

} // namespace

TimestampResult parseIsoTimestamp(std::string_view text) {
    TimestampResult result;
    result.present = !text.empty();
    if (text.empty()) {
        result.message = "timestamp field is empty";
        return result;
    }
    if (text.size() < 20U) {
        result.message = "timestamp must include a timezone";
        return result;
    }

    DateTimeParts parts;
    if (!parseDateTime(text, parts)) {
        result.message = "timestamp has an invalid date or time shape";
        return result;
    }
    if (!validDateTime(parts)) {
        result.message = "timestamp has an out-of-range date or time";
        return result;
    }

    if (!parseFraction(text, parts, result)) {
        return result;
    }
    if (!parseTimezone(text, parts, result)) {
        result.code = ParseDiagnosticCode::InvalidTimestampOffset;
        return result;
    }

    std::int64_t base = 0;
    if (!civilToUnixSeconds(parts, base)) {
        result.message = "timestamp cannot be represented by the supported epoch range";
        return result;
    }
    const std::int64_t adjusted =
        base - static_cast<std::int64_t>(parts.offset_minutes) * 60;
    if (adjusted < 0
        || static_cast<std::uint64_t>(adjusted)
               > (std::numeric_limits<std::uint64_t>::max() - parts.milliseconds)
                     / 1000U) {
        result.message = "timestamp is outside the supported epoch range";
        return result;
    }
    result.value =
        static_cast<std::uint64_t>(adjusted) * 1000U + parts.milliseconds;
    result.valid = true;
    return result;
}

} // namespace loglens::detail
