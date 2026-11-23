#include "record_test_util.hpp"
#include "temp_dir.hpp"
#include "test_util.hpp"
#include "tempo/limits.hpp"
#include "tempo/record.hpp"

using namespace tempo;

TEST(roundtrip_preserves_meta_events_and_hash) {
  const Record original = test::sample_record();
  const SerializeOutcome serialized = serialize_record(original);
  CHECK(serialized.ok);
  CHECK(serialized.bytes.size() > 64);

  const LoadOutcome loaded = load_record_bytes(serialized.bytes);
  CHECK(loaded.ok);
  CHECK_EQ(loaded.error, std::string());
  CHECK(loaded.record.complete);
  CHECK(!loaded.record.recovered);
  CHECK_EQ(loaded.record.meta, original.meta);
  CHECK(test::same_events(loaded.record.events, original.events));
  CHECK_EQ(loaded.record.hash, original.hash);
  CHECK_EQ(loaded.record.hash_hex().size(), 16u);
}

TEST(roundtrip_survives_file_io) {
  test::TempDir dir;
  const Record original = test::sample_record();
  std::string error;
  const std::string path = dir.path() + "/sample.tmpo";
  CHECK(write_record_file(original, path, &error));
  CHECK_EQ(error, std::string());

  const LoadOutcome loaded = load_record_file(path);
  CHECK(loaded.ok);
  CHECK(test::same_events(loaded.record.events, original.events));
  CHECK_EQ(loaded.record.hash, original.hash);
}

TEST(empty_event_stream_roundtrips) {
  Record empty;
  empty.meta.scenario = "empty";
  empty.recompute_hash();
  const SerializeOutcome serialized = serialize_record(empty);
  CHECK(serialized.ok);
  const LoadOutcome loaded = load_record_bytes(serialized.bytes);
  CHECK(loaded.ok);
  CHECK(loaded.record.complete);
  CHECK(loaded.record.events.empty());
  CHECK_EQ(loaded.record.hash, empty.hash);
}

TEST(deterministic_serialization) {
  const Record record = test::sample_record();
  const SerializeOutcome a = serialize_record(record);
  const SerializeOutcome b = serialize_record(record);
  CHECK(a.ok);
  CHECK(b.ok);
  CHECK(a.bytes == b.bytes);
}

TEST(hash_depends_on_event_content) {
  Record record = test::sample_record();
  const std::uint64_t before = record.recompute_hash();
  record.events[3].s2 = "8";
  CHECK(record.recompute_hash() != before);
  CHECK_EQ(record.hash, record.recompute_hash());
}

TEST(attributes_are_preserved) {
  Record record = test::sample_record();
  record.meta.attributes["custom/key"] = "value with spaces";
  const SerializeOutcome serialized = serialize_record(record);
  CHECK(serialized.ok);
  const LoadOutcome loaded = load_record_bytes(serialized.bytes);
  CHECK(loaded.ok);
  CHECK_EQ(loaded.record.meta.attributes.at("kind"), std::string("unit-test"));
  CHECK_EQ(loaded.record.meta.attributes.at("custom/key"),
           std::string("value with spaces"));
}

TEST(repeated_strings_are_interned_once) {
  Record record;
  for (int i = 0; i < 100; ++i) {
    Event event;
    event.type = EventType::Log;
    event.s1 = "the same message";
    event.s2 = "the same value";
    record.events.push_back(event);
  }
  const SerializeOutcome serialized = serialize_record(record);
  CHECK(serialized.ok);
  // Even 100 events should stay compact over the 64-byte header + trailer.
  CHECK(serialized.bytes.size() < 8000);
  const LoadOutcome loaded = load_record_bytes(serialized.bytes);
  CHECK(loaded.ok);
  CHECK_EQ(loaded.record.events.size(), 100u);
  CHECK_EQ(loaded.record.events[99].s1, "the same message");
}

TEST(unknown_event_types_are_preserved) {
  Record record = test::sample_record();
  Event future;
  future.type = static_cast<EventType>(240);
  future.task = 7;
  future.a = 99;
  record.events.push_back(future);
  record.recompute_hash();
  const SerializeOutcome serialized = serialize_record(record);
  CHECK(serialized.ok);
  const LoadOutcome loaded = load_record_bytes(serialized.bytes);
  CHECK(loaded.ok);
  CHECK_EQ(static_cast<int>(loaded.record.events.back().type), 240);
  CHECK_EQ(loaded.record.events.back().a, 99u);
}

TEST(oversized_payload_fails_without_writing) {
  Record record;
  Event event;
  event.type = EventType::FileRead;
  event.bytes = std::string(limits::kMaxEventPayload + 1, 'x');
  record.events.push_back(event);
  const SerializeOutcome serialized = serialize_record(record);
  CHECK(!serialized.ok);
  CHECK(serialized.error.find("payload") != std::string::npos);
}

TEST(oversized_event_string_fails) {
  Record record;
  Event event;
  event.type = EventType::Log;
  event.s1 = std::string(limits::kMaxEventString + 1, 'x');
  record.events.push_back(event);
  CHECK(!serialize_record(record).ok);
}

TEST(write_failure_is_reported) {
  const Record record = test::sample_record();
  std::string error;
  CHECK(!write_record_file(record, "/nonexistent-tempo-dir/record.tmpo", &error));
  CHECK(!error.empty());
}

TEST(loaded_record_can_be_resaved) {
  const Record original = test::sample_record();
  const SerializeOutcome first = serialize_record(original);
  CHECK(first.ok);
  const LoadOutcome loaded = load_record_bytes(first.bytes);
  CHECK(loaded.ok);
  const SerializeOutcome second = serialize_record(loaded.record);
  CHECK(second.ok);
  CHECK(second.bytes == first.bytes);
}
