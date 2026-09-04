#include "abilens/elf.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
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

}  // namespace

HeaderCheck validate_elf_file(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return failure(InputStatus::Unreadable, "input is not a readable regular file");
    }
    const std::uintmax_t file_size_raw = std::filesystem::file_size(path, error);
    if (error) {
        return failure(InputStatus::Unreadable, "could not read input size: " + error.message());
    }
    if (file_size_raw > std::numeric_limits<std::uint64_t>::max()) {
        return failure(InputStatus::Unsupported, "input is larger than the supported address range");
    }
    const std::uint64_t file_size = static_cast<std::uint64_t>(file_size_raw);
    if (file_size < 4U) {
        return failure(InputStatus::NonElf, "input is shorter than the ELF magic");
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return failure(InputStatus::Unreadable, "could not open input");
    }
    const std::size_t header_bytes = static_cast<std::size_t>(std::min<std::uint64_t>(file_size, 64U));
    std::vector<unsigned char> bytes(header_bytes);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return failure(InputStatus::Unreadable, "could not read the ELF header");
    }
    if (bytes[0] != 0x7fU || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F') {
        return failure(InputStatus::NonElf, "input does not begin with the ELF magic");
    }
    if (file_size < 16U) {
        return failure(InputStatus::Corrupt, "ELF file is shorter than the identification header");
    }
    if (bytes[4] != kElfClass32 && bytes[4] != kElfClass64) {
        return failure(InputStatus::Unsupported, "ELF class is not ELF32 or ELF64");
    }
    if (bytes[5] != kLittleEndian && bytes[5] != kBigEndian) {
        return failure(InputStatus::Unsupported, "ELF byte order is not little or big endian");
    }
    if (bytes[6] != 1U) {
        return failure(InputStatus::Corrupt, "ELF identification version is not current");
    }

    const bool is_64 = bytes[4] == kElfClass64;
    const bool little = bytes[5] == kLittleEndian;
    const std::uint64_t minimum_header_size = is_64 ? 64U : 52U;
    if (file_size < minimum_header_size || bytes.size() < minimum_header_size) {
        return failure(InputStatus::Corrupt, "ELF file is shorter than its declared header class");
    }

    const std::size_t type_offset = 16U;
    const std::size_t machine_offset = 18U;
    const std::size_t version_offset = 20U;
    const std::uint32_t version = read_u32(bytes, version_offset, little);
    if (version != 1U) {
        return failure(InputStatus::Corrupt, "ELF header version is not current");
    }

    const std::uint16_t raw_type = read_u16(bytes, type_offset, little);
    const std::uint16_t raw_machine = read_u16(bytes, machine_offset, little);
    const std::uint64_t phoff =
        is_64 ? read_u64(bytes, 32U, little) : static_cast<std::uint64_t>(read_u32(bytes, 28U, little));
    const std::uint64_t shoff =
        is_64 ? read_u64(bytes, 40U, little) : static_cast<std::uint64_t>(read_u32(bytes, 32U, little));
    const std::uint16_t ehsize = read_u16(bytes, is_64 ? 52U : 40U, little);
    const std::uint16_t phentsize = read_u16(bytes, is_64 ? 54U : 42U, little);
    const std::uint16_t phnum = read_u16(bytes, is_64 ? 56U : 44U, little);
    const std::uint16_t shentsize = read_u16(bytes, is_64 ? 58U : 46U, little);
    const std::uint16_t shnum = read_u16(bytes, is_64 ? 60U : 48U, little);

    if (ehsize != minimum_header_size) {
        return failure(InputStatus::Corrupt, "ELF header size does not match its class");
    }
    if (phnum == kExtendedCount || shnum == kExtendedCount) {
        return failure(InputStatus::Unsupported, "extended ELF header counts are not supported");
    }
    const std::uint64_t expected_phentsize = is_64 ? 56U : 32U;
    const std::uint64_t expected_shentsize = is_64 ? 64U : 40U;
    if (phnum != 0U && (phentsize < expected_phentsize ||
                        !range_inside(phoff, phnum, phentsize, file_size))) {
        return failure(InputStatus::Corrupt, "ELF program header table is outside the file");
    }
    if (shnum != 0U && (shentsize < expected_shentsize ||
                        !range_inside(shoff, shnum, shentsize, file_size))) {
        return failure(InputStatus::Corrupt, "ELF section header table is outside the file");
    }

    // Only the PT_DYNAMIC tag is read here.  No segment contents are mapped
    // or interpreted; readelf remains responsible for the dynamic evidence.
    bool has_dynamic = false;
    if (phnum != 0U) {
        stream.clear();
        for (std::uint16_t index = 0; index < phnum; ++index) {
            const std::uint64_t entry = phoff + static_cast<std::uint64_t>(index) * phentsize;
            if (entry > file_size || entry + 4U > file_size) {
                return failure(InputStatus::Corrupt, "ELF program header entry is truncated");
            }
            stream.seekg(static_cast<std::streamoff>(entry), std::ios::beg);
            std::array<unsigned char, 4> type_bytes{};
            stream.read(reinterpret_cast<char*>(type_bytes.data()), 4);
            if (stream.gcount() != 4) {
                return failure(InputStatus::Unreadable, "could not read an ELF program header entry");
            }
            const std::vector<unsigned char> type_vector(type_bytes.begin(), type_bytes.end());
            if (read_u32(type_vector, 0U, little) == kPtDynamic) {
                has_dynamic = true;
            }
        }
    }

    HeaderCheck result;
    result.status = InputStatus::Valid;
    result.header.elf_class = is_64 ? "ELF64" : "ELF32";
    result.header.endian = little ? "little-endian" : "big-endian";
    result.header.type = type_name(raw_type);
    result.header.machine = machine_name(raw_machine);
    result.header.raw_type = raw_type;
    result.header.raw_machine = raw_machine;
    result.header.has_dynamic = has_dynamic;
    result.header.has_program_headers = phnum != 0U;
    result.header.has_section_headers = shnum != 0U;
    return result;
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
