#include "abilens/inspect.hpp"

#include "elf_internal.hpp"
#include "input_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace abilens {
namespace {

using detail::range_inside;
using detail::read_u16;
using detail::read_u32;
using detail::read_u64;

constexpr std::uint8_t kElfClass64 = 2;
constexpr std::uint8_t kLittleEndian = 1;
constexpr std::uint32_t kPtLoad = 1;
constexpr std::uint32_t kPtDynamic = 2;
constexpr std::uint32_t kShtSymtab = 2;
constexpr std::uint64_t kDtNull = 0;
constexpr std::uint64_t kDtNeeded = 1;
constexpr std::uint64_t kDtStrtab = 5;
constexpr std::uint64_t kDtStrsz = 10;
constexpr std::uint64_t kDtRpath = 15;
constexpr std::uint64_t kDtRunpath = 29;
constexpr std::uint64_t kDtVerneed = 0x6ffffffeU;
constexpr std::uint64_t kDtVerneednum = 0x6fffffffU;
constexpr std::uint64_t kMaxDynamicEntries = 4096;
constexpr std::uint64_t kMaxVerneedRecords = 4096;
constexpr std::uint64_t kMaxVernauxRecords = 16384;
constexpr std::uint64_t kMaxStringTable = 64U * 1024U * 1024U;

void append_unique(std::vector<std::string>& values, const std::string& value) {
    if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0U, prefix.size(), prefix) == 0;
}

bool valid_version(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    bool digit = false;
    for (const char character : value) {
        if (character == '.') {
            if (!digit) {
                return false;
            }
            digit = false;
        } else if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
            digit = true;
        } else {
            return false;
        }
    }
    return digit;
}

void append_version(std::vector<VersionRequirement>& values,
                    const std::string& token,
                    const std::string& library) {
    const std::array<std::string, 3U> namespaces{"GLIBCXX_", "GLIBC_", "CXXABI_"};
    for (const std::string& prefix : namespaces) {
        if (!starts_with(token, prefix)) {
            continue;
        }
        const std::string version = token.substr(prefix.size());
        if (!valid_version(version)) {
            return;
        }
        const std::string namespace_name = prefix.substr(0U, prefix.size() - 1U);
        const VersionRequirement candidate{namespace_name, version, library};
        const auto duplicate = std::find_if(
            values.begin(), values.end(), [&](const VersionRequirement& existing) {
                return existing.namespace_name == candidate.namespace_name &&
                       existing.version == candidate.version && existing.library == candidate.library;
            });
        if (duplicate == values.end()) {
            values.push_back(candidate);
        }
        return;
    }
}

void append_colon_separated(const std::string& value,
                            std::vector<std::string>& destination) {
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const std::size_t colon = value.find(':', begin);
        const std::string item = value.substr(
            begin, colon == std::string::npos ? std::string::npos : colon - begin);
        append_unique(destination, item);
        if (colon == std::string::npos) break;
        begin = colon + 1U;
    }
}

bool version_requirement_less(const VersionRequirement& left,
                              const VersionRequirement& right) {
    if (left.namespace_name != right.namespace_name) {
        return left.namespace_name < right.namespace_name;
    }
    if (left.version != right.version) return left.version < right.version;
    return left.library < right.library;
}

// A byte-addressable region translated from a PT_LOAD segment.
struct LoadSegment {
    std::uint64_t vaddr = 0;
    std::uint64_t offset = 0;
    std::uint64_t filesz = 0;
};

struct ElfView {
    const std::vector<unsigned char>& bytes;
    bool is_64 = false;
    bool little = true;
    std::string error;

    std::uint16_t u16(std::uint64_t offset) const {
        return read_u16(bytes, static_cast<std::size_t>(offset), little);
    }
    std::uint32_t u32(std::uint64_t offset) const {
        return read_u32(bytes, static_cast<std::size_t>(offset), little);
    }
    std::uint64_t u64(std::uint64_t offset) const {
        return read_u64(bytes, static_cast<std::size_t>(offset), little);
    }
    std::uint64_t xword(std::uint64_t offset) const {
        return is_64 ? u64(offset) : u32(offset);
    }
};

bool corrupt(ElfView& view, const char* message) {
    if (view.error.empty()) view.error = message;
    return false;
}

std::vector<LoadSegment> load_segments(const ElfView& view,
                                       std::uint64_t phoff,
                                       std::uint16_t phentsize,
                                       std::uint16_t phnum) {
    std::vector<LoadSegment> segments;
    for (std::uint16_t index = 0; index < phnum; ++index) {
        const std::uint64_t entry = phoff + static_cast<std::uint64_t>(index) * phentsize;
        if (view.u32(entry) != kPtLoad) continue;
        LoadSegment segment;
        if (view.is_64) {
            segment.offset = view.u64(entry + 8U);
            segment.vaddr = view.u64(entry + 16U);
            segment.filesz = view.u64(entry + 40U);
        } else {
            segment.offset = view.u32(entry + 4U);
            segment.vaddr = view.u32(entry + 8U);
            segment.filesz = view.u32(entry + 16U);
        }
        if (range_inside(segment.offset, 1U, segment.filesz, view.bytes.size())) {
            segments.push_back(segment);
        }
    }
    return segments;
}

// Translates a virtual address into a file offset using PT_LOAD segments.
std::optional<std::uint64_t> vaddr_offset(const std::vector<LoadSegment>& segments,
                                        std::uint64_t vaddr,
                                        std::uint64_t size) {
    for (const LoadSegment& segment : segments) {
        if (vaddr < segment.vaddr) continue;
        const std::uint64_t delta = vaddr - segment.vaddr;
        if (size <= segment.filesz && delta <= segment.filesz - size) {
            return segment.offset + delta;
        }
    }
    return std::nullopt;
}

// Reads a NUL-terminated string bounded inside a string-table region.
std::optional<std::string> bounded_string(const ElfView& view,
                                          std::uint64_t table_offset,
                                          std::uint64_t table_size,
                                          std::uint64_t index) {
    if (index >= table_size || table_offset + index >= view.bytes.size()) {
        return std::nullopt;
    }
    const std::size_t begin = static_cast<std::size_t>(table_offset + index);
    const std::size_t limit = static_cast<std::size_t>(
        std::min<std::uint64_t>(table_offset + table_size,
                                static_cast<std::uint64_t>(view.bytes.size())));
    const auto it = std::find(view.bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                              view.bytes.begin() + static_cast<std::ptrdiff_t>(limit),
                              static_cast<unsigned char>(0));
    if (it == view.bytes.begin() + static_cast<std::ptrdiff_t>(limit)) {
        return std::nullopt;
    }
    return std::string(view.bytes.begin() + static_cast<std::ptrdiff_t>(begin), it);
}

struct DynamicInfo {
    std::vector<std::uint64_t> needed;
    std::vector<std::uint64_t> rpath;
    std::vector<std::uint64_t> runpath;
    std::uint64_t strtab = 0;
    std::uint64_t strsz = 0;
    std::uint64_t verneed = 0;
    std::uint64_t verneednum = 0;
    bool present = false;
};

bool read_dynamic(ElfView& view,
                  std::uint64_t phoff,
                  std::uint16_t phentsize,
                  std::uint16_t phnum,
                  DynamicInfo& info) {
    const std::uint64_t entry_size = view.is_64 ? 16U : 8U;
    for (std::uint16_t index = 0; index < phnum; ++index) {
        const std::uint64_t entry = phoff + static_cast<std::uint64_t>(index) * phentsize;
        if (view.u32(entry) != kPtDynamic) continue;
        std::uint64_t offset = 0;
        std::uint64_t filesz = 0;
        if (view.is_64) {
            offset = view.u64(entry + 8U);
            filesz = view.u64(entry + 40U);
        } else {
            offset = view.u32(entry + 4U);
            filesz = view.u32(entry + 16U);
        }
        info.present = true;
        const std::uint64_t count = filesz / entry_size;
        if (count > kMaxDynamicEntries || !range_inside(offset, count, entry_size,
                                                        view.bytes.size())) {
            return corrupt(view,
                           "PT_DYNAMIC segment is outside the file or unreasonably large");
        }
        for (std::uint64_t i = 0; i < count; ++i) {
            const std::uint64_t dyn = offset + i * entry_size;
            const std::uint64_t tag = view.xword(dyn);
            const std::uint64_t value = view.xword(dyn + (view.is_64 ? 8U : 4U));
            if (tag == kDtNull) break;
            switch (tag) {
                case kDtNeeded: info.needed.push_back(value); break;
                case kDtRpath: info.rpath.push_back(value); break;
                case kDtRunpath: info.runpath.push_back(value); break;
                case kDtStrtab: info.strtab = value; break;
                case kDtStrsz: info.strsz = value; break;
                case kDtVerneed: info.verneed = value; break;
                case kDtVerneednum: info.verneednum = value; break;
                default: break;
            }
        }
    }
    return true;
}

bool read_verneed(ElfView& view,
                  const std::vector<LoadSegment>& segments,
                  const DynamicInfo& info,
                  std::uint64_t strtab_offset,
                  std::uint64_t strtab_size,
                  ElfReport& report) {
    if (info.verneed == 0 || info.verneednum == 0) return true;
    if (info.verneednum > kMaxVerneedRecords) {
        return corrupt(view,
                       "version requirement count exceeds the inspection bound");
    }
    const auto table = vaddr_offset(segments, info.verneed, 1U);
    if (!table.has_value()) {
        return corrupt(view,
                       "version requirement table is not mapped by PT_LOAD");
    }
    std::uint64_t record = *table;
    std::uint64_t seen = 0;
    while (record != 0 && seen < info.verneednum) {
        if (!range_inside(record, 1U, 16U, view.bytes.size())) {
            return corrupt(view,
                           "version requirement record is outside the file");
        }
        const std::uint16_t vn_cnt = view.u16(record + 2U);
        const std::uint64_t vn_file = view.u32(record + 4U);
        const std::uint64_t vn_aux = view.u32(record + 8U);
        const std::uint64_t vn_next = view.u32(record + 12U);
        const auto library =
            bounded_string(view, strtab_offset, strtab_size, vn_file);
        const std::string library_name = library.value_or("");
        std::uint64_t aux = record + vn_aux;
        std::uint64_t aux_seen = 0;
        while (aux != 0 && aux_seen < vn_cnt && aux_seen < kMaxVernauxRecords) {
            if (!range_inside(aux, 1U, 16U, view.bytes.size())) {
                return corrupt(view,
                               "version requirement aux record is outside the file");
            }
            const std::uint64_t vna_name = view.u32(aux + 8U);
            const std::uint64_t vna_next = view.u32(aux + 12U);
            const auto name = bounded_string(view, strtab_offset, strtab_size, vna_name);
            if (name.has_value()) {
                append_version(report.versions, *name, library_name);
            }
            if (vna_next == 0) break;
            aux += vna_next;
            ++aux_seen;
        }
        if (aux_seen >= kMaxVernauxRecords) {
            return corrupt(view,
                           "version requirement aux chain exceeds the inspection bound");
        }
        ++seen;
        if (vn_next == 0) break;
        record += vn_next;
    }
    return true;
}

bool has_symtab_section(const ElfView& view,
                        std::uint64_t shoff,
                        std::uint16_t shentsize,
                        std::uint16_t shnum) {
    const std::uint64_t type_offset = 4U;
    for (std::uint16_t index = 0; index < shnum; ++index) {
        const std::uint64_t entry = shoff + static_cast<std::uint64_t>(index) * shentsize;
        if (!range_inside(entry, 1U, 8U, view.bytes.size())) {
            return false;
        }
        if (view.u32(entry + type_offset) == kShtSymtab) return true;
    }
    return false;
}

void append_stripped_evidence(const ElfView& view,
                            std::uint64_t shoff,
                            std::uint16_t shentsize,
                            std::uint16_t shnum,
                            ElfReport& report) {
    report.stripped_known = shnum != 0U;
    report.stripped = report.stripped_known &&
                      !has_symtab_section(view, shoff, shentsize, shnum);
    if (!report.stripped_known) {
        report.diagnostics.push_back("stripped: unknown (section headers were unavailable)");
    } else if (report.stripped) {
        report.diagnostics.push_back("stripped: no .symtab section");
    } else {
        report.diagnostics.push_back("not-stripped: .symtab section present");
    }
}

ElfReport failed_report(const ElfHeader& header, InputStatus status,
                        const std::string& message) {
    ElfReport report;
    report.status = status;
    report.header = header;
    report.tool.name = "abilens";
    report.tool.version = kAbiLensVersion;
    report.message = message;
    return report;
}

}  // namespace

ElfReport inspect_elf_buffer(const std::vector<unsigned char>& file,
                             const ElfHeader& header) {
    ElfView view{file, file.size() > 4U && file[4] == kElfClass64,
                 file.size() > 5U && file[5] == kLittleEndian, {}};
    if (file.size() < 52U) {
        return failed_report(header, InputStatus::ToolError,
                             "input is too short to inspect");
    }

    const std::uint64_t phoff = view.is_64 ? view.u64(32U) : view.u32(28U);
    const std::uint64_t shoff = view.is_64 ? view.u64(40U) : view.u32(32U);
    const std::size_t counts_offset = view.is_64 ? 52U : 40U;
    const std::uint16_t phentsize = view.u16(counts_offset + 2U);
    const std::uint16_t phnum = view.u16(counts_offset + 4U);
    const std::uint16_t shentsize = view.u16(counts_offset + 6U);
    const std::uint16_t shnum = view.u16(counts_offset + 8U);

    ElfReport report;
    report.status = InputStatus::Valid;
    report.header = header;
    report.tool.name = "abilens";
    report.tool.version = kAbiLensVersion;

    const std::vector<LoadSegment> segments = load_segments(view, phoff, phentsize, phnum);
    DynamicInfo dynamic;
    if (!read_dynamic(view, phoff, phentsize, phnum, dynamic) ||
        !view.error.empty()) {
        return failed_report(header, InputStatus::Corrupt, view.error);
    }

    if (dynamic.present) {
        if (dynamic.strsz > kMaxStringTable) {
            return failed_report(header, InputStatus::Corrupt,
                                 "dynamic string table exceeds the inspection bound");
        }
        const auto strtab = dynamic.strtab == 0
                                ? std::optional<std::uint64_t>{}
                                : vaddr_offset(segments, dynamic.strtab,
                                               std::max<std::uint64_t>(dynamic.strsz, 1U));
        if (dynamic.strtab != 0 && !strtab.has_value()) {
            return failed_report(header, InputStatus::Corrupt,
                                 "dynamic string table is not mapped by PT_LOAD");
        }
        const std::uint64_t strtab_offset = strtab.value_or(0U);
        const std::uint64_t strtab_size = strtab.has_value() ? dynamic.strsz : 0U;
        for (const std::uint64_t index : dynamic.needed) {
            const auto name = bounded_string(view, strtab_offset, strtab_size, index);
            if (!name.has_value()) {
                return failed_report(header, InputStatus::Corrupt,
                                     "DT_NEEDED entry is outside the dynamic string table");
            }
            append_unique(report.needed, *name);
        }
        for (const std::uint64_t index : dynamic.rpath) {
            const auto value = bounded_string(view, strtab_offset, strtab_size, index);
            if (value.has_value()) append_colon_separated(*value, report.rpath);
        }
        for (const std::uint64_t index : dynamic.runpath) {
            const auto value = bounded_string(view, strtab_offset, strtab_size, index);
            if (value.has_value()) append_colon_separated(*value, report.runpath);
        }
        if (!read_verneed(view, segments, dynamic, strtab_offset, strtab_size, report)) {
            return failed_report(header, InputStatus::Corrupt, view.error);
        }
    }

    std::sort(report.needed.begin(), report.needed.end());
    std::sort(report.rpath.begin(), report.rpath.end());
    std::sort(report.runpath.begin(), report.runpath.end());
    std::sort(report.versions.begin(), report.versions.end(), version_requirement_less);

    if (!header.has_dynamic) {
        report.diagnostics.push_back("static-or-non-dynamic: no PT_DYNAMIC program header");
    }
    append_stripped_evidence(view, shoff, shentsize, shnum, report);
    report.message = "ELF evidence verified";
    return report;
}

namespace detail {

// Reads the whole open file through its descriptor, re-checks the identity so
// a swapped input cannot silently produce evidence, and inspects the bytes.
ElfReport inspect_elf_input(const detail::OpenInput& input, const ElfHeader& header) {
    if (!input.opened()) {
        return failed_report(header, InputStatus::Unreadable, input.error_message());
    }
    if (input.size() > kInspectMaxFileSize) {
        return failed_report(header, InputStatus::ToolError,
                             "input exceeds the inspection size bound");
    }
    std::vector<unsigned char> file(static_cast<std::size_t>(input.size()));
    if (!file.empty() && !input.read_exact(0U, file.data(), file.size())) {
        return failed_report(header, InputStatus::Unreadable,
                             "could not read input contents");
    }
    if (!input.unchanged()) {
        return failed_report(header, InputStatus::ToolError,
                             "input changed while evidence was collected");
    }
    return inspect_elf_buffer(file, header);
}

}  // namespace detail
}  // namespace abilens
