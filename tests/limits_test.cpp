#include "host_test.hpp"
#include "test_util.hpp"
#include "tempo/limits.hpp"
#include "tempo/record_run.hpp"
#include "tempo/runtime.hpp"

using namespace tempo;

namespace {

Task too_many_watches(Context ctx) {
  for (int i = 0; i < 70; ++i) {
    ctx.watch("var" + std::to_string(i), static_cast<std::int64_t>(i));
  }
  co_return;
}

Task oversized_watch(Context ctx) {
  ctx.watch("big", std::string(limits::kMaxWatchValue + 1, 'x'));
  co_return;
}

Task oversized_name(Context ctx) {
  ctx.watch(std::string(limits::kMaxWatchName + 1, 'n'), "x");
  co_return;
}

Task oversized_note(Context ctx) {
  ctx.note("big", std::string(limits::kMaxWatchValue + 1, 'y'));
  co_return;
}

Task long_log(Context ctx) {
  ctx.log(std::string(limits::kMaxLogBytes + 100, 'z'));
  co_return;
}

Task sleep_too_far(Context ctx) {
  co_await ctx.sleep_ns(limits::kMaxVirtualNs * 2);
}

Task task_limit_root(Context ctx) {
  int spawned = 0;
  for (int i = 0; i < 5; ++i) {
    try {
      ctx.spawn("child-" + std::to_string(i), [](Context) -> Task { co_return; });
      ++spawned;
    } catch (const LimitError&) {
      ctx.watch("spawn_failed", spawned);
      co_return;
    }
  }
  co_return;
}

}  // namespace

TEST(watch_limit_fails_the_task_cleanly) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("watches", too_many_watches);
  const RunSummary summary = scheduler.run();
  CHECK(!summary.ok);
  CHECK_EQ(summary.tasks_failed, 1u);
  bool message = false;
  for (const auto& event : host.events) {
    if (event.type == EventType::Error &&
        event.s1.find("too many watch") != std::string::npos) {
      message = true;
    }
  }
  CHECK(message);
}

TEST(watch_and_note_values_are_bounded) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("watch", oversized_watch);
  CHECK(!scheduler.run().ok);

  test::TestHost host2;
  Scheduler scheduler2(host2);
  scheduler2.spawn_root("name", oversized_name);
  CHECK(!scheduler2.run().ok);

  test::TestHost host3;
  Scheduler scheduler3(host3);
  scheduler3.spawn_root("note", oversized_note);
  CHECK(!scheduler3.run().ok);
}

TEST(long_logs_are_explicitly_truncated) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("log", long_log);
  CHECK(scheduler.run().ok);
  bool found = false;
  for (const auto& event : host.events) {
    if (event.type == EventType::Log) {
      found = true;
      CHECK(event.s1.size() <= limits::kMaxLogBytes + 32);
      CHECK(event.s1.find("truncated") != std::string::npos);
      CHECK_EQ(event.d, limits::kMaxLogBytes + 100);
    }
  }
  CHECK(found);
}

TEST(sleep_beyond_the_virtual_clock_limit_fails) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("sleep", sleep_too_far);
  const RunSummary summary = scheduler.run();
  CHECK(!summary.ok);
  CHECK_EQ(summary.tasks_failed, 1u);
}

TEST(task_limit_raises_a_catchable_error) {
  test::TestHost host;
  SchedulerOptions options;
  options.max_tasks = 3;
  Scheduler scheduler(host, options);
  scheduler.spawn_root("limit", task_limit_root);
  const RunSummary summary = scheduler.run();
  CHECK(summary.ok);
  CHECK_EQ(summary.tasks_spawned, 3u);
  bool saw = false;
  for (const auto& event : host.events) {
    if (event.type == EventType::Watch && event.s1 == "spawn_failed" && event.s2 == "2") {
      saw = true;
    }
  }
  CHECK(saw);
}

TEST(record_run_honours_the_event_stream_after_a_limit_failure) {
  test::TestHost host;
  const RecordRunResult result = record_run("watches", too_many_watches, host);
  CHECK(!result.ok);
  CHECK(!result.summary.ok);
  CHECK(!result.record.events.empty());
  CHECK_EQ(result.record.events.back().type, EventType::ScenarioEnd);
}
