#include "persistence_writer.hpp"
#include "loglens/triage.hpp"

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

std::string serializeSession(const SessionState& state) {
    std::string output = "{\"schema\":";
    appendJsonString(output, sessionSchemaName());
    if (!state.name.empty()) {
        output += ",\"name\":";
        appendJsonString(output, state.name);
    }
    output += ",\"source\":{\"path\":";
    appendJsonString(output, state.source_path);
    output += ",\"format\":";
    appendJsonString(output, formatName(state.format));
    output += ",\"multiline\":";
    appendJsonString(output, multilinePolicyName(state.multiline));
    output += ",\"max_record_bytes\":";
    output += std::to_string(state.max_record_bytes);
    if (!state.format_plugin.empty()) {
        output += ",\"format_plugin\":";
        appendJsonString(output, state.format_plugin);
    }
    output += ",\"identity\":";
    appendJsonString(output, state.source_identity);
    output += ",\"modified\":";
    appendJsonString(output, state.source_modified);
    output += ",\"fingerprint\":";
    appendJsonString(output, state.source_fingerprint);
    output += ",\"plugin_fingerprint\":";
    appendJsonString(output, state.plugin_fingerprint);
    output += ",\"fingerprint_bytes\":" + std::to_string(state.fingerprint_bytes);
    output += ",\"size\":" + std::to_string(state.source_size);
    output += ",\"generation\":" + std::to_string(state.source_generation);
    output += "}";
    output += ",\"view\":{\"search\":";
    appendJsonString(output, state.search);
    output += ",\"whole_file_search\":";
    appendJsonString(output, state.whole_file_search);
    output += ",\"investigation_tab\":" + std::to_string(state.investigation_tab);
    output += ",\"settings_open\":";
    output += state.settings_open ? "true" : "false";
    output += ",\"follow\":";
    output += state.follow ? "true" : "false";
    output += ",\"tail_mode\":";
    output += state.tail_mode ? "true" : "false";
    output += ",\"tail_records\":" + std::to_string(state.tail_records);
    for (const auto &property : {std::make_pair("selected", state.selected_window),
                                 std::make_pair("baseline", state.baseline_window),
                                 std::make_pair("comparison", state.comparison_window)}) {
        output += ",";
        appendJsonString(output, property.first);
        output += ":";
        if (!property.second)
            output += "null";
        else
            output += "{\"begin_ms\":" + std::to_string(property.second->begin_ms) +
                      ",\"end_ms\":" + std::to_string(property.second->end_ms) + "}";
    }
    output += ",\"layout\":";
    appendJsonString(output, state.layout);
    output += ",\"geometry\":";
    appendJsonString(output, state.geometry);
    output += ",\"table_header\":";
    appendJsonString(output, state.table_header);
    output += "},\"triage\":" + serializeTriageState(state.triage);
    if (!state.filter.empty()) {
        output += ",\"filter\":";
        appendJsonString(output, state.filter);
    }
    if (!state.level.empty()) {
        output += ",\"level\":";
        appendJsonString(output, state.level);
    }
    output += "}\n";
    return output;
}

} // namespace loglens::detail
