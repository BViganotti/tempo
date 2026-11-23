#include "host_test.hpp"
#include "record_test_util.hpp"
#include "test_util.hpp"
#include "tempo/record_run.hpp"

using namespace tempo;

namespace {

Task recorded_scenario(Context ctx) {
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
  });
  const bool a_ok = co_await ctx.join(a);
  const bool b_ok = co_await ctx.join(b);
  ctx.note("join", a_ok && b_ok ? "both" : "failed");
}

Task reader_scenario(Context ctx) {
  const CapResult result = ctx.read_file("config.txt");
  ctx.watch("ok", result.ok);
  ctx.watch("content", result.data);
  ctx.log("read config");
  co_return;
}

Task spinner(Context ctx) {
  for (;;) {
    co_await ctx.yield();
  }
}

}  // namespace

TEST(record_run_captures_a_complete_run) {
  test::TestHost capabilities;
  RecordRunOptions options;
  options.seed = 1234;
  const RecordRunResult result =
      record_run("recorded", recorded_scenario, capabilities, options);
  CHECK(result.ok);
  CHECK(result.error.empty());
  CHECK(result.record.complete);
  CHECK_EQ(result.record.meta.scenario, std::string("recorded"));
  CHECK_EQ(result.record.meta.seed, 1234u);
  CHECK(result.record.meta.attributes.find("mutate_step") ==
        result.record.meta.attributes.end());
  CHECK(result.record.events.size() > 10);
  CHECK_EQ(result.record.events.front().type, EventType::ScenarioStart);
  CHECK_EQ(result.record.events.back().type, EventType::ScenarioEnd);
  CHECK_EQ(result.record.hash, result.record.compute_hash());
  CHECK_EQ(result.summary.tasks_spawned, 3u);
}

TEST(recorded_event_stream_roundtrips_through_the_container) {
  test::TestHost capabilities;
  const RecordRunResult result = record_run("recorded", recorded_scenario, capabilities);
  CHECK(result.ok);
  const SerializeOutcome serialized = serialize_record(result.record);
  CHECK(serialized.ok);
  const LoadOutcome loaded = load_record_bytes(serialized.bytes);
  CHECK(loaded.ok);
  CHECK_EQ(loaded.record.hash, result.record.hash);
  CHECK(test::same_events(loaded.record.events, result.record.events));
}

TEST(same_seed_records_identical_event_streams) {
  test::TestHost first;
  test::TestHost second;
  RecordRunOptions options;
  options.seed = 999;
  const RecordRunResult a = record_run("recorded", recorded_scenario, first, options);
  const RecordRunResult b = record_run("recorded", recorded_scenario, second, options);
  CHECK(a.ok);
  CHECK(b.ok);
  CHECK(test::same_events(a.record.events, b.record.events));
  CHECK_EQ(a.record.hash, b.record.hash);
}

TEST(different_seed_records_a_different_schedule) {
  test::TestHost first;
  test::TestHost second;
  RecordRunOptions a_options;
  a_options.seed = 1;
  RecordRunOptions b_options;
  b_options.seed = 0xABCDEF;
  const RecordRunResult a = record_run("recorded", recorded_scenario, first, a_options);
  const RecordRunResult b = record_run("recorded", recorded_scenario, second, b_options);
  CHECK(a.ok);
  CHECK(b.ok);
  bool different = a.record.events.size() != b.record.events.size();
  for (std::size_t i = 0; !different && i < a.record.events.size(); ++i) {
    if (!(a.record.events[i] == b.record.events[i])) {
      different = true;
    }
  }
  CHECK(different);
  CHECK(a.record.hash != b.record.hash);
}

TEST(mutation_changes_one_scheduling_decision) {
  test::TestHost base_host;
  const RecordRunResult base = record_run("recorded", recorded_scenario, base_host);
  CHECK(base.ok);

  test::TestHost mutated_host;
  RecordRunOptions options;
  options.mutate_step = 1;
  options.mutate_choice = 0;
  const RecordRunResult mutated =
      record_run("recorded", recorded_scenario, mutated_host, options);
  CHECK(mutated.ok);
  CHECK(mutated.mutation_applied);
  CHECK_EQ(mutated.record.meta.attributes.at("mutation_applied"), std::string("1"));
  CHECK(mutated.record.hash != base.record.hash);

  std::size_t first_difference = 0;
  const std::size_t limit = std::min(base.record.events.size(), mutated.record.events.size());
  while (first_difference < limit &&
         base.record.events[first_difference] == mutated.record.events[first_difference]) {
    ++first_difference;
  }
  CHECK(first_difference < limit);
  CHECK_EQ(base.record.events[first_difference].type, EventType::Step);
  CHECK_EQ(base.record.events[first_difference].a, std::uint64_t(1));
}

TEST(capability_results_are_part_of_the_record) {
  test::TestHost capabilities;
  capabilities.files["config.txt"] = "mode=deterministic";
  const RecordRunResult result = record_run("reader", reader_scenario, capabilities);
  CHECK(result.ok);
  const Event* read = nullptr;
  for (const auto& event : result.record.events) {
    if (event.type == EventType::FileRead) {
      read = &event;
    }
  }
  CHECK(read != nullptr);
  CHECK_EQ(read->s1, std::string("config.txt"));
  CHECK_EQ(read->bytes, std::string("mode=deterministic"));
  CHECK_EQ(read->a, 1u);
}

TEST(failed_run_is_still_a_complete_record) {
  test::TestHost capabilities;
  RecordRunOptions options;
  options.max_steps = 4;
  const RecordRunResult result = record_run("spinner", spinner, capabilities, options);
  CHECK(!result.ok);
  CHECK(result.summary.step_limit);
  CHECK(result.record.events.size() > 0);
  CHECK_EQ(result.record.events.back().type, EventType::ScenarioEnd);
  CHECK(result.record.complete);
}

TEST(capability_errors_are_recorded_not_thrown) {
  test::TestHost capabilities;
  const RecordRunResult result = record_run("reader", reader_scenario, capabilities);
  CHECK(result.ok);
  bool saw_failed_read = false;
  for (const auto& event : result.record.events) {
    if (event.type == EventType::FileRead && event.a == 0) {
      saw_failed_read = true;
      CHECK_EQ(event.s2, std::string("no such file"));
    }
  }
  CHECK(saw_failed_read);
}
