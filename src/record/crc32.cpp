#include "tempo/crc32.hpp"

#include <array>

namespace tempo {
namespace {

constexpr std::array<std::uint32_t, 256> make_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t i = 0; i < 256; ++i) {
    std::uint32_t value = i;
    for (int bit = 0; bit < 8; ++bit) {
      value = (value & 1u) != 0 ? (value >> 1) ^ 0xEDB88320u : value >> 1;
    }
    table[i] = value;
  }
  return table;
}

constexpr std::array<std::uint32_t, 256> kTable = make_table();

}  // namespace

std::uint32_t crc32_update(std::uint32_t crc, const void* data, std::size_t size) noexcept {
  crc ^= 0xFFFFFFFFu;
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    crc = kTable[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

std::uint32_t crc32(std::string_view bytes, std::uint32_t seed) noexcept {
  return crc32_update(seed, bytes.data(), bytes.size());
}

}  // namespace tempo
