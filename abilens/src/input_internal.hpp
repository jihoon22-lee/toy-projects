#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "abilens/model.hpp"

namespace abilens {
namespace detail {

struct InputIdentity {
    std::uint64_t device = 0U;
    std::uint64_t inode = 0U;
    std::uint64_t mode = 0U;
    std::uint64_t size = 0U;
    std::int64_t modified_seconds = 0;
    std::int64_t modified_nanoseconds = 0;
    std::int64_t changed_seconds = 0;
    std::int64_t changed_nanoseconds = 0;
};

class OpenInput {
public:
    explicit OpenInput(const std::filesystem::path& path);
    ~OpenInput();

    OpenInput(const OpenInput&) = delete;
    OpenInput& operator=(const OpenInput&) = delete;
    OpenInput(OpenInput&& other) noexcept;
    OpenInput& operator=(OpenInput&& other) noexcept;

    bool opened() const noexcept;
    int descriptor() const noexcept;
    std::uint64_t size() const noexcept;
    InputStatus error_status() const noexcept;
    const std::string& error_message() const noexcept;
    std::string descriptor_path() const;
    bool read_exact(std::uint64_t offset, unsigned char* destination,
                    std::size_t bytes) const noexcept;
    bool unchanged() const noexcept;

private:
    std::filesystem::path path_;
    int descriptor_ = -1;
    InputIdentity identity_;
    InputStatus error_status_ = InputStatus::Unreadable;
    std::string error_message_;
};

HeaderCheck validate_elf_input(const OpenInput& input);
ReadelfEvidence run_readelf_input(const OpenInput& input);

}  // namespace detail
}  // namespace abilens
