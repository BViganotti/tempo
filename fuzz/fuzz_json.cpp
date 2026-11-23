// Deterministic mutation fuzzer for the bounded JSON parser.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "tempo/json.hpp"
#include "tempo/limits.hpp"

namespace {

struct Rng {
  std::uint64_t state;
  explicit Rng(std::uint64_t seed) : state(seed ? seed : 0x123456789ABCDEFull) {}
  std::uint64_t next() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }
  std::size_t pick(std::size_t limit) { return limit == 0 ? 0 : next() % limit; }
};

bool check_roundtrip(const tempo::json::Value& value) {
  const std::string dumped = value.dump();
  const auto parsed = tempo::json::parse(dumped);
  if (!parsed.ok) {
    std::fprintf(stderr, "parse(dump(value)) failed: %s\n", parsed.error.message.c_str());
    return false;
  }
  return parsed.value.dump() == dumped;
}

}  // namespace

int main(int argc, char** argv) {
  std::uint64_t iterations = 2000;
  std::uint64_t seed = 0xC0FFEE;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
      iterations = std::strtoull(argv[++i], nullptr, 10);
    } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      seed = std::strtoull(argv[++i], nullptr, 10);
    } else {
      std::fprintf(stderr, "usage: fuzz_json [--iterations N] [--seed S]\n");
      return 2;
    }
  }
  if (iterations > 1'000'000) iterations = 1'000'000;

  const std::vector<std::string> seeds = {
      R"({"a":1,"b":[true,false,null],"c":"text"})",
      R"([1,2,3,4,5])",
      R"({"nested":{"array":[{"deep":1}]},"n":-1.5e12})",
      R"("just a string with \u00e9 and \ud83d\ude00")",
      R"({"empty":{},"list":[]})",
  };
  Rng rng(seed);
  std::uint64_t parsed_ok = 0;
  std::uint64_t rejected = 0;
  for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
    std::string text = seeds[rng.pick(seeds.size())];
    const std::uint64_t mode = rng.next() % 5;
    if (mode == 0 && !text.empty()) {
      text[rng.pick(text.size())] = static_cast<char>(rng.next() & 0xFF);
    } else if (mode == 1 && !text.empty()) {
      text.resize(rng.pick(text.size()));
    } else if (mode == 2) {
      text.insert(text.begin() + static_cast<std::ptrdiff_t>(rng.pick(text.size() + 1)),
                  static_cast<char>(rng.next() & 0xFF));
    } else if (mode == 3 && text.size() > 2) {
      const std::size_t at = rng.pick(text.size() - 1);
      text.erase(at, 1 + rng.pick(3));
    } else {
      text.assign(rng.pick(256), '\0');
      for (char& c : text) c = static_cast<char>(rng.next() & 0xFF);
    }

    const auto outcome = tempo::json::parse(text);
    if (outcome.ok) {
      ++parsed_ok;
      if (!check_roundtrip(outcome.value)) {
        std::fprintf(stderr, "roundtrip mismatch at iteration %llu\n",
                     static_cast<unsigned long long>(iteration));
        return 1;
      }
      if (outcome.value.dump().size() > tempo::limits::kMaxJsonBytes * 8) {
        std::fprintf(stderr, "dump unexpectedly large\n");
        return 1;
      }
    } else {
      ++rejected;
      if (outcome.error.message.empty()) {
        std::fprintf(stderr, "rejected without a message\n");
        return 1;
      }
    }
  }
  std::printf("fuzz_json: %llu iterations, %llu accepted, %llu rejected, seed=%llu\n",
              static_cast<unsigned long long>(iterations),
              static_cast<unsigned long long>(parsed_ok),
              static_cast<unsigned long long>(rejected),
              static_cast<unsigned long long>(seed));
  return 0;
}
