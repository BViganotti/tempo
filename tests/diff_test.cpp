#include "host_test.hpp"
#include "record_test_util.hpp"
#include "test_util.hpp"
#include "tempo/diff.hpp"
#include "tempo/record_run.hpp"

using namespace tempo;

namespace {

Task diff_scenario(Context ctx) {
  const TaskId a = ctx.spawn("alpha", [](Context c) -> Task {
    for (int i = 0; i < 3; ++i) {
      c.watch("a", static_cast<std::int64_t>(i));
      co_await c.yield();
    }
  });
  const TaskId b = ctx.spawn("beta", [](Context c) -> Task {
    for (int i = 0; i < 3; ++i) {
      c.watch("b", static_cast<std::int64_t>(i));
      co_await c.yield();
    }
  });
  (void)co_await ctx.join(a);
  (void)co_await ctx.join(b);
}

}  // namespace

TEST(identical_records_diff_equal) {
  const Record record = test::sample_record();
  const RecordDiff diff = diff_records(record, record);
  CHECK(diff.equal);
  CHECK(!diff.first.found);
  CHECK_EQ(diff.common_prefix, record.events.size());
  CHECK_EQ(diff.a_hash, diff.b_hash);
  CHECK_EQ(diff.a_events, record.events.size());
  std::uint32_t total = 0;
  for (const auto& count : diff.counts) {
    CHECK_EQ(count.in_a, count.in_b);
    total += count.in_a;
  }
  CHECK_EQ(total, record.events.size());
}

TEST(different_schedule_reports_first_differing_event) {
  test::TestHost base_caps;
  const RecordRunResult base = record_run("race", diff_scenario, base_caps);
  CHECK(base.ok);
  test::TestHost mutated_caps;
  RecordRunOptions options;
  options.mutate_step = 1;
  const RecordRunResult mutated =
      record_run("race", diff_scenario, mutated_caps, options);
  CHECK(mutated.ok);
  CHECK(mutated.mutation_applied);

  const RecordDiff diff = diff_records(base.record, mutated.record);
  CHECK(!diff.equal);
  CHECK(diff.first.found);
  CHECK(diff.first.has_a);
  CHECK(diff.first.has_b);
  CHECK_EQ(diff.first.a.type, EventType::Step);
  CHECK_EQ(diff.first.b.type, EventType::Step);
  CHECK(diff.first.a.b != diff.first.b.b);
  CHECK(diff.first.summary_a.find("step") != std::string::npos);
  CHECK_EQ(diff.common_prefix, diff.first.index);
  CHECK(diff.a_hash != diff.b_hash);
  CHECK_EQ(diff.a_scenario, std::string("race"));
  CHECK_EQ(diff.b_scenario, std::string("race"));
}

TEST(shorter_record_reports_missing_tail) {
  Record short_record = test::sample_record();
  short_record.events.resize(3);
  short_record.recompute_hash();
  const Record full = test::sample_record();
  const RecordDiff diff = diff_records(full, short_record);
  CHECK(!diff.equal);
  CHECK(diff.first.found);
  CHECK(diff.first.has_a);
  CHECK(!diff.first.has_b);
  CHECK_EQ(diff.first.index, std::size_t(3));
  CHECK_EQ(diff.a_events, std::size_t(6));
  CHECK_EQ(diff.b_events, std::size_t(3));
}

TEST(different_event_counts_are_reported) {
  test::TestHost caps;
  const RecordRunResult base = record_run("race", diff_scenario, caps);
  CHECK(base.ok);
  const RecordDiff diff = diff_records(base.record, test::sample_record());
  CHECK(!diff.equal);
  CHECK(!diff.counts.empty());
  bool saw_step = false;
  for (const auto& count : diff.counts) {
    if (count.type == EventType::Step) {
      saw_step = true;
      CHECK(count.in_a > 0);
    }
  }
  CHECK(saw_step);
}
