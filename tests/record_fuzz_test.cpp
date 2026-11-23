#include "record_test_util.hpp"
#include "test_util.hpp"
#include "tempo/limits.hpp"
#include "tempo/record.hpp"

using namespace tempo;

namespace {

std::uint64_t next_random(std::uint64_t& state) {
  state ^= state << 13;
  state ^= state >> 7;
  state ^= state << 17;
  return state;
}

}  // namespace

TEST(mutated_records_never_crash_or_claim_false_crcs) {
  const Record valid = test::sample_record();
  const SerializeOutcome base = serialize_record(valid);
  CHECK(base.ok);
  std::uint64_t state = 0xC0FFEE123456789ull;

  int strict_ok = 0;
  int recover_ok = 0;
  for (int iteration = 0; iteration < 3000; ++iteration) {
    std::string bytes = base.bytes;
    const std::uint64_t mode = next_random(state) % 6;
    if (mode == 0 && !bytes.empty()) {
      const std::size_t at = next_random(state) % bytes.size();
      bytes[at] ^= static_cast<char>(1u << (next_random(state) % 8));
    } else if (mode == 1 && !bytes.empty()) {
      bytes.resize(next_random(state) % bytes.size());
    } else if (mode == 2) {
      const std::size_t at = next_random(state) % (bytes.size() + 1);
      bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at),
                   static_cast<char>(next_random(state) & 0xFF));
    } else if (mode == 3 && bytes.size() >= 4) {
      const std::size_t at = next_random(state) % (bytes.size() - 3);
      const std::uint32_t value = static_cast<std::uint32_t>(next_random(state));
      for (int i = 0; i < 4; ++i) {
        bytes[at + static_cast<std::size_t>(i)] =
            static_cast<char>((value >> (8 * i)) & 0xFF);
      }
    } else if (mode == 4 && !bytes.empty()) {
      const std::size_t at = next_random(state) % bytes.size();
      bytes[at] = 0;
    } else if (mode == 5) {
      bytes.append(next_random(state) % 8, static_cast<char>(next_random(state) & 0xFF));
    }

    const LoadOutcome recovered = load_record_bytes(bytes, LoadMode::Recover);
    const LoadOutcome strict = load_record_bytes(bytes, LoadMode::Strict);
    if (recovered.ok) {
      ++recover_ok;
      CHECK_EQ(recovered.record.hash, recovered.record.compute_hash());
      CHECK(recovered.record.events.size() <= limits::kMaxEvents);
      for (const auto& event : recovered.record.events) {
        CHECK(event.s1.size() <= limits::kMaxStringBytes);
        CHECK(event.s2.size() <= limits::kMaxStringBytes);
        CHECK(event.bytes.size() <= limits::kMaxEventPayload);
      }
      if (recovered.record.complete) {
        CHECK(strict.ok);
      }
    }
    if (strict.ok) {
      ++strict_ok;
      CHECK(recovered.ok);
      CHECK(recovered.record.complete);
      const SerializeOutcome reserialized = serialize_record(strict.record);
      CHECK(reserialized.ok);
      const LoadOutcome again = load_record_bytes(reserialized.bytes, LoadMode::Strict);
      CHECK(again.ok);
      CHECK(test::same_events(again.record.events, strict.record.events));
    }
  }
  CHECK(recover_ok > 100);
  // Most mutations break CRC or structure; only a few should stay valid.
  CHECK(strict_ok < 3000);
}

TEST(random_buffers_with_valid_header_do_not_allocate_wildly) {
  std::uint64_t state = 0xDEADBEA7ull;
  const std::size_t base_size = serialize_record(test::sample_record()).bytes.size();
  for (int iteration = 0; iteration < 1000; ++iteration) {
    std::string bytes(base_size, '\0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      bytes[i] = static_cast<char>(next_random(state) & 0xFF);
    }
    const LoadOutcome outcome = load_record_bytes(bytes, LoadMode::Recover);
    if (outcome.ok) {
      CHECK_EQ(outcome.record.hash, outcome.record.compute_hash());
    }
  }
}
