#include "host_test.hpp"
#include "test_util.hpp"
#include "tempo/replay.hpp"
#include "tempo/record_run.hpp"
#include "tempo/scenario.hpp"

using namespace tempo;

TEST(registry_lists_builtins) {
  const ScenarioRegistry& registry = ScenarioRegistry::builtins();
  CHECK_EQ(registry.size(), std::size_t(3));
  CHECK(registry.find("counter-race") != nullptr);
  CHECK(registry.find("flaky-service") != nullptr);
  CHECK(registry.find("pipeline") != nullptr);
  CHECK(registry.find("missing") == nullptr);
  const std::vector<ScenarioInfo> list = registry.list();
  CHECK_EQ(list.size(), std::size_t(3));
  CHECK(!list[0].description.empty());
  CHECK(!list[1].fixtures.empty());
}

TEST(registry_rejects_duplicates_and_empty_entries) {
  ScenarioRegistry registry;
  ScenarioInfo info;
  info.name = "x";
  info.entry = [](Context ctx) -> Task {
    ctx.log("x");
    co_return;
  };
  registry.add(info);
  CHECK_THROWS(registry.add(info));
  ScenarioInfo empty;
  empty.name = "y";
  CHECK_THROWS(registry.add(empty));
}

TEST(counter_race_records_and_replays_deterministically) {
  const ScenarioInfo* scenario = ScenarioRegistry::builtins().find("counter-race");
  CHECK(scenario != nullptr);
  test::TestHost capabilities;
  RecordRunOptions options;
  options.seed = 7;
  const RecordRunResult recorded =
      record_run(scenario->name, scenario->entry, capabilities, options);
  CHECK(recorded.ok);
  CHECK(recorded.record.events.size() > 20);

  bool saw_fail = false;
  std::int64_t final_value = -1;
  for (const auto& event : recorded.record.events) {
    if (event.type == EventType::Note && event.s1 == "test") {
      saw_fail = event.s2.rfind("FAIL", 0) == 0 || event.s2.rfind("PASS", 0) == 0;
    }
    if (event.type == EventType::Watch && event.s1 == "final") {
      final_value = std::stoll(event.s2);
    }
  }
  CHECK(saw_fail);
  CHECK(final_value >= 6);
  CHECK(final_value <= 12);

  const ReplayBatchReport batch =
      replay_many(recorded.record, scenario->entry, 100);
  CHECK(batch.ok);
  CHECK(batch.all_identical);
  CHECK(batch.all_hashes_equal);
}

TEST(flaky_service_replays_fixture_and_environment_from_record) {
  const ScenarioInfo* scenario = ScenarioRegistry::builtins().find("flaky-service");
  CHECK(scenario != nullptr);
  test::TestHost capabilities;
  capabilities.files["fixtures/service.conf"] = scenario->fixtures[0].second;
  const RecordRunResult recorded =
      record_run(scenario->name, scenario->entry, capabilities);
  CHECK(recorded.ok);
  const ReplayReport replay = replay_record(recorded.record, scenario->entry);
  CHECK(replay.ok);
  CHECK(replay.identical);
  bool saw_config = false;
  bool saw_random = false;
  for (const auto& event : recorded.record.events) {
    if (event.type == EventType::FileRead) {
      saw_config = event.s1 == "fixtures/service.conf" && !event.bytes.empty();
    }
    if (event.type == EventType::Rng) {
      saw_random = true;
    }
  }
  CHECK(saw_config);
  CHECK(saw_random);
}

TEST(pipeline_records_three_lanes) {
  const ScenarioInfo* scenario = ScenarioRegistry::builtins().find("pipeline");
  CHECK(scenario != nullptr);
  test::TestHost capabilities;
  const RecordRunResult recorded =
      record_run(scenario->name, scenario->entry, capabilities);
  CHECK(recorded.ok);
  std::size_t spawns = 0;
  for (const auto& event : recorded.record.events) {
    if (event.type == EventType::Spawn) {
      ++spawns;
    }
  }
  CHECK_EQ(spawns, std::size_t(4));
  const ReplayReport replay = replay_record(recorded.record, scenario->entry);
  CHECK(replay.identical);
}

TEST(mutating_counter_race_shows_divergence) {
  const ScenarioInfo* scenario = ScenarioRegistry::builtins().find("counter-race");
  CHECK(scenario != nullptr);
  test::TestHost capabilities;
  RecordRunOptions options;
  options.seed = 11;
  const RecordRunResult recorded =
      record_run(scenario->name, scenario->entry, capabilities, options);
  CHECK(recorded.ok);

  ReplayOptions replay_options;
  replay_options.mutate_step = 1;
  replay_options.mutate_choice = 0;
  const ReplayReport replay =
      replay_record(recorded.record, scenario->entry, replay_options);
  CHECK(replay.ok);
  CHECK(replay.mutation_applied);
  CHECK(!replay.identical);
  CHECK(replay.first_divergence.found);
}
