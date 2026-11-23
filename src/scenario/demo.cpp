#include "scenario/demo.hpp"

#include <memory>
#include <string>

namespace tempo::scenarios {
namespace {

struct CounterState {
  std::int64_t value = 0;
};

Task counter_worker(Context ctx, std::shared_ptr<CounterState> state, std::int64_t amount,
                    const char* name) {
  for (int i = 0; i < 6; ++i) {
    const std::int64_t local = state->value;
    ctx.watch("local_read", local);
    co_await ctx.yield();
    state->value = local + amount;
    ctx.watch("counter", state->value);
    ctx.log(std::string(name) + " wrote counter");
    co_await ctx.sleep_ms(static_cast<std::uint64_t>(1 + (i % 2)));
  }
  ctx.note("worker", name);
}

Task flaky_reader(Context ctx, std::shared_ptr<std::string> shared) {
  const CapResult config = ctx.read_file("fixtures/service.conf");
  ctx.watch("config_ok", config.ok);
  ctx.watch("config_size", static_cast<std::int64_t>(config.data.size()));
  if (config.ok) {
    ctx.log("config loaded: " + std::to_string(config.data.size()) + " bytes");
  } else {
    ctx.log("config missing: " + config.error);
  }

  const CapResult mode = ctx.env_get("TEMPO_SERVICE_MODE");
  ctx.watch("mode", mode.ok ? mode.data : std::string("unset"));

  const std::uint64_t draw = ctx.random_u64();
  ctx.watch("draw", static_cast<std::int64_t>(draw >> 60));

  *shared = "reader-start";
  ctx.watch("shared", *shared);
  co_await ctx.sleep_ms(2);
  *shared += "+reader-end";
  ctx.watch("shared", *shared);
}

Task flaky_writer(Context ctx, std::shared_ptr<std::string> shared) {
  co_await ctx.sleep_ms(1);
  *shared += "+writer";
  ctx.watch("shared", *shared);
  ctx.note("writer", "done");
}

Task pipeline_worker(Context ctx, const char* name, std::uint64_t start_ms,
                     std::uint64_t work_ms) {
  co_await ctx.sleep_ms(start_ms);
  ctx.watch("stage", "working");
  ctx.log(std::string(name) + " started");
  for (int i = 0; i < 3; ++i) {
    co_await ctx.yield();
    ctx.watch("progress", static_cast<std::int64_t>(i + 1));
  }
  co_await ctx.sleep_ms(work_ms);
  ctx.watch("stage", "done");
  ctx.note("stage", name);
}

}  // namespace

Task counter_race(Context ctx) {
  auto state = std::make_shared<CounterState>();
  ctx.watch("expected", 12);
  const TaskId alpha = ctx.spawn(
      "alpha", [state](Context c) { return counter_worker(c, state, 1, "alpha"); });
  const TaskId beta = ctx.spawn(
      "beta", [state](Context c) { return counter_worker(c, state, 1, "beta"); });
  const bool a_ok = co_await ctx.join(alpha);
  const bool b_ok = co_await ctx.join(beta);
  ctx.watch("counter", state->value);
  ctx.watch("final", state->value);
  const bool passed = state->value == 12;
  ctx.note("test", passed ? "PASS expected 12" : "FAIL expected 12");
  ctx.log(std::string("counter test ") + (passed ? "passed" : "FAILED") + " with value " +
          std::to_string(state->value));
  ctx.watch("joined", a_ok && b_ok);
}

Task flaky_service(Context ctx) {
  auto shared = std::make_shared<std::string>();
  const TaskId reader = ctx.spawn(
      "reader", [shared](Context c) { return flaky_reader(c, shared); });
  const TaskId writer = ctx.spawn(
      "writer", [shared](Context c) { return flaky_writer(c, shared); });
  const bool r_ok = co_await ctx.join(reader);
  const bool w_ok = co_await ctx.join(writer);
  ctx.watch("shared_final", *shared);
  ctx.note("shared_final", *shared);
  ctx.watch("joined", r_ok && w_ok);
}

Task pipeline(Context ctx) {
  const TaskId intake = ctx.spawn("intake",
                                  [](Context c) { return pipeline_worker(c, "intake", 0, 3); });
  const TaskId transform = ctx.spawn(
      "transform", [](Context c) { return pipeline_worker(c, "transform", 2, 2); });
  const TaskId publish = ctx.spawn(
      "publish", [](Context c) { return pipeline_worker(c, "publish", 5, 1); });
  const bool a = co_await ctx.join(intake);
  const bool b = co_await ctx.join(transform);
  const bool c2 = co_await ctx.join(publish);
  ctx.note("pipeline", a && b && c2 ? "complete" : "degraded");
  ctx.log("pipeline finished");
}

}  // namespace tempo::scenarios
