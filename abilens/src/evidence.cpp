#include <charconv>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

#include "evidence_internal.hpp"
namespace abilens::detail {
std::string serialize_evidence(const ElfReport& report) {
    std::ostringstream out;
    out << "{\"attributes_known\":" << json_bool(report.attributes_known)
        << ",\"loader_metadata_known\":" << json_bool(report.loader_metadata_known)
        << ",\"soname\":" << json_escape(report.soname)
        << ",\"interpreter\":" << json_escape(report.interpreter)
        << ",\"build_id\":" << json_escape(report.build_id)
        << ",\"dwarf_status\":" << json_escape(report.dwarf_status)
        << ",\"type_layouts\":" << json_string_array(report.type_layouts)
        << ",\"symbol_evidence\":[";
    bool first = true;
    for (const auto& symbol : report.symbol_evidence) {
        if (!first) out << ',';
        first = false;
        out << "{\"identity\":" << json_escape(symbol.identity) << ",\"size\":" << symbol.size
            << ",\"binding\":" << symbol.binding << ",\"visibility\":" << symbol.visibility
            << ",\"type\":" << symbol.type
            << ",\"default_version\":" << json_bool(symbol.default_version) << '}';
    }
    out << "],\"resolutions\":[";
    first = true;
    for (const auto& resolution : report.resolutions) {
        if (!first) out << ',';
        first = false;
        out << "{\"needed\":" << json_escape(resolution.needed)
            << ",\"status\":" << json_escape(resolution.status)
            << ",\"path\":" << json_escape(resolution.path)
            << ",\"searched\":" << json_string_array(resolution.searched) << '}';
    }
    out << "]}";
    return out.str();
}
namespace {
void object(const JsonValue& value, std::initializer_list<const char*> fields) {
    if (value.kind != JsonValue::Kind::Object || value.object.size() != fields.size())
        throw std::runtime_error("invalid evidence object");
    for (const auto* key : fields)
        if (!value.object.count(key)) throw std::runtime_error("missing evidence field");
}
const JsonValue& field(const JsonValue& value, const char* key, JsonValue::Kind kind) {
    const auto found = value.object.find(key);
    if (found == value.object.end() || found->second.kind != kind)
        throw std::runtime_error(std::string("invalid evidence field: ") + key);
    return found->second;
}
std::string string(const JsonValue& value, const char* key) {
    return field(value, key, JsonValue::Kind::String).scalar;
}
bool boolean(const JsonValue& value, const char* key) {
    return field(value, key, JsonValue::Kind::Boolean).boolean;
}
std::uint64_t number(const JsonValue& value, const char* key, std::uint64_t max) {
    const auto text = field(value, key, JsonValue::Kind::Number).scalar;
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size() || result > max)
        throw std::runtime_error("invalid evidence integer");
    return result;
}
std::vector<std::string> strings(const JsonValue& value, const char* key) {
    std::vector<std::string> result;
    for (const auto& item : field(value, key, JsonValue::Kind::Array).array) {
        if (item.kind != JsonValue::Kind::String)
            throw std::runtime_error("invalid evidence string array");
        result.push_back(item.scalar);
    }
    return result;
}
}  // namespace
void parse_evidence(const JsonValue& value, ElfReport& report) {
    object(value, {"attributes_known", "loader_metadata_known", "soname", "interpreter", "build_id",
                   "dwarf_status", "type_layouts", "symbol_evidence", "resolutions"});
    report.attributes_known = boolean(value, "attributes_known");
    report.loader_metadata_known = boolean(value, "loader_metadata_known");
    report.soname = string(value, "soname");
    report.interpreter = string(value, "interpreter");
    report.build_id = string(value, "build_id");
    report.dwarf_status = string(value, "dwarf_status");
    const std::set<std::string> states{"not-requested", "unavailable", "absent",       "complete",
                                       "partial",       "limited",     "input-changed"};
    if (!states.count(report.dwarf_status))
        throw std::runtime_error("invalid DWARF evidence status");
    report.type_layouts = strings(value, "type_layouts");
    std::set<std::string> identities;
    for (const auto& symbol : field(value, "symbol_evidence", JsonValue::Kind::Array).array) {
        object(symbol, {"identity", "size", "binding", "visibility", "type", "default_version"});
        SymbolEvidence parsed{string(symbol, "identity"),
                              number(symbol, "size", std::numeric_limits<std::uint64_t>::max()),
                              static_cast<unsigned>(number(symbol, "binding", 15)),
                              static_cast<unsigned>(number(symbol, "visibility", 3)),
                              static_cast<unsigned>(number(symbol, "type", 15)),
                              boolean(symbol, "default_version")};
        if (!identities.insert(parsed.identity).second)
            throw std::runtime_error("duplicate symbol evidence");
        report.symbol_evidence.push_back(std::move(parsed));
    }
    if (report.attributes_known &&
        (!report.symbols_known ||
         identities != std::set<std::string>(report.symbols.begin(), report.symbols.end())))
        throw std::runtime_error("symbol evidence identities do not match symbols");
    for (const auto& resolution : field(value, "resolutions", JsonValue::Kind::Array).array) {
        object(resolution, {"needed", "status", "path", "searched"});
        report.resolutions.push_back({string(resolution, "needed"), string(resolution, "status"),
                                      string(resolution, "path"), strings(resolution, "searched")});
    }
}
}  // namespace abilens::detail
