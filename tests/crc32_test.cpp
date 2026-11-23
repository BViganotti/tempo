#include "test_util.hpp"
#include "tempo/crc32.hpp"

using namespace tempo;

TEST(crc32_known_vector) {
  CHECK_EQ(crc32("123456789"), 0xCBF43926u);
  CHECK_EQ(crc32(""), 0u);
  CHECK_EQ(crc32("a"), 0xE8B7BE43u);
}

TEST(crc32_incremental_matches_one_shot) {
  const std::string data = "tempo deterministic replay";
  std::uint32_t crc = 0;
  crc = crc32_update(crc, data.data(), 5);
  crc = crc32_update(crc, data.data() + 5, data.size() - 5);
  CHECK_EQ(crc32(data), crc);
}

TEST(crc32_detects_bit_flip) {
  std::string data = "the quick brown fox";
  const std::uint32_t before = crc32(data);
  data[3] ^= 0x01;
  CHECK(crc32(data) != before);
}
