#include "abilens/elf.hpp"

#include "input_internal.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace abilens {
namespace {

constexpr std::uint8_t kElfClass32 = 1;
constexpr std::uint8_t kElfClass64 = 2;
constexpr std::uint8_t kLittleEndian = 1;
constexpr std::uint8_t kBigEndian = 2;
constexpr std::uint32_t kPtDynamic = 2;
constexpr std::uint16_t kExtendedCount = 0xffffU;

bool range_inside(std::uint64_t offset,
                  std::uint64_t count,
                  std::uint64_t element_size,
                  std::uint64_t file_size) {
    if (element_size != 0 && count > std::numeric_limits<std::uint64_t>::max() / element_size) {
        return false;
    }
    const std::uint64_t total = count * element_size;
    return offset <= file_size && total <= file_size - offset;
}

std::uint16_t read_u16(const std::vector<unsigned char>& bytes,
                       std::size_t offset,
                       bool little) {
    if (offset + 2U > bytes.size()) {
        return 0;
    }
    if (little) {
        const std::uint32_t value = static_cast<std::uint32_t>(bytes[offset]) |
                                    (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U);
        return static_cast<std::uint16_t>(value);
    }
    const std::uint32_t value = (static_cast<std::uint32_t>(bytes[offset]) << 8U) |
                                static_cast<std::uint32_t>(bytes[offset + 1U]);
    return static_cast<std::uint16_t>(value);
}

std::uint32_t read_u32(const std::vector<unsigned char>& bytes,
                       std::size_t offset,
                       bool little) {
    if (offset + 4U > bytes.size()) {
        return 0;
    }
    if (little) {
        return static_cast<std::uint32_t>(bytes[offset]) |
               (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
               (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
               (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
    }
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

std::uint64_t read_u64(const std::vector<unsigned char>& bytes,
                       std::size_t offset,
                       bool little) {
    if (offset + 8U > bytes.size()) {
        return 0;
    }
    std::uint64_t value = 0;
    if (little) {
        for (unsigned int index = 0; index < 8U; ++index) {
            value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
        }
        return value;
    }
    for (unsigned int index = 0; index < 8U; ++index) {
        value = (value << 8U) | static_cast<std::uint64_t>(bytes[offset + index]);
    }
    return value;
}

std::string type_name(std::uint16_t value) {
    switch (value) {
        case 0:
            return "ET_NONE (No file type)";
        case 1:
            return "ET_REL (Relocatable file)";
        case 2:
            return "ET_EXEC (Executable file)";
        case 3:
            return "ET_DYN (Shared object file)";
        case 4:
            return "ET_CORE (Core file)";
        default:
            return "ET_UNKNOWN (" + std::to_string(value) + ")";
    }
}

std::string machine_name(std::uint16_t value) {
    switch (value) {
        case 3:
            return "Intel 80386";
        case 40:
            return "ARM";
        case 62:
            return "Advanced Micro Devices X86-64";
        case 183:
            return "AArch64";
        case 243:
            return "RISC-V";
        default:
            return "Machine (" + std::to_string(value) + ")";
    }
}

HeaderCheck failure(InputStatus status, std::string message) {
    HeaderCheck result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

HeaderCheck success() {
    HeaderCheck result;
    result.status = InputStatus::Valid;
    return result;
}

struct HeaderLayout {
    bool is_64 = false;
    bool little = false;
    std::uint16_t raw_type = 0U;
    std::uint16_t raw_machine = 0U;
    std::uint64_t phoff = 0U;
    std::uint64_t shoff = 0U;
    std::uint16_t phentsize = 0U;
    std::uint16_t phnum = 0U;
    std::uint16_t shentsize = 0U;
    std::uint16_t shnum = 0U;
};

HeaderCheck read_header(const detail::OpenInput& input,
                        std::vector<unsigned char>& bytes) {
    const std::uint64_t file_size = input.size();
    if (file_size < 4U) {
        return failure(InputStatus::NonElf, "input is shorter than the ELF magic");
    }
    const auto header_bytes = static_cast<std::size_t>(
        std::min<std::uint64_t>(file_size, 64U));
    bytes.resize(header_bytes);
    if (!input.read_exact(0U, bytes.data(), bytes.size())) {
        return failure(InputStatus::Unreadable, "could not read the ELF header");
    }
    return success();
}

HeaderCheck validate_identification(const std::vector<unsigned char>& bytes,
                                    std::uint64_t file_size) {
    const bool elf_magic = bytes[0] == 0x7fU && bytes[1] == 'E' &&
                           bytes[2] == 'L' && bytes[3] == 'F';
    if (!elf_magic) {
        return failure(InputStatus::NonElf, "input does not begin with the ELF magic");
    }
    if (file_size < 16U) {
        return failure(InputStatus::Corrupt,
                       "ELF file is shorter than the identification header");
    }
    if (bytes[4] != kElfClass32 && bytes[4] != kElfClass64) {
        return failure(InputStatus::Unsupported, "ELF class is not ELF32 or ELF64");
    }
    if (bytes[5] != kLittleEndian && bytes[5] != kBigEndian) {
        return failure(InputStatus::Unsupported,
                       "ELF byte order is not little or big endian");
    }
    if (bytes[6] != 1U) {
        return failure(InputStatus::Corrupt,
                       "ELF identification version is not current");
    }
    return success();
}

HeaderCheck parse_layout(const std::vector<unsigned char>& bytes,
                         std::uint64_t file_size,
                         HeaderLayout& layout) {
    layout.is_64 = bytes[4] == kElfClass64;
    layout.little = bytes[5] == kLittleEndian;
    const std::uint64_t minimum_header_size = layout.is_64 ? 64U : 52U;
    if (file_size < minimum_header_size || bytes.size() < minimum_header_size) {
        return failure(InputStatus::Corrupt,
                       "ELF file is shorter than its declared header class");
    }
    if (read_u32(bytes, 20U, layout.little) != 1U) {
        return failure(InputStatus::Corrupt, "ELF header version is not current");
    }
    layout.raw_type = read_u16(bytes, 16U, layout.little);
    layout.raw_machine = read_u16(bytes, 18U, layout.little);
    layout.phoff = layout.is_64 ? read_u64(bytes, 32U, layout.little)
                                : read_u32(bytes, 28U, layout.little);
    layout.shoff = layout.is_64 ? read_u64(bytes, 40U, layout.little)
                                : read_u32(bytes, 32U, layout.little);
    const std::size_t word_offset = layout.is_64 ? 52U : 40U;
    const std::uint16_t ehsize = read_u16(bytes, word_offset, layout.little);
    layout.phentsize = read_u16(bytes, word_offset + 2U, layout.little);
    layout.phnum = read_u16(bytes, word_offset + 4U, layout.little);
    layout.shentsize = read_u16(bytes, word_offset + 6U, layout.little);
    layout.shnum = read_u16(bytes, word_offset + 8U, layout.little);
    if (ehsize != minimum_header_size) {
        return failure(InputStatus::Corrupt, "ELF header size does not match its class");
    }
    if (layout.phnum == kExtendedCount || layout.shnum == kExtendedCount) {
        return failure(InputStatus::Unsupported,
                       "extended ELF header counts are not supported");
    }
    return success();
}

HeaderCheck validate_tables(const HeaderLayout& layout, std::uint64_t file_size) {
    const std::uint64_t expected_phentsize = layout.is_64 ? 56U : 32U;
    const std::uint64_t expected_shentsize = layout.is_64 ? 64U : 40U;
    const bool invalid_program_table =
        layout.phnum != 0U &&
        (layout.phentsize < expected_phentsize ||
         !range_inside(layout.phoff, layout.phnum, layout.phentsize, file_size));
    if (invalid_program_table) {
        return failure(InputStatus::Corrupt,
                       "ELF program header table is outside the file");
    }
    const bool invalid_section_table =
        layout.shnum != 0U &&
        (layout.shentsize < expected_shentsize ||
         !range_inside(layout.shoff, layout.shnum, layout.shentsize, file_size));
    if (invalid_section_table) {
        return failure(InputStatus::Corrupt,
                       "ELF section header table is outside the file");
    }
    return success();
}

HeaderCheck scan_dynamic_segment(const detail::OpenInput& input,
                                 const HeaderLayout& layout,
                                 std::uint64_t file_size, bool& has_dynamic) {
    has_dynamic = false;
    for (std::uint16_t index = 0; index < layout.phnum; ++index) {
        const std::uint64_t entry = layout.phoff +
                                    static_cast<std::uint64_t>(index) * layout.phentsize;
        if (entry > file_size || file_size - entry < 4U) {
            return failure(InputStatus::Corrupt,
                           "ELF program header entry is truncated");
        }
        std::array<unsigned char, 4> type_bytes{};
        if (!input.read_exact(entry, type_bytes.data(), type_bytes.size())) {
            return failure(InputStatus::Unreadable,
                           "could not read an ELF program header entry");
        }
        const std::vector<unsigned char> type_vector(type_bytes.begin(), type_bytes.end());
        has_dynamic = has_dynamic ||
                      read_u32(type_vector, 0U, layout.little) == kPtDynamic;
    }
    return success();
}

HeaderCheck make_header(const HeaderLayout& layout, bool has_dynamic) {
    HeaderCheck result = success();
    result.header.elf_class = layout.is_64 ? "ELF64" : "ELF32";
    result.header.endian = layout.little ? "little-endian" : "big-endian";
    result.header.type = type_name(layout.raw_type);
    result.header.machine = machine_name(layout.raw_machine);
    result.header.raw_type = layout.raw_type;
    result.header.raw_machine = layout.raw_machine;
    result.header.has_dynamic = has_dynamic;
    result.header.has_program_headers = layout.phnum != 0U;
    result.header.has_section_headers = layout.shnum != 0U;
    return result;
}

}  // namespace

HeaderCheck detail::validate_elf_input(const detail::OpenInput& input) {
    if (!input.opened()) {
        return failure(input.error_status(), input.error_message());
    }
    const std::uint64_t file_size = input.size();
    std::vector<unsigned char> bytes;
    HeaderCheck step = read_header(input, bytes);
    if (step.status != InputStatus::Valid) return step;
    step = validate_identification(bytes, file_size);
    if (step.status != InputStatus::Valid) return step;
    HeaderLayout layout;
    step = parse_layout(bytes, file_size, layout);
    if (step.status != InputStatus::Valid) return step;
    step = validate_tables(layout, file_size);
    if (step.status != InputStatus::Valid) return step;
    bool has_dynamic = false;
    step = scan_dynamic_segment(input, layout, file_size, has_dynamic);
    if (step.status != InputStatus::Valid) return step;
    if (!input.unchanged()) {
        return failure(InputStatus::Unreadable,
                       "input changed while reading ELF metadata");
    }
    return make_header(layout, has_dynamic);
}

HeaderCheck validate_elf_file(const std::filesystem::path& path) {
    const detail::OpenInput input(path);
    return detail::validate_elf_input(input);
}

const char* input_status_name(InputStatus status) noexcept {
    switch (status) {
        case InputStatus::Valid:
            return "valid";
        case InputStatus::NonElf:
            return "non-elf";
        case InputStatus::Corrupt:
            return "corrupt";
        case InputStatus::Unsupported:
            return "unsupported";
        case InputStatus::Unreadable:
            return "unreadable";
        case InputStatus::ToolError:
            return "tool-error";
    }
    return "unknown";
}

}  // namespace abilens
