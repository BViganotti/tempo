#include <filesystem>
#include <fstream>
#include <string>

#include "host_test.hpp"
#include "tempo/json.hpp"
#include "temp_dir.hpp"
#include "test_util.hpp"
#include "tempo/record_run.hpp"
#include "tempo/workbench.hpp"

using namespace tempo;

namespace {

HttpResponse call(Workbench& workbench, const std::string& method, const std::string& path,
                  const std::string& query = {}, const std::string& body = {}) {
  HttpRequest request;
  request.method = method;
  request.path = path;
  request.target = path + (query.empty() ? "" : "?" + query);
  request.query = query;
  request.body = body;
  request.version = "HTTP/1.1";
  request.headers["host"] = "127.0.0.1:1";
  return workbench.handle(request);
}

json::Value parse_body(const HttpResponse& response) {
  const auto parsed = json::parse(response.body);
  CHECK(parsed.ok);
  return parsed.value;
}

Task tiny(Context ctx) {
  ctx.watch("value", 1);
  co_await ctx.yield();
  ctx.watch("value", 2);
}

}  // namespace

TEST(recovered_and_malformed_records_surface_as_warnings) {
  test::TempDir dir;
  std::filesystem::create_directories(dir.path() + "/records");
  test::TestHost capabilities;
  const RecordRunResult recorded = record_run("tiny", tiny, capabilities);
  CHECK(recorded.ok);
  const SerializeOutcome serialized = serialize_record(recorded.record);
  CHECK(serialized.ok);
  CHECK(serialized.bytes.size() > 20);
  // Remove the footer while keeping every event intact.
  const std::string truncated = serialized.bytes.substr(0, serialized.bytes.size() - 10);
  {
    std::ofstream out(dir.path() + "/records/rec-1700000001-deadbeef-tiny.tmpo",
                      std::ios::binary);
    out.write(truncated.data(), static_cast<std::streamsize>(truncated.size()));
  }
  {
    std::ofstream out(dir.path() + "/records/rec-1700000002-badbad00-broken.tmpo",
                      std::ios::binary);
    out << "this is definitely not a tempo record";
  }

  Workbench workbench(WorkbenchOptions{.data_dir = dir.path()});
  std::string error;
  CHECK(workbench.initialize(&error));
  CHECK_EQ(workbench.records().size(), std::size_t(1));
  CHECK(!workbench.load_warnings().empty());
  CHECK(workbench.records().front().record->recovered);
  CHECK(!workbench.records().front().record->complete);

  const HttpResponse overview = call(workbench, "GET", "/api/overview");
  const json::Value overview_body = parse_body(overview);
  CHECK_EQ(overview_body.find("records")->items().size(), std::size_t(1));
  CHECK(overview_body.find("records")->items()[0].find("recovered")->as_bool());
  CHECK(!overview_body.find("loadWarnings")->items().empty());

  const std::int64_t id = workbench.records().front().id;
  const HttpResponse frame = call(workbench, "GET",
                                  "/api/records/" + std::to_string(id) + "/frame", "index=0");
  CHECK_EQ(frame.status, 200);
  const std::string replay_body = R"({"recordId":)" + std::to_string(id) + "}";
  const HttpResponse replay = call(workbench, "POST", "/api/replay", {}, replay_body);
  CHECK_EQ(replay.status, 400);
  CHECK(parse_body(replay).find("error")->as_string().find("incomplete") !=
        std::string::npos);

  const std::string file = workbench.records().front().file;
  CHECK(std::filesystem::exists(file));
  const HttpResponse deleted = call(
      workbench, "POST", "/api/records/" + std::to_string(id) + "/delete", {}, "{}");
  CHECK_EQ(deleted.status, 200);
  CHECK(!std::filesystem::exists(file));
}

TEST(initialize_reports_a_missing_data_directory_as_created) {
  test::TempDir dir;
  const std::string nested = dir.path() + "/nested/data";
  Workbench workbench(WorkbenchOptions{.data_dir = nested});
  std::string error;
  CHECK(workbench.initialize(&error));
  CHECK(std::filesystem::is_directory(nested));
  CHECK(std::filesystem::is_directory(nested + "/records"));
}
