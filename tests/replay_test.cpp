#include <cstdlib>
#include <filesystem>

#include "host_test.hpp"
#include "record_test_util.hpp"
#include "temp_dir.hpp"
#include "test_util.hpp"
#include "tempo/replay.hpp"
#include "tempo/system.hpp"

using namespace tempo;

namespace {

Task replay_scenario(Context ctx) {
  const TaskId a = ctx.spawn("alpha", [](Context c) -> Task {
    for (int i = 0; i < 3; ++i) {
      c.watch("round", static_cast<std::int64_t>(i));
      co_await c.yield();
    }
    c.note("alpha", "done");
  });
  const TaskId b = ctx.spawn("beta", [](Context c) -> Task {
    c.watch("phase", "start");
    co_await c.sleep_ms(2);
    c.watch("phase", "end");
    c.note("beta", "done");
  });
  const bool a_ok = co_await ctx.join(a);
  const bool b_ok = co_await ctx.join(b);
  ctx.note("join", a_ok && b_ok ? "both" : "failed");
}

Task changed_scenario(Context ctx) {
  ctx.log("this program changed");
  ctx.watch("new", true);
  co_return;
}

Task failing_scenario(Context ctx) {
  const TaskId child = ctx.spawn("child", [](Context) -> Task {
    throw std::runtime_error("expected failure");
    co_return;
  });
  (void)co_await ctx.join(child);
}

Task write_scenario(Context ctx) {
  const CapResult written = ctx.write_file("scratch.txt", "recorded write");
  ctx.watch("write_ok", written.ok);
  co_return;
}

Task file_scenario(Context ctx) {
  const CapResult read = ctx.read_file("manifest.txt");
  ctx.watch("ok", read.ok);
  ctx.watch("manifest", read.data);
  const CapResult env = ctx.env_get("TEMPO_REPLAY_ENV");
  ctx.watch("env", env.data);
  co_return;
}

}  // namespace

TEST(replay_regenerates_the_recorded_event_stream) {
  test::TestHost capabilities;
  const RecordRunResult recorded = record_run("replay", replay_scenario, capabilities);
  CHECK(recorded.ok);

  const ReplayReport replay = replay_record(recorded.record, replay_scenario);
  CHECK(replay.ok);
  CHECK(replay.identical);
  CHECK(replay.complete);
  CHECK(!replay.first_divergence.found);
  CHECK_EQ(replay.replayed_hash, recorded.record.hash);
  CHECK_EQ(replay.replayed_events, recorded.record.events.size());
  CHECK_EQ(replay.recorded_hash, recorded.record.hash);
}

TEST(replay_is_stable_across_one_hundred_runs) {
  test::TestHost capabilities;
  const RecordRunResult recorded = record_run("replay", replay_scenario, capabilities);
  CHECK(recorded.ok);
  const ReplayBatchReport batch = replay_many(recorded.record, replay_scenario, 100);
  CHECK(batch.ok);
  CHECK_EQ(batch.runs, 100u);
  CHECK_EQ(batch.hashes.size(), 100u);
  CHECK(batch.all_identical);
  CHECK(batch.all_hashes_equal);
  CHECK(batch.elapsed_ns > 0);
  for (std::uint64_t hash : batch.hashes) {
    CHECK_EQ(hash, recorded.record.hash);
  }
}

TEST(replay_of_a_failed_run_reproduces_the_failure) {
  test::TestHost capabilities;
  const RecordRunResult recorded = record_run("failing", failing_scenario, capabilities);
  CHECK(!recorded.ok);
  CHECK(!recorded.summary.ok);
  const ReplayReport replay = replay_record(recorded.record, failing_scenario);
  CHECK(replay.ok);
  CHECK(replay.identical);
  CHECK(!replay.summary.ok);
  CHECK_EQ(replay.summary.tasks_failed, 1u);
}

TEST(changed_program_reports_first_divergence) {
  test::TestHost capabilities;
  const RecordRunResult recorded = record_run("replay", replay_scenario, capabilities);
  CHECK(recorded.ok);
  const ReplayReport replay = replay_record(recorded.record, changed_scenario);
  CHECK(replay.ok);
  CHECK(!replay.identical);
  CHECK(replay.first_divergence.found);
  CHECK_EQ(replay.first_divergence.index, std::size_t(3));
  CHECK(replay.first_divergence.has_expected);
  CHECK(replay.first_divergence.has_actual);
  CHECK_EQ(replay.first_divergence.expected.type, EventType::Spawn);
  CHECK_EQ(replay.first_divergence.actual.type, EventType::Log);
  CHECK(!replay.first_divergence.expected_summary.empty());
  CHECK(!replay.first_divergence.actual_summary.empty());
}

TEST(incomplete_record_is_refused) {
  Record record = test::sample_record();
  record.complete = false;
  const ReplayReport replay = replay_record(record, replay_scenario);
  CHECK(!replay.ok);
  CHECK(replay.error.find("incomplete") != std::string::npos);
  CHECK(!replay.identical);
}

TEST(empty_record_is_refused) {
  Record record;
  record.complete = true;
  record.meta.scenario = "empty";
  record.recompute_hash();
  const ReplayReport replay = replay_record(record, replay_scenario);
  CHECK(!replay.ok);
}

TEST(replayed_capabilities_do_not_touch_the_os) {
  test::TempDir dir;
  CHECK(dir.write("manifest.txt", "release=2026"));

  RecordRunResult recorded;
  {
    SystemCapabilityOptions options;
    options.data_root = dir.path();
    SystemHost host(options);
    recorded = record_run("file", file_scenario, host);
  }
  CHECK(recorded.ok);

  // Delete the fixture: replay must answer from the record.
  std::error_code ec;
  CHECK(std::filesystem::remove(dir.path() + "/manifest.txt", ec));
  CHECK(!dir.exists("manifest.txt"));

  const ReplayReport replay = replay_record(recorded.record, file_scenario);
  CHECK(replay.ok);
  CHECK(replay.identical);
}

TEST(environment_is_answered_from_the_record) {
  test::TempDir dir;
  CHECK(dir.write("manifest.txt", "x"));
  RecordRunResult recorded;
  {
    SystemCapabilityOptions options;
    options.data_root = dir.path();
    SystemHost host(options);
    (void)::setenv("TEMPO_REPLAY_ENV", "recorded-value", 1);
    recorded = record_run("file", file_scenario, host);
    (void)::unsetenv("TEMPO_REPLAY_ENV");
  }
  CHECK(recorded.ok);
  CHECK(!::getenv("TEMPO_REPLAY_ENV"));
  const ReplayReport replay = replay_record(recorded.record, file_scenario);
  CHECK(replay.identical);
}

TEST(replayed_writes_do_not_touch_the_disk) {
  test::TempDir dir;
  RecordRunResult recorded;
  {
    SystemHost host({.data_root = dir.path()});
    recorded = record_run("write", write_scenario, host);
  }
  CHECK(recorded.ok);
  CHECK(dir.exists("scratch.txt"));
  std::error_code ec;
  CHECK(std::filesystem::remove(dir.path() + "/scratch.txt", ec));
  const ReplayReport replay = replay_record(recorded.record, write_scenario);
  CHECK(replay.ok);
  CHECK(replay.identical);
  CHECK(!dir.exists("scratch.txt"));
}

TEST(replay_many_rejects_zero_runs) {
  Record record = test::sample_record();
  const ReplayBatchReport batch = replay_many(record, replay_scenario, 0);
  CHECK(!batch.ok);
  CHECK_EQ(batch.runs, 0u);
}

TEST(replay_many_caps_runs) {
  test::TestHost capabilities;
  const RecordRunResult recorded = record_run("replay", replay_scenario, capabilities);
  CHECK(recorded.ok);
  const ReplayBatchReport batch =
      replay_many(recorded.record, replay_scenario, limits::kMaxReplayRuns + 50);
  CHECK(batch.ok);
  CHECK_EQ(batch.runs, limits::kMaxReplayRuns);
  CHECK(batch.all_identical);
}
