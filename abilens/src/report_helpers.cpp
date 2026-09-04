#include "report_internal.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace abilens {
namespace detail {

std::string trim(std::string value) {
    auto not_space = [](unsigned char character) { return std::isspace(character) == 0; };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::vector<std::uint64_t> version_parts(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    std::vector<std::uint64_t> parts;
    std::size_t begin = 0;
    while (begin < value.size()) {
        const std::size_t end = value.find('.', begin);
        const std::string token = value.substr(begin, end == std::string::npos ? end : end - begin);
        if (token.empty() || token.size() > 9U ||
            !std::all_of(token.begin(), token.end(), [](char character) {
                return std::isdigit(static_cast<unsigned char>(character)) != 0;
            })) {
            return {};
        }
        try {
            parts.push_back(std::stoull(token));
        } catch (const std::exception&) {
            return {};
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1U;
    }
    return parts;
}

bool version_less(const std::string& left, const std::string& right) {
    const auto left_parts = version_parts(left);
    const auto right_parts = version_parts(right);
    const std::size_t size = std::max(left_parts.size(), right_parts.size());
    for (std::size_t index = 0; index < size; ++index) {
        const std::uint64_t left_value = index < left_parts.size() ? left_parts[index] : 0U;
        const std::uint64_t right_value = index < right_parts.size() ? right_parts[index] : 0U;
        if (left_value != right_value) {
            return left_value < right_value;
        }
    }
    return false;
}

std::string maximum_version(const ElfReport& report, const std::string& namespace_name) {
    std::string maximum;
    for (const VersionRequirement& requirement : report.versions) {
        if (requirement.namespace_name != namespace_name) {
            continue;
        }
        if (maximum.empty() || version_less(maximum, requirement.version)) {
            maximum = requirement.version;
        }
    }
    return maximum;
}

bool is_absolute_path(const std::string& value) {
    return !value.empty() && value.front() == '/';
}

std::size_t valid_utf8_sequence_length(const std::string& value, std::size_t offset) {
    if (offset >= value.size()) {
        return 0U;
    }
    const auto byte = [&](std::size_t index) {
        return static_cast<unsigned char>(value[offset + index]);
    };
    const auto continuation = [&](std::size_t index) {
        return offset + index < value.size() && (byte(index) & 0xc0U) == 0x80U;
    };
    const unsigned char first = byte(0U);
    if (first <= 0x7fU) {
        return 1U;
    }
    if (first >= 0xc2U && first <= 0xdfU) {
        return continuation(1U) ? 2U : 0U;
    }
    if (first >= 0xe0U && first <= 0xefU) {
        if (!continuation(1U) || !continuation(2U)) {
            return 0U;
        }
        const unsigned char second = byte(1U);
        if ((first == 0xe0U && second < 0xa0U) ||
            (first == 0xedU && second > 0x9fU)) {
            return 0U;
        }
        return 3U;
    }
    if (first >= 0xf0U && first <= 0xf4U) {
        if (!continuation(1U) || !continuation(2U) || !continuation(3U)) {
            return 0U;
        }
        const unsigned char second = byte(1U);
        if ((first == 0xf0U && second < 0x90U) ||
            (first == 0xf4U && second > 0x8fU)) {
            return 0U;
        }
        return 4U;
    }
    return 0U;
}

std::string json_escape(const std::string& value) {
    std::ostringstream output;
    output << '"';
    for (std::size_t index = 0U; index < value.size(); ++index) {
        const unsigned char character = static_cast<unsigned char>(value[index]);
        switch (character) {
            case '"':
                output << "\\\"";
                break;
            case '\\':
                output << "\\\\";
                break;
            case '\b':
                output << "\\b";
                break;
            case '\f':
                output << "\\f";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                if (character < 0x20U) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<unsigned int>(character) << std::dec;
                } else if (character >= 0x80U) {
                    const std::size_t length = valid_utf8_sequence_length(value, index);
                    if (length == 0U) {
                        output << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                               << static_cast<unsigned int>(character) << std::dec;
                    } else {
                        output.write(value.data() + index, static_cast<std::streamsize>(length));
                        index += length - 1U;
                    }
                } else {
                    output << static_cast<char>(character);
                }
                break;
        }
    }
    output << '"';
    return output.str();
}

std::string json_bool(bool value) {
    return value ? "true" : "false";
}

std::string json_string_array(const std::vector<std::string>& values) {
    std::ostringstream output;
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0U) {
            output << ',';
        }
        output << json_escape(values[index]);
    }
    output << ']';
    return output.str();
}

std::vector<std::string> sorted_strings(const std::vector<std::string>& values) {
    std::vector<std::string> result = values;
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}


}  // namespace detail
}  // namespace abilens
