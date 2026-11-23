#include <memory>
#include <vector>

#include "host_test.hpp"
#include "test_util.hpp"
#include "tempo/record_run.hpp"
#include "tempo/replay.hpp"

using namespace tempo;

namespace {

struct PropertyState {
  std::int64_t total = 0;
};

Task property_worker(Context ctx, std::shared_ptr<PropertyState> state, int id) {
  for (int i = 0; i < 5; ++i) {
    const std::uint64_t draw = ctx.random_u64();
    if ((draw & 1u) != 0) {
      co_await ctx.yield();
    } else {
      co_await ctx.sleep_ms(static_cast<std::uint64_t>(1 + (i % 3)));
    }
    state->total += static_cast<std::int64_t>(id + i);
    ctx.watch("total", state->total);
    ctx.watch("draw", static_cast<std::int64_t>(draw >> 60));
  }
}

Task property_scenario(Context ctx) {
  auto state = std::make_shared<PropertyState>();
  std::vector<TaskId> ids;
  for (int id = 0; id < 3; ++id) {
    ids.push_back(ctx.spawn("worker-" + std::to_string(id),
                            [state, id](Context c) { return property_worker(c, state, id); }));
  }
  for (TaskId id : ids) {
    (void)co_await ctx.join(id);
  }
  ctx.watch("final", state->total);
  ctx.note("final", std::to_string(state->total));
}

}  // namespace

TEST(property_replay_is_identical_across_many_seeds) {
  for (std::uint64_t seed = 0; seed < 25; ++seed) {
    test::TestHost host;
    RecordRunOptions options;
    options.seed = seed;
    const RecordRunResult recorded =
        record_run("property", property_scenario, host, options);
    CHECK(recorded.ok);
    CHECK(recorded.record.events.size() > 20);
    const ReplayReport replay = replay_record(recorded.record, property_scenario);
    CHECK(replay.ok);
    CHECK(replay.identical);
    CHECK_EQ(replay.replayed_hash, recorded.record.hash);
  }
}

TEST(property_same_seed_is_byte_identical) {
  test::TestHost first;
  test::TestHost second;
  RecordRunOptions options;
  options.seed = 0xDEADBEEF;
  const RecordRunResult a = record_run("property", property_scenario, first, options);
  const RecordRunResult b = record_run("property", property_scenario, second, options);
  CHECK(a.ok);
  CHECK(b.ok);
  CHECK_EQ(a.record.hash, b.record.hash);
  CHECK_EQ(a.record.events.size(), b.record.events.size());
  for (std::size_t i = 0; i < a.record.events.size(); ++i) {
    CHECK(a.record.events[i] == b.record.events[i]);
  }
}

TEST(property_different_seeds_change_the_schedule) {
  test::TestHost first;
  test::TestHost second;
  RecordRunOptions a_options;
  a_options.seed = 1;
  RecordRunOptions b_options;
  b_options.seed = 2;
  const RecordRunResult a = record_run("property", property_scenario, first, a_options);
  const RecordRunResult b = record_run("property", property_scenario, second, b_options);
  CHECK(a.ok);
  CHECK(b.ok);
  CHECK(a.record.hash != b.record.hash);
}

TEST(property_serialization_roundtrip_is_stable) {
  test::TestHost host;
  RecordRunOptions options;
  options.seed = 77;
  const RecordRunResult recorded =
      record_run("property", property_scenario, host, options);
  CHECK(recorded.ok);
  const SerializeOutcome serialized = serialize_record(recorded.record);
  CHECK(serialized.ok);
  for (int i = 0; i < 5; ++i) {
    const LoadOutcome loaded = load_record_bytes(serialized.bytes);
    CHECK(loaded.ok);
    CHECK_EQ(loaded.record.hash, recorded.record.hash);
    const SerializeOutcome again = serialize_record(loaded.record);
    CHECK(again.ok);
    CHECK(again.bytes == serialized.bytes);
  }
}
