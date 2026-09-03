#include "json_string_parser.hpp"

#include <cstdint>

namespace loglens::detail {

namespace {

void setDiagnostic(JsonStringResult& result, ParseDiagnosticCode code,
                  const std::string& field, std::size_t offset,
                  const char* message) {
    result.valid = false;
    result.diagnostic = ParseDiagnostic{code, field, offset, message};
}

bool appendByte(JsonStringResult& result, char value, std::size_t maxBytes,
                std::size_t offset, const std::string& field) {
    if (result.value.size() >= maxBytes) {
        if (!result.limited) {
            result.limited = true;
            result.diagnostic = ParseDiagnostic{
                ParseDiagnosticCode::LimitExceeded, field, offset,
                "JSON string exceeds the safety limit"};
        }
        return true;
    }
    result.value.push_back(value);
    return true;
}

bool readHexQuad(std::string_view input, std::size_t& position, std::uint32_t& value) {
    if (position > input.size() || input.size() - position < 4U) {
        return false;
    }
    value = 0;
    for (std::size_t index = 0; index < 4U; ++index) {
        const char digit = input[position++];
        unsigned nibble = 0;
        if (digit >= '0' && digit <= '9') {
            nibble = static_cast<unsigned>(digit - '0');
        } else if (digit >= 'a' && digit <= 'f') {
            nibble = static_cast<unsigned>(digit - 'a') + 10U;
        } else if (digit >= 'A' && digit <= 'F') {
            nibble = static_cast<unsigned>(digit - 'A') + 10U;
        } else {
            return false;
        }
        value = (value << 4U) | nibble;
    }
    return true;
}

bool readUnicodeScalar(JsonStringResult& result, std::string_view input,
                       std::size_t& position, std::size_t offset,
                       const std::string& field, std::uint32_t& codepoint) {
    if (!readHexQuad(input, position, codepoint)) {
        setDiagnostic(result, ParseDiagnosticCode::InvalidUnicode, field, offset,
                      "JSON unicode escape must contain four hex digits");
        return false;
    }
    if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU) {
        setDiagnostic(result, ParseDiagnosticCode::InvalidUnicode, field, offset,
                      "JSON string contains an unpaired low surrogate");
        return false;
    }
    if (codepoint < 0xD800U || codepoint > 0xDBFFU) {
        return true;
    }
    if (position > input.size() || input.size() - position < 6U
        || input[position] != '\\' || input[position + 1] != 'u') {
        setDiagnostic(result, ParseDiagnosticCode::InvalidUnicode, field, offset,
                      "JSON high surrogate is missing its low surrogate");
        return false;
    }
    position += 2;
    std::uint32_t low = 0;
    if (!readHexQuad(input, position, low) || low < 0xDC00U || low > 0xDFFFU) {
        setDiagnostic(result, ParseDiagnosticCode::InvalidUnicode, field, offset,
                      "JSON surrogate pair contains an invalid low surrogate");
        return false;
    }
    codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) + (low - 0xDC00U);
    return true;
}

bool appendCodepoint(JsonStringResult& result, std::uint32_t codepoint,
                     std::size_t maxBytes, std::size_t offset,
                     const std::string& field) {
    if (codepoint <= 0x7FU) {
        return appendByte(result, static_cast<char>(codepoint), maxBytes, offset, field);
    }
    if (codepoint <= 0x7FFU) {
        appendByte(result, static_cast<char>(0xC0U | (codepoint >> 6U)), maxBytes, offset,
                    field);
        return appendByte(result, static_cast<char>(0x80U | (codepoint & 0x3FU)),
                          maxBytes, offset, field);
    }
    if (codepoint <= 0xFFFFU) {
        appendByte(result, static_cast<char>(0xE0U | (codepoint >> 12U)), maxBytes, offset,
                    field);
        appendByte(result, static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)),
                    maxBytes, offset, field);
        return appendByte(result, static_cast<char>(0x80U | (codepoint & 0x3FU)),
                          maxBytes, offset, field);
    }
    appendByte(result, static_cast<char>(0xF0U | (codepoint >> 18U)), maxBytes, offset,
                field);
    appendByte(result, static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)), maxBytes,
                offset, field);
    appendByte(result, static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)), maxBytes,
                offset, field);
    return appendByte(result, static_cast<char>(0x80U | (codepoint & 0x3FU)), maxBytes,
                      offset, field);
}

bool appendUnicode(JsonStringResult& result, std::string_view input,
                   std::size_t& position, std::size_t maxBytes, std::size_t offset,
                   const std::string& field) {
    std::uint32_t codepoint = 0;
    if (!readUnicodeScalar(result, input, position, offset, field, codepoint)) {
        return false;
    }
    return appendCodepoint(result, codepoint, maxBytes, offset, field);
}

bool consumeUtf8(std::string_view input, std::size_t& position, unsigned char first,
                 std::size_t offset, const std::string& field,
                 JsonStringResult& result) {
    std::size_t continuationCount = 0;
    std::uint32_t codepoint = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xC2U && first <= 0xDFU) {
        continuationCount = 1;
        codepoint = first & 0x1FU;
        minimum = 0x80U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
        continuationCount = 2;
        codepoint = first & 0x0FU;
        minimum = 0x800U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
        continuationCount = 3;
        codepoint = first & 0x07U;
        minimum = 0x10000U;
    } else {
        setDiagnostic(result, ParseDiagnosticCode::InvalidUnicode, field, offset,
                      "JSON string contains an invalid UTF-8 lead byte");
        return false;
    }
    if (input.size() - position < continuationCount) {
        setDiagnostic(result, ParseDiagnosticCode::InvalidUnicode, field, offset,
                      "JSON string contains a truncated UTF-8 sequence");
        return false;
    }
    for (std::size_t index = 0; index < continuationCount; ++index) {
        const unsigned char byte = static_cast<unsigned char>(input[position + index]);
        if ((byte & 0xC0U) != 0x80U) {
            setDiagnostic(result, ParseDiagnosticCode::InvalidUnicode, field, offset,
                          "JSON string contains an invalid UTF-8 continuation byte");
            return false;
        }
        codepoint = (codepoint << 6U) | (byte & 0x3FU);
    }
    if (codepoint < minimum || codepoint > 0x10FFFFU
        || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
        setDiagnostic(result, ParseDiagnosticCode::InvalidUnicode, field, offset,
                      "JSON string contains a non-scalar UTF-8 code point");
        return false;
    }
    // The caller appends the bytes with the real decoded-string bound; this
    // helper only validates and advances the sequence.
    position += continuationCount;
    return true;
}

char decodedSimpleEscape(char escaped) {
    switch (escaped) {
        case 'b': return '\b';
        case 'f': return '\f';
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        default: return '\0';
    }
}

bool parseEscape(JsonStringResult& result, std::string_view input,
                 std::size_t& position, std::size_t maxBytes, std::size_t offset,
                 const std::string& field) {
    if (position >= input.size()) {
        setDiagnostic(result, ParseDiagnosticCode::InvalidEscape, field, offset,
                      "JSON escape is truncated");
        return false;
    }
    const char escaped = input[position++];
    if (escaped == '"' || escaped == '\\' || escaped == '/') {
        appendByte(result, escaped, maxBytes, offset, field);
        return true;
    }
    if (escaped == 'u') {
        return appendUnicode(result, input, position, maxBytes, offset, field);
    }
    const char decoded = decodedSimpleEscape(escaped);
    if (decoded != '\0') {
        appendByte(result, decoded, maxBytes, offset, field);
        return true;
    }
    setDiagnostic(result, ParseDiagnosticCode::InvalidEscape, field, offset,
                  "JSON string contains an invalid escape");
    return false;
}

bool appendRawByte(JsonStringResult& result, std::string_view input,
                   std::size_t& position, unsigned char byte, std::size_t maxBytes,
                   std::size_t offset, const std::string& field) {
    if (byte < 0x80U) {
        appendByte(result, static_cast<char>(byte), maxBytes, offset, field);
        return true;
    }
    const std::size_t before = position;
    if (!consumeUtf8(input, position, byte, offset, field, result)) {
        return false;
    }
    appendByte(result, static_cast<char>(byte), maxBytes, offset, field);
    for (std::size_t continuation = before; continuation < position; ++continuation) {
        appendByte(result, input[continuation], maxBytes, offset, field);
    }
    return true;
}

} // namespace

JsonStringResult parseJsonString(std::string_view input, std::size_t& position,
                                 std::size_t maxBytes, std::size_t start,
                                 const std::string& field) {
    JsonStringResult result;
    if (position >= input.size() || input[position] != '"') {
        setDiagnostic(result, ParseDiagnosticCode::InvalidJson, field, position,
                      "JSON string is missing its opening quote");
        return result;
    }
    ++position;
    while (position < input.size()) {
        const std::size_t offset = position;
        const unsigned char byte = static_cast<unsigned char>(input[position++]);
        if (byte == '"') {
            result.valid = true;
            return result;
        }
        if (byte == '\\') {
            if (!parseEscape(result, input, position, maxBytes, offset, field)) {
                return result;
            }
            continue;
        }
        if (byte < 0x20U) {
            setDiagnostic(result, ParseDiagnosticCode::InvalidJson, field, offset,
                          "JSON string contains an unescaped control byte");
            return result;
        }
        if (!appendRawByte(result, input, position, byte, maxBytes, offset, field)) {
            return result;
        }
    }
    setDiagnostic(result, ParseDiagnosticCode::InvalidJson, field, start,
                  "JSON string is not terminated");
    return result;
}

} // namespace loglens::detail
