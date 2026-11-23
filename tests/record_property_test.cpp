#include <cstdint>
#include <string>
#include <vector>

#include "test_util.hpp"
#include "tempo/limits.hpp"
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

std::string random_string(Rng& rng, std::size_t max_length, bool allow_nul) {
  const std::size_t length = rng.pick(max_length + 1);
  std::string value;
  value.reserve(length);
  for (std::size_t i = 0; i < length; ++i) {
    char c = static_cast<char>(32 + (rng.next() % 90));
    if (allow_nul && rng.pick(20) == 0) {
      c = '\0';
    }
    value.push_back(c);
  }
  return value;
}

Record random_record(Rng& rng) {
  Record record;
  record.meta.scenario = random_string(rng, 24, false);
  record.meta.tool_version = "property";
  record.meta.seed = rng.next();
  record.meta.created_unix = rng.next() % 2'000'000'000ull;
  record.meta.start_ns = 1 + rng.pick(1'000'000);
  const std::size_t attributes = rng.pick(4);
  for (std::size_t i = 0; i < attributes; ++i) {
    record.meta.attributes["key-" + std::to_string(i)] =
        random_string(rng, 32, true);
  }
  const std::size_t count = rng.pick(25);
  for (std::size_t i = 0; i < count; ++i) {
    Event event;
    event.type = static_cast<EventType>(rng.pick(27));
    event.task = static_cast<TaskId>(rng.pick(8));
    event.a = rng.next();
    event.b = rng.next();
    event.c = rng.next();
    event.d = rng.next();
    event.s1 = random_string(rng, 48, true);
    event.s2 = random_string(rng, 48, true);
    const std::size_t payload = rng.pick(40);
    for (std::size_t k = 0; k < payload; ++k) {
      event.bytes.push_back(static_cast<char>(rng.next() & 0xFF));
    }
    record.events.push_back(std::move(event));
  }
  record.recompute_hash();
  return record;
}

}  // namespace

TEST(random_records_roundtrip_exactly) {
  Rng rng(0xAB5EED);
  for (int iteration = 0; iteration < 200; ++iteration) {
    const Record original = random_record(rng);
    const SerializeOutcome serialized = serialize_record(original);
    CHECK(serialized.ok);
    const LoadOutcome loaded = load_record_bytes(serialized.bytes);
    CHECK(loaded.ok);
    CHECK(loaded.record.complete);
    CHECK_EQ(loaded.record.meta, original.meta);
    CHECK_EQ(loaded.record.hash, original.hash);
    CHECK_EQ(loaded.record.events.size(), original.events.size());
    for (std::size_t i = 0; i < original.events.size(); ++i) {
      CHECK(loaded.record.events[i] == original.events[i]);
    }
    const SerializeOutcome again = serialize_record(loaded.record);
    CHECK(again.ok);
    CHECK(again.bytes == serialized.bytes);
  }
}

TEST(random_records_survive_every_truncation_as_a_prefix) {
  Rng rng(0xFEED5EED);
  for (int iteration = 0; iteration < 25; ++iteration) {
    const Record original = random_record(rng);
    const SerializeOutcome serialized = serialize_record(original);
    CHECK(serialized.ok);
    for (std::size_t length = 0; length < serialized.bytes.size();
         length += 1 + serialized.bytes.size() / 64) {
      const std::string_view prefix(serialized.bytes.data(), length);
      const LoadOutcome recovered = load_record_bytes(prefix, LoadMode::Recover);
      if (length < 40) {
        CHECK(!recovered.ok);
        continue;
      }
      CHECK(recovered.ok);
      CHECK(!recovered.record.complete);
      CHECK(recovered.record.events.size() <= original.events.size());
      for (std::size_t i = 0; i < recovered.record.events.size(); ++i) {
        CHECK(recovered.record.events[i] == original.events[i]);
      }
      CHECK_EQ(recovered.record.hash, recovered.record.compute_hash());
    }
  }
}
