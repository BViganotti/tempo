#include "host_test.hpp"
#include "test_util.hpp"
#include "tempo/runtime.hpp"

using namespace tempo;

namespace {

Task racer(Context ctx, int rounds, std::int64_t step) {
  for (int i = 0; i < rounds; ++i) {
    co_await ctx.yield();
    ctx.watch("round", static_cast<std::int64_t>(i));
    co_await ctx.sleep_ms(static_cast<std::uint64_t>(1 + (i % 3)));
  }
  ctx.watch("done", step);
}

Task racing_root(Context ctx) {
  auto a = ctx.spawn("alpha", [](Context c) { return racer(c, 3, 2); });
  auto b = ctx.spawn("beta", [](Context c) { return racer(c, 3, 1); });
  const bool a_ok = co_await ctx.join(a);
  const bool b_ok = co_await ctx.join(b);
  ctx.note("join", a_ok && b_ok ? "both" : "failed");
}

Task sleep_pair(Context ctx) {
  const TaskId slow = ctx.spawn("slow", [](Context c) -> Task {
    co_await c.sleep_ms(10);
    c.note("woke", "slow");
  });
  const TaskId fast = ctx.spawn("fast", [](Context c) -> Task {
    co_await c.sleep_ms(5);
    c.note("woke", "fast");
  });
  co_await ctx.join(slow);
  co_await ctx.join(fast);
}

Task failing_child(Context ctx) {
  const TaskId child = ctx.spawn("boom", [](Context) -> Task {
    throw std::runtime_error("boom");
    co_return;
  });
  const bool ok = co_await ctx.join(child);
  ctx.note("child", ok ? "ok" : "failed");
}

Task random_sketch(Context ctx) {
  for (int i = 0; i < 3; ++i) {
    ctx.watch("r" + std::to_string(i), static_cast<std::int64_t>(ctx.random_u64() >> 32));
    co_await ctx.yield();
  }
}

Task clock_reader(Context ctx) {
  ctx.watch("t0", static_cast<std::int64_t>(ctx.now_ns()));
  co_await ctx.sleep_ms(2);
  ctx.watch("t1", static_cast<std::int64_t>(ctx.now_ns()));
}

Task named_child_root(Context ctx) {
  const TaskId child = ctx.spawn("named-child", [](Context c) -> Task {
    co_await c.yield();
  });
  (void)co_await ctx.join(child);
}

Task absolute_sleeper(Context ctx) {
  const std::uint64_t deadline = ctx.now_ns() + 3'000'000;
  ctx.watch("before", static_cast<std::int64_t>(ctx.now_ns()));
  co_await ctx.sleep_until_ns(deadline);
  ctx.watch("after", static_cast<std::int64_t>(ctx.now_ns()));
  ctx.note("deadline", std::to_string(deadline));
}

Task self_join(Context ctx) {
  (void)co_await ctx.join(ctx.id());
}

Task spinner(Context ctx) {
  for (;;) {
    co_await ctx.yield();
  }
}

}  // namespace

TEST(racing_tasks_record_spawns_steps_and_exits) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("racing", racing_root);
  const RunSummary summary = scheduler.run();
  CHECK(summary.ok);
  CHECK_EQ(summary.tasks_spawned, 3u);
  CHECK_EQ(host.count(EventType::Spawn), 3u);
  CHECK_EQ(host.count(EventType::Exit), 3u);
  CHECK(host.count(EventType::Step) > 10u);
  CHECK(host.count(EventType::Join) >= 1u);
  CHECK(host.count(EventType::Join) <= 2u);
  CHECK_EQ(host.find(EventType::Note)->s2, "both");
}

TEST(same_seed_replays_same_event_stream) {
  test::TestHost host_a;
  Scheduler a(host_a, {.seed = 12345});
  a.spawn_root("racing", racing_root);
  CHECK(a.run().ok);

  test::TestHost host_b;
  Scheduler b(host_b, {.seed = 12345});
  b.spawn_root("racing", racing_root);
  CHECK(b.run().ok);

  CHECK(host_a.events.size() == host_b.events.size());
  for (std::size_t i = 0; i < host_a.events.size(); ++i) {
    if (!(host_a.events[i] == host_b.events[i])) {
      CHECK(false);
      break;
    }
  }
}

TEST(different_seed_changes_schedule_but_not_outcome) {
  test::TestHost host_a;
  Scheduler a(host_a, {.seed = 1});
  a.spawn_root("racing", racing_root);
  CHECK(a.run().ok);

  test::TestHost host_b;
  Scheduler b(host_b, {.seed = 987654321});
  b.spawn_root("racing", racing_root);
  CHECK(b.run().ok);

  bool different = host_a.events.size() != host_b.events.size();
  for (std::size_t i = 0; !different && i < host_a.events.size(); ++i) {
    if (!(host_a.events[i] == host_b.events[i])) {
      different = true;
    }
  }
  CHECK(different);
  CHECK_EQ(host_a.find(EventType::Note)->s2, "both");
  CHECK_EQ(host_b.find(EventType::Note)->s2, "both");
}

TEST(sleeps_wake_in_deadline_order) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("sleep-pair", sleep_pair);
  const RunSummary summary = scheduler.run();
  CHECK(summary.ok);

  std::vector<std::uint64_t> jumps;
  for (const auto& event : host.events) {
    if (event.type == EventType::ClockJump) {
      jumps.push_back(event.a);
    }
  }
  CHECK_EQ(jumps.size(), 2u);
  CHECK(jumps[0] < jumps[1]);

  std::vector<std::string> wakes;
  for (const auto& event : host.events) {
    if (event.type == EventType::Note && event.s1 == "woke") {
      wakes.push_back(event.s2);
    }
  }
  CHECK_EQ(wakes.size(), 2u);
  CHECK_EQ(wakes[0], std::string("fast"));
  CHECK_EQ(wakes[1], std::string("slow"));
}

TEST(join_sees_failed_child) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("failing", failing_child);
  const RunSummary summary = scheduler.run();
  CHECK(!summary.ok);
  CHECK_EQ(summary.tasks_failed, 1u);
  const Event* note = host.find(EventType::Note);
  CHECK(note != nullptr);
  CHECK_EQ(note->s2, "failed");
  bool saw_exit_failure = false;
  for (const auto& event : host.events) {
    if (event.type == EventType::Exit && event.task == 2 && event.b == 0) {
      saw_exit_failure = true;
    }
  }
  CHECK(saw_exit_failure);
}

TEST(death_of_child_wakes_joiners) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("failing", failing_child);
  (void)scheduler.run();
  CHECK_EQ(host.count(EventType::Wake), 1u);
  CHECK_EQ(host.find(EventType::Wake)->s1, std::string("join"));
}

TEST(random_draws_are_deterministic_per_seed) {
  test::TestHost host_a;
  Scheduler a(host_a, {.seed = 777});
  a.spawn_root("random", random_sketch);
  CHECK(a.run().ok);

  test::TestHost host_b;
  Scheduler b(host_b, {.seed = 777});
  b.spawn_root("random", random_sketch);
  CHECK(b.run().ok);

  CHECK_EQ(host_a.count(EventType::Rng), 3u);
  std::vector<std::uint64_t> values_a;
  std::vector<std::uint64_t> values_b;
  for (const auto& event : host_a.events) {
    if (event.type == EventType::Rng) {
      values_a.push_back(event.a);
    }
  }
  for (const auto& event : host_b.events) {
    if (event.type == EventType::Rng) {
      values_b.push_back(event.a);
    }
  }
  CHECK(values_a == values_b);
}

TEST(virtual_clock_advances_only_on_sleep) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("clock", clock_reader);
  CHECK(scheduler.run().ok);
  std::uint64_t t0 = 0;
  std::uint64_t t1 = 0;
  for (const auto& event : host.events) {
    if (event.type == EventType::Watch && event.s1 == "t0") {
      t0 = std::stoull(event.s2);
    }
    if (event.type == EventType::Watch && event.s1 == "t1") {
      t1 = std::stoull(event.s2);
    }
  }
  CHECK_EQ(t1 - t0, std::uint64_t(2'000'000));
}

TEST(scheduler_task_names_and_counts) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("root-name", named_child_root);
  CHECK(scheduler.run().ok);
  CHECK_EQ(scheduler.task_name(1), std::string("root-name"));
  CHECK_EQ(scheduler.task_name(2), std::string("named-child"));
  CHECK_EQ(scheduler.task_name(99), std::string());
  CHECK_EQ(scheduler.task_count(), 2u);
  CHECK(scheduler.state_of(1) == TaskState::Done);
}

TEST(absolute_sleep_uses_the_virtual_deadline) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("absolute", absolute_sleeper);
  CHECK(scheduler.run().ok);
  std::uint64_t before = 0;
  std::uint64_t after = 0;
  std::uint64_t deadline = 0;
  for (const auto& event : host.events) {
    if (event.type == EventType::Watch && event.s1 == "before") {
      before = std::stoull(event.s2);
    }
    if (event.type == EventType::Watch && event.s1 == "after") {
      after = std::stoull(event.s2);
    }
    if (event.type == EventType::Note && event.s1 == "deadline") {
      deadline = std::stoull(event.s2);
    }
  }
  CHECK_EQ(after, deadline);
  CHECK_EQ(after - before, std::uint64_t(3'000'000));
}

TEST(self_join_deadlocks_cleanly) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("self-join", self_join);
  const RunSummary summary = scheduler.run();
  CHECK(!summary.ok);
  CHECK(summary.deadlock);
  CHECK(summary.error.find("deadlock") != std::string::npos);
}

TEST(step_limit_stops_spinner) {
  test::TestHost host;
  SchedulerOptions options;
  options.seed = 3;
  options.max_steps = 5;
  Scheduler scheduler(host, options);
  scheduler.spawn_root("spinner", spinner);
  const RunSummary summary = scheduler.run();
  CHECK(!summary.ok);
  CHECK(summary.step_limit);
  CHECK_EQ(summary.steps, 5u);
  CHECK_EQ(host.count(EventType::Step), 5u);
}

TEST(invalid_host_choice_is_reported) {
  test::TestHost host;
  host.forced_choice = 999;
  Scheduler scheduler(host);
  scheduler.spawn_root("racing", racing_root);
  const RunSummary summary = scheduler.run();
  CHECK(!summary.ok);
  CHECK(summary.host_error);
  CHECK_EQ(host.count(EventType::Step), 0u);
}

TEST(step_events_carry_time_and_draw) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("racing", racing_root);
  (void)scheduler.run();
  const Event* step = host.find(EventType::Step);
  CHECK(step != nullptr);
  CHECK_EQ(step->a, 0u);
  CHECK_EQ(step->b, step->task);
  CHECK(step->d > 0);
  CHECK(step->s1 == "racing");
}
