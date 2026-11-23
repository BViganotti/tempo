#include <cstdint>
#include <string>
#include <vector>

#include "test_util.hpp"
#include "tempo/diff.hpp"
#include "tempo/record.hpp"

using namespace tempo;

namespace {

struct Rng {
  std::uint64_t state;
  explicit Rng(std::uint64_t seed) : state(seed ? seed : 0x9E3779B97F4A7C15ull) {}
  std::uint64_t next() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }
  std::size_t pick(std::size_t limit) { return limit == 0 ? 0 : next() % limit; }
};

Record random_record(Rng& rng) {
  Record record;
  record.meta.scenario = "diff";
  const std::size_t count = rng.pick(30);
  for (std::size_t i = 0; i < count; ++i) {
    Event event;
    event.type = static_cast<EventType>(rng.pick(27));
    event.task = static_cast<TaskId>(rng.pick(5));
    event.a = rng.next() % 10;
    event.b = rng.next() % 10;
    event.s1 = "s" + std::to_string(rng.next() % 4);
    record.events.push_back(std::move(event));
  }
  record.recompute_hash();
  return record;
}

std::size_t manual_prefix(const Record& a, const Record& b) {
  std::size_t index = 0;
  while (index < a.events.size() && index < b.events.size() &&
         a.events[index] == b.events[index]) {
    ++index;
  }
  return index;
}

}  // namespace

TEST(diff_common_prefix_and_first_event_match_a_manual_scan) {
  Rng rng(0xD1FF);
  for (int iteration = 0; iteration < 200; ++iteration) {
    const Record a = random_record(rng);
    const Record b = random_record(rng);
    const RecordDiff diff = diff_records(a, b);
    const std::size_t expected_prefix = manual_prefix(a, b);
    CHECK_EQ(diff.common_prefix, expected_prefix);
    CHECK_EQ(diff.a_events, a.events.size());
    CHECK_EQ(diff.b_events, b.events.size());
    const bool expected_equal = a.hash == b.hash && a.events.size() == b.events.size() &&
                                expected_prefix == a.events.size();
    CHECK_EQ(diff.equal, expected_equal);
    if (!expected_equal) {
      CHECK(diff.first.found);
      CHECK_EQ(diff.first.index, expected_prefix);
      CHECK_EQ(diff.first.has_a, expected_prefix < a.events.size());
      CHECK_EQ(diff.first.has_b, expected_prefix < b.events.size());
      if (diff.first.has_a) {
        CHECK(diff.first.a == a.events[expected_prefix]);
        CHECK(!diff.first.summary_a.empty());
      }
      if (diff.first.has_b) {
        CHECK(diff.first.b == b.events[expected_prefix]);
        CHECK(!diff.first.summary_b.empty());
      }
    } else {
      CHECK(!diff.first.found);
    }
    std::uint64_t counted_a = 0;
    std::uint64_t counted_b = 0;
    for (const auto& count : diff.counts) {
      counted_a += count.in_a;
      counted_b += count.in_b;
    }
    CHECK_EQ(counted_a, a.events.size());
    CHECK_EQ(counted_b, b.events.size());
  }
}

TEST(diff_of_identical_records_is_equal) {
  Rng rng(1);
  const Record record = random_record(rng);
  const RecordDiff diff = diff_records(record, record);
  CHECK(diff.equal);
  CHECK(!diff.first.found);
  CHECK_EQ(diff.common_prefix, record.events.size());
}
