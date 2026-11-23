#include "host_test.hpp"
#include "test_util.hpp"
#include "tempo/replay.hpp"

using namespace tempo;

namespace {

Task racing(Context ctx) {
  const TaskId a = ctx.spawn("alpha", [](Context c) -> Task {
    for (int i = 0; i < 4; ++i) {
      c.watch("a", static_cast<std::int64_t>(i));
      co_await c.yield();
    }
  });
  const TaskId b = ctx.spawn("beta", [](Context c) -> Task {
    for (int i = 0; i < 4; ++i) {
      c.watch("b", static_cast<std::int64_t>(i));
      co_await c.yield();
    }
  });
  const bool a_ok = co_await ctx.join(a);
  const bool b_ok = co_await ctx.join(b);
  ctx.note("join", a_ok && b_ok ? "both" : "failed");
}

}  // namespace

TEST(modified_schedule_diverges_at_the_altered_step) {
  test::TestHost capabilities;
  const RecordRunResult recorded = record_run("race", racing, capabilities);
  CHECK(recorded.ok);

  ReplayOptions options;
  options.mutate_step = 1;
  options.mutate_choice = 0;
  const ReplayReport replay = replay_record(recorded.record, racing, options);
  CHECK(replay.ok);
  CHECK(replay.mutation_applied);
  CHECK(!replay.identical);
  CHECK(replay.first_divergence.found);
  CHECK(replay.first_divergence.has_expected);
  CHECK(replay.first_divergence.has_actual);
  CHECK_EQ(replay.first_divergence.expected.type, EventType::Step);
  CHECK_EQ(replay.first_divergence.actual.type, EventType::Step);
  CHECK_EQ(replay.first_divergence.expected.a, std::uint64_t(1));
  CHECK(replay.first_divergence.expected.b != replay.first_divergence.actual.b);
  CHECK(replay.first_divergence.reason.find("mismatch") != std::string::npos);
  std::size_t recorded_step_one = 0;
  for (std::size_t i = 0; i < recorded.record.events.size(); ++i) {
    const Event& event = recorded.record.events[i];
    if (event.type == EventType::Step && event.a == 1) {
      recorded_step_one = i;
      break;
    }
  }
  CHECK_EQ(replay.first_divergence.index, recorded_step_one);
}

TEST(unmutated_replay_of_same_record_stays_identical) {
  test::TestHost capabilities;
  const RecordRunResult recorded = record_run("race", racing, capabilities);
  CHECK(recorded.ok);
  ReplayOptions options;
  options.mutate_step = 1;
  const ReplayReport replay = replay_record(recorded.record, racing, options);
  CHECK(!replay.identical);
  // Replaying the same record without intervention remains identical.
  const ReplayReport clean = replay_record(recorded.record, racing);
  CHECK(clean.identical);
}

TEST(mutated_recording_replays_identically_to_itself) {
  test::TestHost capabilities;
  RecordRunOptions record_options;
  record_options.mutate_step = 1;
  const RecordRunResult mutated =
      record_run("race", racing, capabilities, record_options);
  CHECK(mutated.ok);
  CHECK(mutated.mutation_applied);
  const ReplayReport replay = replay_record(mutated.record, racing);
  CHECK(replay.ok);
  CHECK(replay.identical);
  CHECK_EQ(replay.replayed_hash, mutated.record.hash);
}

TEST(mutation_that_cannot_apply_leaves_the_run_identical) {
  test::TestHost capabilities;
  const RecordRunResult recorded = record_run("race", racing, capabilities);
  CHECK(recorded.ok);
  ReplayOptions options;
  options.mutate_step = 9999;
  const ReplayReport replay = replay_record(recorded.record, racing, options);
  CHECK(replay.ok);
  CHECK(!replay.mutation_applied);
  CHECK(replay.identical);
}

TEST(batch_replay_reports_the_first_failing_run) {
  test::TestHost capabilities;
  const RecordRunResult recorded = record_run("race", racing, capabilities);
  CHECK(recorded.ok);
  ReplayOptions options;
  options.mutate_step = 1;
  const ReplayBatchReport batch = replay_many(recorded.record, racing, 4, options);
  CHECK(batch.ok);
  CHECK(!batch.all_identical);
  CHECK_EQ(batch.first_failed_run, std::int64_t(0));
  CHECK_EQ(batch.hashes.size(), std::size_t(4));
}

TEST(mutation_choice_varies_which_task_is_picked) {
  test::TestHost capabilities;
  const RecordRunResult recorded = record_run("race", racing, capabilities);
  CHECK(recorded.ok);
  std::uint32_t seen = 0;
  for (std::uint32_t choice = 0; choice < 3; ++choice) {
    ReplayOptions options;
    options.mutate_step = 1;
    options.mutate_choice = choice;
    const ReplayReport replay = replay_record(recorded.record, racing, options);
    CHECK(!replay.identical);
    CHECK(replay.first_divergence.found);
    if (replay.first_divergence.has_expected && replay.first_divergence.has_actual) {
      seen |= (1u << (replay.first_divergence.actual.b & 1u));
    }
  }
  CHECK(seen != 0);
}
