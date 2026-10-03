#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace abilens::detail {

inline bool range_inside(std::uint64_t offset,
                         std::uint64_t count,
                         std::uint64_t element_size,
                         std::uint64_t file_size) {
    if (element_size != 0 &&
        count > std::numeric_limits<std::uint64_t>::max() / element_size) {
        return false;
    }
    const std::uint64_t total = count * element_size;
    return offset <= file_size && total <= file_size - offset;
}

inline std::uint16_t read_u16(const std::vector<unsigned char>& bytes,
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

inline std::uint32_t read_u32(const std::vector<unsigned char>& bytes,
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

inline std::uint64_t read_u64(const std::vector<unsigned char>& bytes,
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

}  // namespace abilens::detail
