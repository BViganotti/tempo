#include <memory>

#include "host_test.hpp"
#include "test_util.hpp"
#include "tempo/debug.hpp"
#include "tempo/record_run.hpp"

using namespace tempo;

namespace {

Task break_scenario(Context ctx) {
  const TaskId child = ctx.spawn("reader", [](Context c) -> Task {
    c.watch("status", "opening");
    const CapResult read = c.read_file("config.txt");
    c.watch("status", read.ok ? "loaded" : "failed");
    c.watch("payload", read.data);
    co_await c.sleep_ms(1);
    c.watch("status", "closed");
  });
  (void)co_await ctx.join(child);
}

std::shared_ptr<const Record> make_record() {
  test::TestHost caps;
  caps.files["config.txt"] = "answer=42";
  RecordRunResult result = record_run("breakable", break_scenario, caps);
  return std::make_shared<const Record>(std::move(result.record));
}

std::size_t first_index_of(const Record& record, EventType type) {
  for (std::size_t i = 0; i < record.events.size(); ++i) {
    if (record.events[i].type == type) {
      return i;
    }
  }
  return record.events.size();
}

}  // namespace

TEST(breakpoint_by_event_type) {
  const auto record = make_record();
  DebugSession session(record);
  Breakpoint bp;
  bp.has_type = true;
  bp.type = EventType::FileRead;
  const auto id = session.add_breakpoint(bp);
  CHECK(id.has_value());
  CHECK_EQ(session.breakpoints().size(), std::size_t(1));
  const auto hit = session.next_breakpoint(0, true);
  CHECK(hit.has_value());
  CHECK_EQ(*hit, first_index_of(*record, EventType::FileRead));
  CHECK_EQ(record->events[*hit].type, EventType::FileRead);
  // Running forward again from the hit skips it.
  const auto next = session.next_breakpoint(*hit, true);
  CHECK(!next.has_value() || record->events[*next].type == EventType::FileRead);
}

TEST(breakpoint_by_task_and_text) {
  const auto record = make_record();
  DebugSession session(record);
  Breakpoint bp;
  bp.has_task = true;
  bp.task = 2;
  bp.has_text = true;
  bp.text = "loaded";
  CHECK(session.add_breakpoint(bp).has_value());
  const auto hit = session.next_breakpoint(0, true);
  CHECK(hit.has_value());
  CHECK_EQ(record->events[*hit].type, EventType::Watch);
  CHECK_EQ(record->events[*hit].task, 2u);
  CHECK_EQ(record->events[*hit].s2, std::string("loaded"));
}

TEST(text_matches_event_type_names_case_insensitively) {
  const auto record = make_record();
  DebugSession session(record);
  Breakpoint bp;
  bp.has_text = true;
  bp.text = "fileread";
  CHECK(session.add_breakpoint(bp).has_value());
  const auto hit = session.next_breakpoint(0, true);
  CHECK(hit.has_value());
  CHECK_EQ(record->events[*hit].type, EventType::FileRead);
}

TEST(disabled_breakpoints_are_skipped) {
  const auto record = make_record();
  DebugSession session(record);
  Breakpoint bp;
  bp.has_type = true;
  bp.type = EventType::FileRead;
  bp.enabled = false;
  const auto id = session.add_breakpoint(bp);
  CHECK(id.has_value());
  CHECK(!session.next_breakpoint(0, true).has_value());
  CHECK(session.set_breakpoint_enabled(*id, true));
  CHECK(session.next_breakpoint(0, true).has_value());
}

TEST(backward_search_finds_earlier_breakpoint) {
  const auto record = make_record();
  DebugSession session(record);
  Breakpoint bp;
  bp.has_type = true;
  bp.type = EventType::Watch;
  CHECK(session.add_breakpoint(bp).has_value());
  const auto last = record->events.size() - 1;
  const auto hit = session.next_breakpoint(last, false);
  CHECK(hit.has_value());
  CHECK(*hit < last);
  CHECK_EQ(record->events[*hit].type, EventType::Watch);
  const auto earlier = session.next_breakpoint(*hit, false);
  CHECK(!earlier.has_value() || *earlier < *hit);
}

TEST(breakpoints_can_be_removed_and_cleared) {
  const auto record = make_record();
  DebugSession session(record);
  Breakpoint bp;
  bp.has_type = true;
  bp.type = EventType::Exit;
  const auto first = session.add_breakpoint(bp);
  CHECK(first.has_value());
  CHECK(session.remove_breakpoint(*first));
  CHECK(!session.remove_breakpoint(*first));
  CHECK_EQ(session.breakpoints().size(), std::size_t(0));
  bp.type = EventType::Step;
  CHECK(session.add_breakpoint(bp).has_value());
  session.clear_breakpoints();
  CHECK(session.breakpoints().empty());
}

TEST(breakpoint_ids_are_unique_and_the_limit_is_enforced) {
  const auto record = make_record();
  DebugSession session(record);
  Breakpoint bp;
  bp.has_type = true;
  bp.type = EventType::Step;
  for (std::size_t i = 0; i < limits::kMaxBreakpoints; ++i) {
    const auto id = session.add_breakpoint(bp);
    CHECK(id.has_value());
    CHECK_EQ(*id, static_cast<std::uint32_t>(i + 1));
  }
  CHECK(!session.add_breakpoint(bp).has_value());
}

TEST(no_breakpoints_means_no_stop) {
  const auto record = make_record();
  DebugSession session(record);
  CHECK(!session.next_breakpoint(0, true).has_value());
  CHECK(!session.next_breakpoint(record->events.size() - 1, false).has_value());
}

TEST(matches_combines_all_criteria) {
  const auto record = make_record();
  DebugSession session(record);
  Breakpoint bp;
  bp.has_type = true;
  bp.type = EventType::Watch;
  bp.has_task = true;
  bp.task = 2;
  bp.has_text = true;
  bp.text = "status";
  CHECK(session.add_breakpoint(bp).has_value());
  std::size_t matches = 0;
  for (const auto& event : record->events) {
    if (session.matches(event)) {
      ++matches;
      CHECK_EQ(event.type, EventType::Watch);
      CHECK_EQ(event.task, 2u);
      CHECK_EQ(event.s1, std::string("status"));
    }
  }
  CHECK_EQ(matches, std::size_t(3));
}
