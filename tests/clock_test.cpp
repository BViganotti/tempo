#include "test_util.hpp"
#include "tempo/clock.hpp"

using namespace tempo;

TEST(starts_positive) {
  VirtualClock clock;
  CHECK(clock.now_ns() > 0);
  VirtualClock zero(0);
  CHECK(zero.now_ns() > 0);
}

TEST(advances_forward_only) {
  VirtualClock clock(1000);
  CHECK(clock.advance_to(2000));
  CHECK_EQ(clock.now_ns(), 2000u);
  CHECK(!clock.advance_to(1500));
  CHECK_EQ(clock.now_ns(), 2000u);
  CHECK(clock.advance_to(2000));
}

TEST(advance_by_saturates) {
  VirtualClock clock(1);
  clock.advance_by(500);
  CHECK_EQ(clock.now_ns(), 501u);
  clock.advance_by(UINT64_MAX);
  CHECK_EQ(clock.now_ns(), UINT64_MAX);
}

TEST(formatting_is_readable) {
  CHECK_EQ(format_ns(500), "500ns");
  CHECK_EQ(format_ns(1500), "1.500us");
  CHECK_EQ(format_ns(2'500'000), "2.500ms");
  CHECK_EQ(format_ns(2'000'000'000), "2.000s");
}
