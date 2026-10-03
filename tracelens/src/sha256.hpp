#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace tracelens {
class Sha256 {
  std::array<uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::array<unsigned char, 64> buf{};
  uint64_t bytes = 0;
  size_t used = 0;
  void block(const unsigned char *);

public:
  void add(const char *, size_t);
  std::string finish();
};
} // namespace tracelens
