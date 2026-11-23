#include <cstdint>
#include <string>

#include "test_util.hpp"
#include "tempo/json.hpp"

using namespace tempo::json;

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

std::string random_text(Rng& rng) {
  const std::size_t length = rng.pick(24);
  std::string text;
  for (std::size_t i = 0; i < length; ++i) {
    const int kind = static_cast<int>(rng.pick(6));
    if (kind == 0) {
      text.push_back(static_cast<char>(rng.pick(0x20)));
    } else if (kind == 1) {
      text.push_back('"');
    } else if (kind == 2) {
      text.push_back('\\');
    } else if (kind == 3) {
      text.append("\xF0\x9F\x98\x80");
    } else {
      text.push_back(static_cast<char>(32 + rng.pick(90)));
    }
  }
  return text;
}

Value random_value(Rng& rng, int depth) {
  const std::size_t kind = depth <= 0 ? rng.pick(4) : rng.pick(6);
  switch (kind) {
    case 0:
      return Value();
    case 1:
      return Value((rng.next() & 1u) != 0);
    case 2: {
      const std::int64_t integer = static_cast<std::int64_t>(rng.next() % 2'000'001) - 1'000'000;
      return Value(integer);
    }
    case 3:
      return Value(random_text(rng));
    case 4: {
      Value array = Value::array();
      const std::size_t count = rng.pick(4);
      for (std::size_t i = 0; i < count; ++i) {
        array.push(random_value(rng, depth - 1));
      }
      return array;
    }
    default: {
      Value object = Value::object();
      const std::size_t count = rng.pick(4);
      for (std::size_t i = 0; i < count; ++i) {
        object["key-" + std::to_string(i)] = random_value(rng, depth - 1);
      }
      return object;
    }
  }
}

}  // namespace

TEST(random_json_values_roundtrip_through_dump_and_parse) {
  Rng rng(0x7E57);
  for (int iteration = 0; iteration < 500; ++iteration) {
    const Value value = random_value(rng, 5);
    const std::string dumped = value.dump();
    const ParseOutcome parsed = parse(dumped);
    CHECK(parsed.ok);
    CHECK_EQ(parsed.value.dump(), dumped);
    CHECK(valid_utf8(dumped));
  }
}

TEST(random_json_is_rejected_when_truncated) {
  Rng rng(0xBEEF);
  for (int iteration = 0; iteration < 200; ++iteration) {
    const std::string dumped = random_value(rng, 4).dump();
    if (dumped.size() < 2) continue;
    // A truncated top-level number (e.g. 123 -> 12) is still valid JSON, so
    // only structural values have a guaranteed-invalid one-byte cut.
    if (dumped.front() != '[' && dumped.front() != '{' && dumped.front() != '"') {
      continue;
    }
    const std::string truncated = dumped.substr(0, dumped.size() - 1);
    const ParseOutcome parsed = parse(truncated);
    CHECK(!parsed.ok);
    CHECK(!parsed.error.message.empty());
  }
}

TEST(object_key_order_is_canonical) {
  Value object = Value::object();
  object["z"] = Value(1);
  object["a"] = Value(2);
  object["m"] = Value(3);
  const std::string dumped = object.dump();
  CHECK(dumped.find("\"a\":2,\"m\":3,\"z\":1") != std::string::npos);
}
