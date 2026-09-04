#include "duplicates_internal.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace diskmap {
namespace detail {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

std::uint32_t rotateRight(std::uint32_t value, unsigned shift) {
    return (value >> shift) | (value << (32U - shift));
}

std::uint32_t loadWord(const unsigned char* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U)
           | (static_cast<std::uint32_t>(bytes[1]) << 16U)
           | (static_cast<std::uint32_t>(bytes[2]) << 8U)
           | static_cast<std::uint32_t>(bytes[3]);
}

void storeWord(std::string& output, std::uint32_t value) {
    static constexpr char digits[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4) {
        output += digits[(value >> static_cast<unsigned>(shift)) & 0x0fU];
    }
}

void appendLength(std::string& output, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        output += static_cast<char>(value >> static_cast<unsigned>(shift));
    }
}

} // namespace

Sha256::Sha256()
    : state_{0x6a09e667U,
             0xbb67ae85U,
             0x3c6ef372U,
             0xa54ff53aU,
             0x510e527fU,
             0x9b05688cU,
             0x1f83d9abU,
             0x5be0cd19U} {}

void Sha256::transform(const unsigned char* block) {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t index = 0; index < 16; ++index) {
        schedule[index] = loadWord(block + index * 4);
    }
    for (std::size_t index = 16; index < schedule.size(); ++index) {
        const std::uint32_t first = schedule[index - 15];
        const std::uint32_t second = schedule[index - 2];
        const std::uint32_t smallFirst = rotateRight(first, 7U)
                                         ^ rotateRight(first, 18U) ^ (first >> 3U);
        const std::uint32_t smallSecond = rotateRight(second, 17U)
                                          ^ rotateRight(second, 19U) ^ (second >> 10U);
        schedule[index] = schedule[index - 16] + smallFirst
                          + schedule[index - 7] + smallSecond;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];
    for (std::size_t index = 0; index < schedule.size(); ++index) {
        const std::uint32_t bigE = rotateRight(e, 6U) ^ rotateRight(e, 11U)
                                   ^ rotateRight(e, 25U);
        const std::uint32_t choose = (e & f) ^ ((~e) & g);
        const std::uint32_t bigA = rotateRight(a, 2U) ^ rotateRight(a, 13U)
                                   ^ rotateRight(a, 22U);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t first = h + bigE + choose + kRoundConstants[index]
                                    + schedule[index];
        const std::uint32_t second = bigA + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const char* data, std::size_t size) {
    total_bytes_ += static_cast<std::uint64_t>(size);
    while (size > 0) {
        const std::size_t available = buffer_.size() - buffered_;
        const std::size_t copied = size < available ? size : available;
        std::copy(data, data + copied, buffer_.begin() + buffered_);
        buffered_ += copied;
        data += copied;
        size -= copied;
        if (buffered_ == buffer_.size()) {
            transform(buffer_.data());
            buffered_ = 0;
        }
    }
}

std::string Sha256::finalHex() {
    const std::uint64_t bitCount = total_bytes_ * 8U;
    const unsigned char marker = 0x80U;
    update(reinterpret_cast<const char*>(&marker), 1);
    const unsigned char zero = 0;
    while (buffered_ != 56) {
        update(reinterpret_cast<const char*>(&zero), 1);
    }
    std::string length;
    length.reserve(8);
    appendLength(length, bitCount);
    update(length.data(), length.size());

    std::string result;
    result.reserve(64);
    for (const std::uint32_t word : state_) {
        storeWord(result, word);
    }
    return result;
}

std::string partialFingerprint(std::uint64_t size,
                               const std::string& first,
                               const std::string& last) {
    Sha256 digest;
    static constexpr char marker[] = "diskmap-partial-v1";
    digest.update(marker, sizeof(marker) - 1);
    std::string encoded;
    encoded.reserve(8);
    appendLength(encoded, size);
    digest.update(encoded.data(), encoded.size());
    encoded.clear();
    appendLength(encoded, static_cast<std::uint64_t>(first.size()));
    digest.update(encoded.data(), encoded.size());
    digest.update(first.data(), first.size());
    encoded.clear();
    appendLength(encoded, static_cast<std::uint64_t>(last.size()));
    digest.update(encoded.data(), encoded.size());
    digest.update(last.data(), last.size());
    return digest.finalHex();
}

} // namespace detail

std::string sha256Hex(std::string_view bytes) {
    detail::Sha256 digest;
    digest.update(bytes.data(), bytes.size());
    return digest.finalHex();
}

} // namespace diskmap
