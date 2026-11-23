#include "test_util.hpp"
#include "tempo/event.hpp"

using namespace tempo;

namespace {

Event make(EventType type) {
  Event event;
  event.type = type;
  event.task = 3;
  event.a = 1;
  event.b = 2;
  event.c = 3;
  event.d = 4;
  event.s1 = "one";
  event.s2 = "two";
  event.bytes = "three";
  return event;
}

}  // namespace

TEST(event_names_roundtrip_through_switch) {
  CHECK_EQ(event_type_name(EventType::ScenarioStart), "ScenarioStart");
  CHECK_EQ(event_type_name(EventType::Step), "Step");
  CHECK_EQ(event_type_name(EventType::Watch), "Watch");
  CHECK_EQ(event_type_name(EventType::SocketRecv), "SocketRecv");
  CHECK_EQ(event_type_name(static_cast<EventType>(250)), "?");
}

TEST(capability_classification) {
  CHECK(is_capability_event(EventType::FileRead));
  CHECK(is_capability_event(EventType::FileWrite));
  CHECK(is_capability_event(EventType::EnvGet));
  CHECK(is_capability_event(EventType::SocketConnect));
  CHECK(is_capability_event(EventType::SocketSend));
  CHECK(is_capability_event(EventType::SocketRecv));
  CHECK(is_capability_event(EventType::SocketClose));
  CHECK(!is_capability_event(EventType::Step));
  CHECK(!is_capability_event(EventType::Watch));
}

TEST(lifecycle_classification) {
  CHECK(is_lifecycle_event(EventType::Spawn));
  CHECK(is_lifecycle_event(EventType::Step));
  CHECK(is_lifecycle_event(EventType::Exit));
  CHECK(is_lifecycle_event(EventType::Join));
  CHECK(!is_lifecycle_event(EventType::FileRead));
  CHECK(!is_lifecycle_event(EventType::Rng));
}

TEST(summaries_are_nonempty) {
  for (int i = 0; i <= 26; ++i) {
    const Event event = make(static_cast<EventType>(i));
    const std::string summary = event_summary(event);
    CHECK(!summary.empty());
  }
}

TEST(equality_compares_every_field) {
  Event a = make(EventType::Step);
  Event b = a;
  CHECK(a == b);
  b.d = 5;
  CHECK(a != b);
  b = a;
  b.bytes = "different";
  CHECK(a != b);
  b = a;
  b.task = 9;
  CHECK(a != b);
}

TEST(hash_is_sensitive_to_every_field) {
  const Event base = make(EventType::Step);
  const std::uint64_t seed = 14695981039346656037ull;
  const std::uint64_t reference = event_hash(seed, base);
  {
    Event changed = base;
    changed.type = EventType::Watch;
    CHECK(event_hash(seed, changed) != reference);
  }
  {
    Event changed = base;
    changed.task = 4;
    CHECK(event_hash(seed, changed) != reference);
  }
  std::uint64_t Event::*const fields[] = {&Event::a, &Event::b, &Event::c, &Event::d};
  for (auto field : fields) {
    Event changed = base;
    changed.*field += 1;
    CHECK(event_hash(seed, changed) != reference);
  }
  {
    Event changed = base;
    changed.s1 = "one!";
    CHECK(event_hash(seed, changed) != reference);
  }
  {
    Event changed = base;
    changed.bytes = "thre";
    CHECK(event_hash(seed, changed) != reference);
  }
}

TEST(fnv_matches_known_value) {
  CHECK_EQ(fnv1a64(14695981039346656037ull, "a"), 0xAF63DC4C8601EC8Cull);
  CHECK_EQ(fnv1a64(14695981039346656037ull, ""), 14695981039346656037ull);
}
