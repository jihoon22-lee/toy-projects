#include "persistence_validation.hpp"

#include "loglens/filter_expr.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string_view>

namespace loglens::detail {

namespace {

bool utf8Lead(unsigned char lead, std::size_t& width, std::uint32_t& codepoint,
              std::uint32_t& minimum) {
    if (lead < 0x80U) {
        width = 1;
        codepoint = lead;
        minimum = 0;
        return true;
    }
    if (lead >= 0xC2U && lead <= 0xDFU) {
        width = 2;
        codepoint = lead & 0x1FU;
        minimum = 0x80U;
        return true;
    }
    if (lead >= 0xE0U && lead <= 0xEFU) {
        width = 3;
        codepoint = lead & 0x0FU;
        minimum = 0x800U;
        return true;
    }
    if (lead >= 0xF0U && lead <= 0xF4U) {
        width = 4;
        codepoint = lead & 0x07U;
        minimum = 0x10000U;
        return true;
    }
    return false;
}

bool validUtf8Sequence(std::string_view text, std::size_t position,
                       std::size_t& width) {
    const unsigned char lead = static_cast<unsigned char>(text[position]);
    std::uint32_t codepoint = 0;
    std::uint32_t minimum = 0;
    if (!utf8Lead(lead, width, codepoint, minimum)
        || width > text.size() - position) {
        return false;
    }
    for (std::size_t index = 1; index < width; ++index) {
        const unsigned char continuation =
            static_cast<unsigned char>(text[position + index]);
        if ((continuation & 0xC0U) != 0x80U) {
            return false;
        }
        codepoint = (codepoint << 6U) | (continuation & 0x3FU);
    }
    return width == 1
           || (codepoint >= minimum && codepoint <= 0x10FFFFU
               && !(codepoint >= 0xD800U && codepoint <= 0xDFFFU));
}

bool validUtf8(std::string_view text) {
    std::size_t position = 0;
    while (position < text.size()) {
        std::size_t width = 0;
        if (!validUtf8Sequence(text, position, width)) {
            return false;
        }
        position += width;
    }
    return true;
}

bool validLabel(const std::string& value, const char* label,
                PersistenceError& error) {
    if (value.empty()) {
        setPersistenceError(error, PersistenceErrorCode::InvalidValue,
                            std::string(label) + " must not be empty");
        return false;
    }
    if (value.size() > kMaxPersistedNameBytes) {
        setPersistenceError(error, PersistenceErrorCode::LimitExceeded,
                            std::string(label) + " exceeds 128-byte limit");
        return false;
    }
    if (!validUtf8(value)) {
        setPersistenceError(error, PersistenceErrorCode::InvalidValue,
                            std::string(label) + " must be valid UTF-8");
        return false;
    }
    for (unsigned char byte : value) {
        if (byte < 0x20U || byte == 0x7FU) {
            setPersistenceError(error, PersistenceErrorCode::InvalidValue,
                                std::string(label) + " contains a control byte");
            return false;
        }
    }
    return true;
}

bool validFormat(Format format) {
    return format == Format::Auto || format == Format::PlainIso
           || format == Format::Syslog || format == Format::JsonLine
           || format == Format::Raw;
}

template <typename Item, typename Validator>
bool validateAndSort(std::vector<Item>& items, PersistenceError& error,
                     Validator validator) {
    if (items.size() > kMaxPersistedItems) {
        setPersistenceError(error, PersistenceErrorCode::LimitExceeded,
                            "persistence item count exceeds 128-item limit");
        return false;
    }
    for (const Item& item : items) {
        if (!validator(item, error)) {
            return false;
        }
    }
    std::sort(items.begin(), items.end(),
              [](const Item& left, const Item& right) {
                  return left.name < right.name;
              });
    for (std::size_t index = 1; index < items.size(); ++index) {
        if (items[index - 1].name == items[index].name) {
            setPersistenceError(error, PersistenceErrorCode::DuplicateName,
                                "persistence item names must be unique");
            return false;
        }
    }
    return true;
}

} // namespace

void setPersistenceError(PersistenceError& error, PersistenceErrorCode code,
                         const std::string& message, std::size_t offset) {
    error.code = code;
    error.offset = offset;
    error.message = message;
}

bool validSourceProfile(const SourceProfile& profile, PersistenceError& error) {
    if (!validLabel(profile.name, "profile name", error)) {
        return false;
    }
    if (!validFormat(profile.format)) {
        setPersistenceError(error, PersistenceErrorCode::InvalidValue,
                            "profile format is not supported");
        return false;
    }
    if (std::string_view(multilinePolicyName(profile.multiline)) == "unknown") {
        setPersistenceError(error, PersistenceErrorCode::InvalidValue,
                            "profile multiline policy is not supported");
        return false;
    }
    if (profile.max_record_bytes == 0 || profile.max_record_bytes > kMaxRecordBytes) {
        setPersistenceError(error, PersistenceErrorCode::LimitExceeded,
                            "profile max_record_bytes is outside the supported range");
        return false;
    }
    return true;
}

bool validSavedQuery(const SavedQuery& query, PersistenceError& error) {
    if (!validLabel(query.name, "query name", error)) {
        return false;
    }
    if (query.expression.empty()) {
        setPersistenceError(error, PersistenceErrorCode::InvalidQuery,
                            "saved query expression must not be empty");
        return false;
    }
    if (query.expression.size() > kMaxFilterQueryBytes) {
        setPersistenceError(error, PersistenceErrorCode::LimitExceeded,
                            "saved query expression exceeds 4096-byte limit");
        return false;
    }
    if (!validUtf8(query.expression)) {
        setPersistenceError(error, PersistenceErrorCode::InvalidQuery,
                            "saved query expression must be valid UTF-8");
        return false;
    }
    ParseError parseError;
    if (!Filter::parse(query.expression, parseError)) {
        setPersistenceError(error, PersistenceErrorCode::InvalidQuery,
                            "saved query expression is invalid: " + parseError.message,
                            parseError.position);
        return false;
    }
    return true;
}

bool validateAndSortProfiles(std::vector<SourceProfile>& profiles,
                             PersistenceError& error) {
    return validateAndSort(profiles, error, validSourceProfile);
}

bool validateAndSortQueries(std::vector<SavedQuery>& queries,
                            PersistenceError& error) {
    return validateAndSort(queries, error, validSavedQuery);
}

} // namespace loglens::detail
