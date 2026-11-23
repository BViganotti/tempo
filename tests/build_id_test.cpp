#include <memory>
#include <string>

#include "host_test.hpp"
#include "temp_dir.hpp"
#include "test_util.hpp"
#include "tempo/json.hpp"
#include "tempo/record_run.hpp"
#include "tempo/replay.hpp"
#include "tempo/scenario.hpp"
#include "tempo/workbench.hpp"

using namespace tempo;

namespace {

Task stamped_scenario(Context ctx) {
  ctx.watch("phase", "run");
  co_await ctx.yield();
  ctx.watch("phase", "done");
}

HttpResponse api(Workbench& workbench, const std::string& method, const std::string& path,
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

TEST(builtins_have_a_shared_nonempty_build_id) {
  const auto scenarios = ScenarioRegistry::builtins().list();
  CHECK(!scenarios.empty());
  const std::string expected = scenarios.front().build_id;
  CHECK(!expected.empty());
  CHECK_EQ(expected.size(), std::size_t(64));
  for (const auto& scenario : scenarios) {
    CHECK_EQ(scenario.build_id, expected);
  }
}

TEST(record_run_stamps_the_build_id_into_metadata) {
  test::TestHost capabilities;
  RecordRunOptions options;
  options.build_id = "build-abc";
  const RecordRunResult recorded = record_run("stamped", stamped_scenario, capabilities, options);
  CHECK(recorded.ok);
  const auto it = recorded.record.meta.attributes.find("build_id");
  CHECK(it != recorded.record.meta.attributes.end());
  CHECK_EQ(it->second, std::string("build-abc"));
}

TEST(replay_reports_a_build_mismatch_without_refusing_to_run) {
  test::TestHost capabilities;
  RecordRunOptions options;
  options.build_id = "old-build";
  const RecordRunResult recorded = record_run("stamped", stamped_scenario, capabilities, options);
  CHECK(recorded.ok);
  ReplayOptions replay_options;
  replay_options.expected_build_id = "new-build";
  const ReplayReport replay = replay_record(recorded.record, stamped_scenario, replay_options);
  CHECK(replay.ok);
  CHECK(replay.identical);
  CHECK(replay.build_mismatch);
  CHECK(replay.build_warning.find("does not match") != std::string::npos);
}

TEST(workbench_surfaces_build_mismatch_for_imported_records) {
  test::TestHost capabilities;
  RecordRunOptions options;
  options.build_id = "an-older-checkout";
  const RecordRunResult recorded = record_run("counter-race", [](Context) -> Task {
    co_return;
  }, capabilities, options);
  CHECK(recorded.ok);
  const SerializeOutcome serialized = serialize_record(recorded.record);
  CHECK(serialized.ok);

  test::TempDir dir;
  Workbench workbench(WorkbenchOptions{.data_dir = dir.path()});
  std::string error;
  CHECK(workbench.initialize(&error));
  const HttpResponse imported = api(workbench, "POST", "/api/records/import", serialized.bytes);
  CHECK_EQ(imported.status, 200);
  const auto parsed = json::parse(imported.body);
  CHECK(parsed.ok);
  const std::int64_t id = parsed.value.find("record")->find("id")->as_i64();
  const HttpResponse replay = api(workbench, "POST", "/api/replay",
                                  R"({"recordId":)" + std::to_string(id) + "}");
  CHECK_EQ(replay.status, 200);
  const auto body = json::parse(replay.body);
  CHECK(body.ok);
  CHECK(body.value.find("buildMismatch")->as_bool());
  CHECK(!body.value.find("buildWarning")->as_string().empty());
}
