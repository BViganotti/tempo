// Deterministic mutation fuzzer for the Tempo record reader.
// Usage: fuzz_record [--iterations N] [--seed S]
//
// Not a formal fuzzer harness: it needs no third-party runtime and runs the
// exact production parser under every mutation. A sanitizer build turns any
// memory error into a hard failure.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "tempo/crc32.hpp"
#include "tempo/limits.hpp"
#include "tempo/record.hpp"

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

tempo::Event make_event(tempo::EventType type, tempo::TaskId task, std::uint64_t a,
                        std::string s1, std::size_t payload) {
  tempo::Event event;
  event.type = type;
  event.task = task;
  event.a = a;
  event.b = a * 7;
  event.s1 = std::move(s1);
  event.s2 = "value-" + std::to_string(a);
  event.bytes.assign(payload, static_cast<char>('a' + (a % 26)));
  return event;
}

tempo::Record base_record() {
  tempo::Record record;
  record.meta.scenario = "fuzz";
  record.meta.seed = 1234;
  record.meta.created_unix = 1700000000;
  record.events.push_back(make_event(tempo::EventType::ScenarioStart, 0, 1, "fuzz", 0));
  for (std::uint64_t i = 0; i < 24; ++i) {
    const auto type = static_cast<tempo::EventType>(1 + (i % 26));
    record.events.push_back(make_event(type, static_cast<tempo::TaskId>(1 + i % 4), i,
                                       "event-" + std::to_string(i), i % 17));
  }
  record.events.push_back(make_event(tempo::EventType::ScenarioEnd, 0, 99, "ok", 0));
  record.recompute_hash();
  return record;
}

bool invariants(const tempo::LoadOutcome& outcome, bool /*strict*/) {
  if (!outcome.ok) {
    return true;
  }
  if (outcome.record.hash != outcome.record.compute_hash()) {
    std::fprintf(stderr, "hash mismatch\n");
    return false;
  }
  if (outcome.record.events.size() > tempo::limits::kMaxEvents) {
    std::fprintf(stderr, "event count exceeds limit\n");
    return false;
  }
  for (const auto& event : outcome.record.events) {
    if (event.s1.size() > tempo::limits::kMaxStringBytes ||
        event.s2.size() > tempo::limits::kMaxStringBytes ||
        event.bytes.size() > tempo::limits::kMaxEventPayload) {
      std::fprintf(stderr, "event exceeds limit\n");
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::uint64_t iterations = 2000;
  std::uint64_t seed = 0x51EED;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
      iterations = std::strtoull(argv[++i], nullptr, 10);
    } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      seed = std::strtoull(argv[++i], nullptr, 10);
    } else {
      std::fprintf(stderr, "usage: fuzz_record [--iterations N] [--seed S]\n");
      return 2;
    }
  }
  if (iterations > 1'000'000) {
    iterations = 1'000'000;
  }
  Rng rng(seed);
  const tempo::SerializeOutcome base = tempo::serialize_record(base_record());
  if (!base.ok) {
    std::fprintf(stderr, "base record failed: %s\n", base.error.c_str());
    return 1;
  }

  std::uint64_t recovered = 0;
  std::uint64_t strict_ok = 0;
  for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
    std::string bytes = base.bytes;
    const std::uint64_t mode = rng.next() % 7;
    switch (mode) {
      case 0:
        if (!bytes.empty()) bytes[rng.pick(bytes.size())] ^= static_cast<char>(1u << rng.pick(8));
        break;
      case 1:
        if (!bytes.empty()) bytes.resize(rng.pick(bytes.size()));
        break;
      case 2: {
        const std::size_t at = rng.pick(bytes.size() + 1);
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                     static_cast<char>(rng.next() & 0xFF));
        break;
      }
      case 3:
        if (bytes.size() >= 4) {
          const std::size_t at = rng.pick(bytes.size() - 3);
          const std::uint32_t value = static_cast<std::uint32_t>(rng.next());
          for (int i = 0; i < 4; ++i) {
            bytes[at + static_cast<std::size_t>(i)] = static_cast<char>((value >> (8 * i)) & 0xFF);
          }
        }
        break;
      case 4:
        bytes.append(rng.pick(16), static_cast<char>(rng.next() & 0xFF));
        break;
      case 5:
        if (bytes.size() > 8) {
          const std::size_t at = rng.pick(bytes.size() - 4);
          bytes.erase(at, 1 + rng.pick(4));
        }
        break;
      default: {
        bytes.assign(rng.pick(512), '\0');
        for (char& c : bytes) c = static_cast<char>(rng.next() & 0xFF);
        if (bytes.size() >= 4) {
          bytes[0] = 'T'; bytes[1] = 'M'; bytes[2] = 'P'; bytes[3] = 'O';
        }
        break;
      }
    }

    const tempo::LoadOutcome recovered_outcome =
        tempo::load_record_bytes(bytes, tempo::LoadMode::Recover);
    const tempo::LoadOutcome strict_outcome =
        tempo::load_record_bytes(bytes, tempo::LoadMode::Strict);
    if (!invariants(recovered_outcome, false) || !invariants(strict_outcome, true)) {
      std::fprintf(stderr, "invariant failed at iteration %llu\n",
                   static_cast<unsigned long long>(iteration));
      return 1;
    }
    if (recovered_outcome.ok) ++recovered;
    if (strict_outcome.ok) {
      ++strict_ok;
      const tempo::SerializeOutcome again = tempo::serialize_record(strict_outcome.record);
      if (!again.ok) {
        std::fprintf(stderr, "reserialize failed\n");
        return 1;
      }
      const tempo::LoadOutcome reload =
          tempo::load_record_bytes(again.bytes, tempo::LoadMode::Strict);
      if (!reload.ok || reload.record.events.size() != strict_outcome.record.events.size()) {
        std::fprintf(stderr, "reload failed\n");
        return 1;
      }
    }
  }
  std::printf("fuzz_record: %llu iterations, %llu recovered, %llu strict-valid, seed=%llu\n",
              static_cast<unsigned long long>(iterations),
              static_cast<unsigned long long>(recovered),
              static_cast<unsigned long long>(strict_ok),
              static_cast<unsigned long long>(seed));
  return 0;
}
