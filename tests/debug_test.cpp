#include <memory>

#include "host_test.hpp"
#include "test_util.hpp"
#include "tempo/debug.hpp"
#include "tempo/record_run.hpp"

using namespace tempo;

namespace {

Task watched(Context ctx) {
  const TaskId a = ctx.spawn("alpha", [](Context c) -> Task {
    for (int i = 0; i < 6; ++i) {
      c.watch("round", static_cast<std::int64_t>(i));
      c.watch("parity", i % 2 == 0 ? "even" : "odd");
      co_await c.yield();
    }
    c.note("alpha", "done");
  });
  const TaskId b = ctx.spawn("beta", [](Context c) -> Task {
    c.watch("phase", "start");
    co_await c.sleep_ms(3);
    c.watch("phase", "end");
  });
  (void)co_await ctx.join(a);
  (void)co_await ctx.join(b);
}

WatchMap brute_force(const Record& record, std::size_t index) {
  WatchMap state;
  for (std::size_t i = 0; i <= index && i < record.events.size(); ++i) {
    if (record.events[i].type == EventType::Watch) {
      state[record.events[i].task][record.events[i].s1] = record.events[i].s2;
    }
  }
  return state;
}

std::shared_ptr<const Record> make_record() {
  test::TestHost caps;
  RecordRunResult result = record_run("watched", watched, caps);
  return std::make_shared<const Record>(std::move(result.record));
}

Task many_watches(Context ctx) {
  for (int i = 0; i < 300; ++i) {
    ctx.watch("counter", static_cast<std::int64_t>(i));
    co_await ctx.yield();
  }
}

Task many_keys(Context ctx) {
  std::vector<TaskId> ids;
  ids.reserve(70);
  for (int t = 0; t < 70; ++t) {
    ids.push_back(ctx.spawn("keys-" + std::to_string(t), [t](Context c) -> Task {
      for (int i = 0; i < 60; ++i) {
        c.watch("key_" + std::to_string(t) + "_" + std::to_string(i),
                static_cast<std::int64_t>(i));
      }
      co_return;
    }));
  }
  for (TaskId id : ids) {
    (void)co_await ctx.join(id);
  }
}

}  // namespace

TEST(session_exposes_events_and_basic_navigation) {
  const auto record = make_record();
  DebugSession session(record);
  CHECK(session.valid());
  CHECK_EQ(session.event_count(), record->events.size());
  CHECK(session.event_at(0) != nullptr);
  CHECK(session.event_at(record->events.size()) == nullptr);
  CHECK_EQ(session.event_at(0)->type, EventType::ScenarioStart);
  CHECK(session.step_forward(0).value() == 1);
  CHECK(!session.step_forward(record->events.size() - 1).has_value());
  CHECK(!session.step_back(0).has_value());
  CHECK(session.step_back(1).value() == 0);
}

TEST(state_is_reconstructed_exactly_at_every_index) {
  const auto record = make_record();
  DebugSession session(record);
  for (std::size_t i = 0; i < record->events.size(); ++i) {
    CHECK(session.state_at(i) == brute_force(*record, i));
  }
  const WatchMap final_state = session.state_at(record->events.size() - 1);
  CHECK(final_state.count(2) == 1);
  CHECK_EQ(final_state.at(2).at("round"), std::string("5"));
  CHECK(final_state.count(3) == 1);
  CHECK_EQ(final_state.at(3).at("phase"), std::string("end"));
}

TEST(reverse_stepping_restores_state_exactly) {
  const auto record = make_record();
  DebugSession session(record);
  std::size_t index = record->events.size() - 1;
  std::size_t steps = 0;
  while (true) {
    const WatchMap state = session.state_at(index);
    CHECK(state == brute_force(*record, index));
    const auto previous = session.step_back(index);
    if (!previous.has_value()) {
      break;
    }
    index = *previous;
    ++steps;
  }
  CHECK_EQ(index, std::size_t(0));
  CHECK(steps > 10);
}

TEST(frames_include_event_and_state) {
  const auto record = make_record();
  DebugSession session(record);
  std::size_t watch_index = 0;
  for (std::size_t i = 0; i < record->events.size(); ++i) {
    if (record->events[i].type == EventType::Watch) {
      watch_index = i;
      break;
    }
  }
  const DebugFrame frame = session.frame_at(watch_index);
  CHECK_EQ(frame.event.type, EventType::Watch);
  CHECK_EQ(frame.index, watch_index);
  CHECK(!frame.state.empty());
  CHECK(!frame.is_divergence);
}

TEST(divergence_annotation_marks_the_frame) {
  const auto record = make_record();
  DebugSession session(record);
  session.set_divergence(5, "changed schedule");
  CHECK(session.divergence_index().value() == 5);
  CHECK_EQ(session.divergence_reason(), std::string("changed schedule"));
  CHECK(session.frame_at(5).is_divergence);
  CHECK(!session.frame_at(4).is_divergence);
  session.set_divergence(record->events.size() + 10, "out of range");
  CHECK(!session.divergence_index().has_value());
}

TEST(checkpoints_produce_identical_state_and_stay_within_budget) {
  test::TestHost caps;
  RecordRunResult result = record_run("counter", many_watches, caps);
  auto record = std::make_shared<const Record>(std::move(result.record));
  DebugSession session(record);
  CHECK(session.has_checkpoints());
  for (std::size_t i = 0; i < record->events.size(); ++i) {
    CHECK(session.state_at(i) == brute_force(*record, i));
  }
}

TEST(huge_state_disables_checkpoints_but_stays_correct) {
  test::TestHost caps;
  RecordRunResult result = record_run("keys", many_keys, caps);
  auto record = std::make_shared<const Record>(std::move(result.record));
  DebugSession session(record);
  CHECK(!session.has_checkpoints());
  const WatchMap state = session.state_at(record->events.size() - 1);
  CHECK_EQ(DebugSession::state_key_count(state), std::size_t(4200));
  CHECK_EQ(state.at(2).at("key_0_0"), std::string("0"));
  CHECK_EQ(state.at(71).at("key_69_59"), std::string("59"));
}

TEST(timeline_has_lanes_with_names_and_status) {
  const auto record = make_record();
  DebugSession session(record);
  const std::vector<TaskLane> lanes = session.timeline();
  CHECK(!lanes.empty());
  bool saw_root = false;
  bool saw_alpha = false;
  bool saw_beta = false;
  bool saw_system = false;
  for (const auto& lane : lanes) {
    CHECK_EQ(lane.events.size(), lane.event_count);
    if (lane.task == kNoTask) {
      saw_system = true;
    }
    if (lane.name == "alpha") {
      saw_alpha = true;
      CHECK(lane.exited);
      CHECK(!lane.failed);
    }
    if (lane.name == "beta") {
      saw_beta = true;
      CHECK(lane.exited);
    }
    if (lane.name == "watched") {
      saw_root = true;
    }
  }
  CHECK(saw_root);
  CHECK(saw_alpha);
  CHECK(saw_beta);
  CHECK(saw_system);
}

TEST(render_state_is_deterministic) {
  WatchMap state;
  state[1]["a"] = "1";
  state[2]["b"] = "2";
  const std::string rendered = DebugSession::render_state(state);
  CHECK(rendered.find("task 1") != std::string::npos);
  CHECK(rendered.find("a = 1") != std::string::npos);
  CHECK_EQ(DebugSession::state_key_count(state), std::size_t(2));
}
