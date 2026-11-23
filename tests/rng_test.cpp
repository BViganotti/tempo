#include "test_util.hpp"
#include "tempo/rng.hpp"

using namespace tempo;

TEST(known_sequence_seed_one) {
  Rng rng(1);
  CHECK_EQ(rng.next_u64(), 0xb3f2af6d0fc710c5ull);
  CHECK_EQ(rng.next_u64(), 0x853b559647364ceall);
  CHECK_EQ(rng.next_u64(), 0x92f89756082a4514ull);
  CHECK_EQ(rng.next_u64(), 0x642e1c7bc266a3a7ull);
  CHECK_EQ(rng.next_u64(), 0xb27a48e29a233673ull);
}

TEST(reseed_reproduces_stream) {
  Rng a(1234567);
  Rng b(55);
  Rng fresh(1234567);
  for (int i = 0; i < 17; ++i) {
    (void)b.next_u64();
  }
  b.reseed(1234567);
  for (int i = 0; i < 100; ++i) {
    const std::uint64_t value = a.next_u64();
    CHECK_EQ(value, b.next_u64());
    CHECK_EQ(value, fresh.next_u64());
  }
}

TEST(seed_zero_is_not_degenerate) {
  Rng a(0);
  Rng b(0);
  std::uint64_t previous = 0;
  int distinct = 0;
  for (int i = 0; i < 64; ++i) {
    const std::uint64_t value = a.next_u64();
    CHECK_EQ(value, b.next_u64());
    if (value != previous) {
      ++distinct;
    }
    previous = value;
  }
  CHECK(distinct > 60);
}

TEST(bounded_stays_in_range) {
  Rng rng(99);
  for (std::uint64_t bound = 1; bound <= 257; ++bound) {
    for (int i = 0; i < 50; ++i) {
      CHECK(rng.bounded(bound) < bound);
    }
  }
  CHECK_EQ(rng.bounded(0), 0u);
  CHECK_EQ(rng.bounded(1), 0u);
}

TEST(bounded_covers_small_range) {
  Rng rng(2024);
  int seen[4] = {0, 0, 0, 0};
  for (int i = 0; i < 1000; ++i) {
    ++seen[rng.bounded(4)];
  }
  for (int count : seen) {
    CHECK(count > 150);
  }
}

TEST(double_is_unit_interval) {
  Rng rng(7);
  for (int i = 0; i < 1000; ++i) {
    const double value = rng.next_double();
    CHECK(value >= 0.0);
    CHECK(value < 1.0);
  }
}
