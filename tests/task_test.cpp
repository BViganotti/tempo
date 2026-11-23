#include "host_test.hpp"
#include "test_util.hpp"
#include "tempo/runtime.hpp"

using namespace tempo;

namespace {

Task one_yield(Context ctx) {
  ctx.watch("phase", "start");
  co_await ctx.yield();
  ctx.watch("phase", "end");
}

Task immediate(Context ctx) {
  ctx.watch("ran", true);
  co_return;
}

Task bad_awaitable(Context ctx) {
  ctx.spawn("bad", [](Context) -> Task { co_await std::suspend_always{}; });
  co_await ctx.yield();
}

}  // namespace

TEST(task_does_not_start_before_run) {
  test::TestHost host;
  Scheduler scheduler(host);
  const TaskId id = scheduler.spawn_root("one", one_yield);
  CHECK_EQ(id, 1u);
  CHECK_EQ(host.events.size(), 2u);  // ScenarioStart + Spawn
  CHECK_EQ(host.events[0].type, EventType::ScenarioStart);
  CHECK_EQ(host.events[1].type, EventType::Spawn);
  CHECK_EQ(host.count(EventType::Step), 0u);

  const RunSummary summary = scheduler.run();
  CHECK(summary.ok);
  CHECK_EQ(summary.steps, 2u);
  CHECK_EQ(summary.tasks_spawned, 1u);
  CHECK_EQ(host.count(EventType::Step), 2u);
  CHECK_EQ(host.count(EventType::Yield), 1u);
  CHECK_EQ(host.count(EventType::Exit), 1u);
  CHECK_EQ(host.events.back().type, EventType::ScenarioEnd);
  CHECK_EQ(host.count(EventType::Watch), 2u);
}

TEST(task_completes_without_suspending) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("immediate", immediate);
  const RunSummary summary = scheduler.run();
  CHECK(summary.ok);
  CHECK_EQ(summary.steps, 1u);
  CHECK_EQ(host.count(EventType::Exit), 1u);
  const Event* exit = host.find(EventType::Exit);
  CHECK(exit != nullptr);
  CHECK_EQ(exit->b, 1u);
}

TEST(task_ids_are_sequential_and_names_recorded) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("root", immediate);
  const RunSummary summary = scheduler.run();
  CHECK(summary.ok);
  const Event* spawn = host.find(EventType::Spawn);
  CHECK(spawn != nullptr);
  CHECK_EQ(spawn->s1, "root");
  CHECK_EQ(spawn->b, 1u);
  CHECK_EQ(spawn->a, kNoTask);
}

TEST(watch_only_records_changes) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("immediate", immediate);
  CHECK(scheduler.run().ok);
  CHECK_EQ(host.count(EventType::Watch), 1u);
  CHECK_EQ(host.find(EventType::Watch)->s1, "ran");
  CHECK_EQ(host.find(EventType::Watch)->s2, "true");
}

TEST(no_root_reports_error) {
  test::TestHost host;
  Scheduler scheduler(host);
  const RunSummary summary = scheduler.run();
  CHECK(!summary.ok);
  CHECK_EQ(summary.error, std::string("no root task"));
}

TEST(spawn_root_twice_throws) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("root", immediate);
  bool threw = false;
  try {
    scheduler.spawn_root("again", immediate);
  } catch (const LimitError&) {
    threw = true;
  }
  CHECK(threw);
}

TEST(suspended_outside_tempo_awaitable_is_detected) {
  test::TestHost host2;
  Scheduler scheduler2(host2);
  scheduler2.spawn_root("root", bad_awaitable);
  const RunSummary summary = scheduler2.run();
  CHECK(!summary.ok);
  CHECK_EQ(summary.tasks_failed, 1u);
  bool found = false;
  for (const auto& event : host2.events) {
    if (event.type == EventType::Error &&
        event.s1 == "task suspended outside a Tempo awaitable") {
      found = true;
    }
  }
  CHECK(found);
}
