#include "host_test.hpp"
#include "test_util.hpp"
#include "tempo/runtime.hpp"

using namespace tempo;

namespace {

Task victim(Context ctx) {
  try {
    co_await ctx.sleep_ms(1000);
    ctx.watch("survived", true);
  } catch (const CancelledError&) {
    ctx.watch("caught", true);
    throw;
  }
}

Task cancelling_root(Context ctx) {
  const TaskId id = ctx.spawn("victim", victim);
  co_await ctx.sleep_ms(10);
  ctx.cancel(id);
  const bool joined = co_await ctx.join(id);
  ctx.watch("joined_ok", joined);
}

Task ready_victim(Context ctx) {
  for (;;) {
    co_await ctx.yield();
  }
}

Task ready_cancel_root(Context ctx) {
  const TaskId id = ctx.spawn("spinner", ready_victim);
  co_await ctx.yield();
  co_await ctx.yield();
  ctx.cancel(id);
  const bool joined = co_await ctx.join(id);
  ctx.note("joined", joined ? "ok" : "cancelled");
}

Task self_cancelling(Context ctx) {
  ctx.cancel(ctx.id());
  co_await ctx.yield();
  ctx.watch("unreachable", true);
}

Task checked(Context ctx) {
  ctx.cancel(ctx.id());
  try {
    ctx.check_cancelled();
  } catch (const CancelledError&) {
    ctx.watch("checked", true);
  }
  co_return;
}

Task cancel_waiting(Context ctx) {
  const TaskId sleeper = ctx.spawn("sleeper", [](Context c) -> Task {
    co_await c.sleep_ms(50);
  });
  const TaskId waiter = ctx.spawn("waiter", [sleeper](Context c) -> Task {
    (void)co_await c.join(sleeper);
  });
  co_await ctx.yield();
  ctx.cancel(waiter);
  const bool ok = co_await ctx.join(sleeper);
  ctx.watch("sleeper_ok", ok);
}

}  // namespace

TEST(cancel_wakes_sleeper_and_join_reports_failure) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("cancel-root", cancelling_root);
  const RunSummary summary = scheduler.run();
  CHECK(summary.ok);
  CHECK_EQ(summary.tasks_failed, 0u);
  CHECK_EQ(host.count(EventType::Cancel), 1u);
  CHECK_EQ(host.count(EventType::ClockJump), 1u);
  bool caught = false;
  for (const auto& event : host.events) {
    if (event.type == EventType::Watch && event.s1 == "caught" && event.s2 == "true") {
      caught = true;
    }
    if (event.type == EventType::Watch && event.s1 == "survived") {
      CHECK(false);
    }
  }
  CHECK(caught);
  std::uint64_t exit_ok = 1;
  for (const auto& event : host.events) {
    if (event.type == EventType::Exit && event.task == 2) {
      exit_ok = event.b;
    }
  }
  CHECK_EQ(exit_ok, 0u);
}

TEST(cancel_ready_spinner) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("ready-cancel", ready_cancel_root);
  const RunSummary summary = scheduler.run();
  CHECK(summary.ok);
  const Event* note = host.find(EventType::Note);
  CHECK(note != nullptr);
  CHECK_EQ(note->s2, "cancelled");
}

TEST(self_cancel_unwinds_at_next_await) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("self", self_cancelling);
  const RunSummary summary = scheduler.run();
  CHECK(summary.ok);
  for (const auto& event : host.events) {
    if (event.type == EventType::Watch) {
      CHECK(event.s1 != "unreachable");
    }
  }
}

TEST(check_cancelled_throws) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("checked", checked);
  CHECK(scheduler.run().ok);
  bool checked_watch = false;
  for (const auto& event : host.events) {
    if (event.type == EventType::Watch && event.s1 == "checked") {
      checked_watch = true;
    }
  }
  CHECK(checked_watch);
}

TEST(cancel_waiter_removes_it_from_target_joiners) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("cancel-waiting", cancel_waiting);
  const RunSummary summary = scheduler.run();
  CHECK(summary.ok);
  CHECK_EQ(summary.tasks_failed, 0u);
  bool sleeper_ok = false;
  for (const auto& event : host.events) {
    if (event.type == EventType::Watch && event.s1 == "sleeper_ok" && event.s2 == "true") {
      sleeper_ok = true;
    }
  }
  CHECK(sleeper_ok);
}
