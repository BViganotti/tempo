#include "record_test_util.hpp"
#include "test_util.hpp"
#include "tempo/crc32.hpp"
#include "tempo/limits.hpp"
#include "tempo/record.hpp"

using namespace tempo;

namespace {

bool is_event_prefix(const std::vector<Event>& prefix, const std::vector<Event>& full) {
  if (prefix.size() > full.size()) {
    return false;
  }
  for (std::size_t i = 0; i < prefix.size(); ++i) {
    if (!(prefix[i] == full[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace

TEST(truncated_prefixes_recover_without_inventing_events) {
  const Record original = test::sample_record();
  const SerializeOutcome serialized = serialize_record(original);
  CHECK(serialized.ok);
  const std::string& bytes = serialized.bytes;

  std::size_t recovered_count = 0;
  std::size_t strict_count = 0;
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    const std::string_view prefix(bytes.data(), length);
    const LoadOutcome recovered = load_record_bytes(prefix, LoadMode::Recover);
    if (length < 40) {
      CHECK(!recovered.ok);
      continue;
    }
    CHECK(recovered.ok);
    CHECK(is_event_prefix(recovered.record.events, original.events));
    CHECK(!recovered.record.complete);
    CHECK(recovered.record.recovered);
    CHECK(!recovered.record.warning.empty());
    CHECK_EQ(recovered.record.hash, recovered.record.compute_hash());
    ++recovered_count;

    const LoadOutcome strict = load_record_bytes(prefix, LoadMode::Strict);
    CHECK(!strict.ok);
    ++strict_count;
  }
  CHECK(recovered_count > 100);
  CHECK_EQ(strict_count, recovered_count);
}

TEST(truncation_inside_first_event_yields_zero_events) {
  const Record original = test::sample_record();
  const SerializeOutcome serialized = serialize_record(original);
  CHECK(serialized.ok);
  // Header + metadata + string table are at the front; cut at 120 bytes, which
  // is inside the sections before the first complete event.
  const LoadOutcome recovered =
      load_record_bytes(std::string_view(serialized.bytes.data(), 120), LoadMode::Recover);
  CHECK(recovered.ok);
  CHECK(recovered.record.events.empty());
  CHECK(!recovered.record.complete);
}

TEST(corrupted_payload_is_caught_by_event_crc) {
  const Record original = test::sample_record();
  SerializeOutcome serialized = serialize_record(original);
  CHECK(serialized.ok);
  // Find an occurrence of the unique payload and flip a byte.
  const std::string needle("a\0b\0c", 5);
  const std::size_t at = serialized.bytes.find(needle);
  CHECK(at != std::string::npos);
  serialized.bytes[at] ^= 0x40;

  const LoadOutcome strict = load_record_bytes(serialized.bytes);
  CHECK(!strict.ok);

  const LoadOutcome recovered = load_record_bytes(serialized.bytes, LoadMode::Recover);
  CHECK(recovered.ok);
  CHECK(!recovered.record.complete);
  CHECK(recovered.record.warning.find("checksum") != std::string::npos);
  CHECK(recovered.record.events.size() < original.events.size());
  CHECK(is_event_prefix(recovered.record.events, original.events));
}

TEST(corrupted_body_crc_is_caught) {
  const Record original = test::sample_record();
  SerializeOutcome serialized = serialize_record(original);
  CHECK(serialized.ok);
  CHECK(serialized.bytes.size() > 24);
  // The four bytes immediately before the 20-byte footer are the body CRC.
  serialized.bytes[serialized.bytes.size() - 24] ^= 0x1;
  const LoadOutcome strict = load_record_bytes(serialized.bytes);
  CHECK(!strict.ok);
  const LoadOutcome recovered = load_record_bytes(serialized.bytes, LoadMode::Recover);
  CHECK(recovered.ok);
  CHECK(!recovered.record.complete);
  CHECK(is_event_prefix(recovered.record.events, original.events));
}

TEST(bad_magic_and_header_crc_are_rejected) {
  const SerializeOutcome serialized = serialize_record(test::sample_record());
  CHECK(serialized.ok);

  std::string bad_magic = serialized.bytes;
  bad_magic[0] = 'X';
  CHECK(!load_record_bytes(bad_magic, LoadMode::Strict).ok);
  CHECK(!load_record_bytes(bad_magic, LoadMode::Recover).ok);

  std::string bad_crc = serialized.bytes;
  bad_crc[4] ^= 0x1;  // version byte: also changes version
  CHECK(!load_record_bytes(bad_crc, LoadMode::Strict).ok);
}

TEST(unsupported_version_is_rejected) {
  SerializeOutcome serialized = serialize_record(test::sample_record());
  CHECK(serialized.ok);
  serialized.bytes[4] = 9;
  serialized.bytes[5] = 0;
  // Recompute the header CRC so only the version check can reject it.
  const std::string_view first8(serialized.bytes.data(), 8);
  const std::uint32_t crc8 = crc32(first8);
  for (int i = 0; i < 4; ++i) {
    serialized.bytes[8 + i] = static_cast<char>((crc8 >> (8 * i)) & 0xFF);
  }
  const std::uint32_t crc36 = crc32(std::string_view(serialized.bytes.data(), 36));
  for (int i = 0; i < 4; ++i) {
    serialized.bytes[36 + i] = static_cast<char>((crc36 >> (8 * i)) & 0xFF);
  }
  const LoadOutcome outcome = load_record_bytes(serialized.bytes);
  CHECK(!outcome.ok);
  CHECK(outcome.error.find("version") != std::string::npos);
}

TEST(trailing_garbage_is_rejected_in_strict_mode) {
  SerializeOutcome serialized = serialize_record(test::sample_record());
  CHECK(serialized.ok);
  serialized.bytes.append("garbage");
  CHECK(!load_record_bytes(serialized.bytes, LoadMode::Strict).ok);
  const LoadOutcome recovered = load_record_bytes(serialized.bytes, LoadMode::Recover);
  CHECK(recovered.ok);
  CHECK(!recovered.record.complete);
}

TEST(random_short_buffers_do_not_crash_or_lie) {
  // Deterministic pseudo-random header-ish buffers.
  std::uint64_t state = 0x123456789ABCDEFull;
  auto next = [&state]() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  };
  for (int iteration = 0; iteration < 500; ++iteration) {
    std::string bytes;
    const std::size_t length = next() % 200;
    bytes.resize(length);
    for (std::size_t i = 0; i < length; ++i) {
      bytes[i] = static_cast<char>(next() & 0xFF);
    }
    const LoadOutcome outcome = load_record_bytes(bytes, LoadMode::Recover);
    if (outcome.ok) {
      CHECK(outcome.record.events.size() <= limits::kMaxEvents);
      CHECK_EQ(outcome.record.hash, outcome.record.compute_hash());
    }
  }
}
