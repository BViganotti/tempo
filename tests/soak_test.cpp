#include <memory>
#include <string>

#include "host_test.hpp"
#include "temp_dir.hpp"
#include "test_util.hpp"
#include "tempo/record_run.hpp"
#include "tempo/replay.hpp"
#include "tempo/workbench.hpp"

using namespace tempo;

namespace {

Task soak_scenario(Context ctx) {
  const TaskId a = ctx.spawn("alpha", [](Context c) -> Task {
    for (int i = 0; i < 4; ++i) {
      c.watch("a", static_cast<std::int64_t>(i));
      co_await c.yield();
    }
  });
  const TaskId b = ctx.spawn("beta", [](Context c) -> Task {
    for (int i = 0; i < 4; ++i) {
      c.watch("b", static_cast<std::int64_t>(i));
      co_await c.sleep_ms(1);
    }
  });
  (void)co_await ctx.join(a);
  (void)co_await ctx.join(b);
}

HttpResponse call(Workbench& workbench, const std::string& method, const std::string& path,
                  const std::string& body = {}) {
  HttpRequest request;
  request.method = method;
  request.path = path;
  request.body = body;
  request.version = "HTTP/1.1";
  request.headers["host"] = "127.0.0.1:1";
  return workbench.handle(request);
}

}  // namespace

TEST(soak_replay_and_record_roundtrips) {
  test::TestHost host;
  RecordRunOptions options;
  options.seed = 123;
  const RecordRunResult recorded = record_run("soak", soak_scenario, host, options);
  CHECK(recorded.ok);
  auto record = std::make_shared<const Record>(recorded.record);
  for (int i = 0; i < 500; ++i) {
    const ReplayReport report = replay_record(*record, soak_scenario);
    CHECK(report.identical);
    CHECK_EQ(report.replayed_hash, record->hash);
  }
  const SerializeOutcome serialized = serialize_record(*record);
  CHECK(serialized.ok);
  for (int i = 0; i < 100; ++i) {
    const LoadOutcome loaded = load_record_bytes(serialized.bytes);
    CHECK(loaded.ok);
    CHECK_EQ(loaded.record.hash, record->hash);
  }
}

TEST(soak_workbench_record_and_delete_cycles) {
  test::TempDir dir;
  Workbench workbench(WorkbenchOptions{.data_dir = dir.path()});
  std::string error;
  CHECK(workbench.initialize(&error));
  for (int i = 0; i < 100; ++i) {
    const HttpResponse recorded = call(
        workbench, "POST", "/api/record",
        R"({"scenario":"counter-race","seed":)" + std::to_string(i) + "}");
    CHECK_EQ(recorded.status, 200);
    CHECK_EQ(workbench.records().size(), std::size_t(1));
    const RecordId id = workbench.records().front().id;
    const HttpResponse deleted = call(
        workbench, "POST", "/api/records/" + std::to_string(id) + "/delete", "{}");
    CHECK_EQ(deleted.status, 200);
    CHECK(workbench.records().empty());
  }
  CHECK(workbench.retained_bytes() == 0);
}
