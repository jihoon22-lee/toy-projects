#include "persistence_writer.hpp"

#include <string_view>

namespace loglens::detail {

namespace {

bool appendNamedEscape(std::string& output, unsigned char byte) {
    switch (byte) {
        case '"': output += "\\\""; return true;
        case '\\': output += "\\\\"; return true;
        case '\b': output += "\\b"; return true;
        case '\f': output += "\\f"; return true;
        case '\n': output += "\\n"; return true;
        case '\r': output += "\\r"; return true;
        case '\t': output += "\\t"; return true;
        default: return false;
    }
}

void appendJsonString(std::string& output, std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    output.push_back('"');
    for (const unsigned char byte : value) {
        if (appendNamedEscape(output, byte)) {
            continue;
        }
        if (byte < 0x20U) {
            output += "\\u00";
            output.push_back(kHex[byte >> 4U]);
            output.push_back(kHex[byte & 0x0FU]);
        } else {
            output.push_back(static_cast<char>(byte));
        }
    }
    output.push_back('"');
}

} // namespace

std::string serializeSourceProfiles(const std::vector<SourceProfile>& profiles) {
    std::string output = "{\"schema\":";
    appendJsonString(output, sourceProfileSchemaName());
    output += ",\"profiles\":[";
    for (std::size_t index = 0; index < profiles.size(); ++index) {
        if (index != 0) {
            output.push_back(',');
        }
        const SourceProfile& profile = profiles[index];
        output += "{\"name\":";
        appendJsonString(output, profile.name);
        output += ",\"format\":";
        appendJsonString(output, formatName(profile.format));
        output += ",\"multiline\":";
        appendJsonString(output, multilinePolicyName(profile.multiline));
        output += ",\"max_record_bytes\":";
        output += std::to_string(profile.max_record_bytes);
        output += "}";
    }
    output += "]}\n";
    return output;
}

std::string serializeSavedQueries(const std::vector<SavedQuery>& queries) {
    std::string output = "{\"schema\":";
    appendJsonString(output, savedQuerySchemaName());
    output += ",\"queries\":[";
    for (std::size_t index = 0; index < queries.size(); ++index) {
        if (index != 0) {
            output.push_back(',');
        }
        const SavedQuery& query = queries[index];
        output += "{\"name\":";
        appendJsonString(output, query.name);
        output += ",\"expression\":";
        appendJsonString(output, query.expression);
        output += "}";
    }
    output += "]}\n";
    return output;
}

} // namespace loglens::detail
