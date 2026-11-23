#include <filesystem>
#include <string>
#include <vector>

#include "temp_dir.hpp"
#include "test_util.hpp"
#include "tempo/json.hpp"
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

struct Fixture {
  test::TempDir dir;
  Workbench workbench;

  Fixture() : workbench(WorkbenchOptions{.data_dir = dir.path()}) {
    std::string error;
    CHECK(workbench.initialize(&error));
    CHECK_EQ(error, std::string());
  }
};

}  // namespace

TEST(overview_lists_scenarios_and_is_initialized) {
  Fixture fixture;
  const HttpResponse response = call(fixture.workbench, "GET", "/api/overview");
  CHECK_EQ(response.status, 200);
  const json::Value body = parse_body(response);
  CHECK_EQ(body.find("scenarios")->items().size(), std::size_t(3));
  CHECK(body.find("records")->items().empty());
  CHECK_EQ(body.find("version")->as_string(), std::string("0.1.0"));
}

TEST(static_assets_are_embedded) {
  Fixture fixture;
  const HttpResponse index = call(fixture.workbench, "GET", "/");
  CHECK_EQ(index.status, 200);
  CHECK(index.body.find("<!doctype html>") != std::string::npos);
  CHECK(index.body.find("Tempo") != std::string::npos);
  CHECK(index.content_type.find("text/html") != std::string::npos);

  const HttpResponse css = call(fixture.workbench, "GET", "/style.css");
  CHECK_EQ(css.status, 200);
  CHECK(css.content_type.find("text/css") != std::string::npos);

  const HttpResponse js = call(fixture.workbench, "GET", "/app.js");
  CHECK_EQ(js.status, 200);
  CHECK(js.content_type.find("javascript") != std::string::npos);

  const HttpResponse missing = call(fixture.workbench, "GET", "/nope.js");
  CHECK_EQ(missing.status, 404);

  const HttpResponse traversal = call(fixture.workbench, "GET", "/../secret");
  CHECK_EQ(traversal.status, 404);
}

TEST(record_endpoint_captures_and_persists) {
  Fixture fixture;
  const HttpResponse response =
      call(fixture.workbench, "POST", "/api/record", {}, R"({"scenario":"counter-race","seed":7})");
  CHECK_EQ(response.status, 200);
  const json::Value body = parse_body(response);
  CHECK(body.find("ok")->as_bool());
  CHECK(body.find("record") != nullptr);
  const std::int64_t id = body.find("record")->find("id")->as_i64();
  CHECK(id > 0);
  CHECK(body.find("record")->find("events")->as_i64() > 20);
  CHECK_EQ(body.find("record")->find("scenario")->as_string(), std::string("counter-race"));

  const HttpResponse overview = call(fixture.workbench, "GET", "/api/overview");
  const json::Value listing = parse_body(overview);
  CHECK_EQ(listing.find("records")->items().size(), std::size_t(1));

  std::size_t files = 0;
  for (const auto& entry :
       std::filesystem::directory_iterator(fixture.dir.path() + "/records")) {
    if (entry.path().extension() == ".tmpo") {
      ++files;
    }
  }
  CHECK_EQ(files, std::size_t(1));

  const HttpResponse events = call(fixture.workbench, "GET",
                                   "/api/records/" + std::to_string(id) + "/events",
                                   "from=0&limit=10");
  CHECK_EQ(events.status, 200);
  const json::Value page = parse_body(events);
  CHECK_EQ(page.find("events")->items().size(), std::size_t(10));
  CHECK_EQ(page.find("events")->items()[0].find("type")->as_string(),
           std::string("ScenarioStart"));

  const HttpResponse frame = call(fixture.workbench, "GET",
                                  "/api/records/" + std::to_string(id) + "/frame",
                                  "index=0");
  CHECK_EQ(frame.status, 200);
  const json::Value frame_body = parse_body(frame);
  CHECK(frame_body.find("state")->is_object());
  CHECK(!frame_body.find("event")->find("summary")->as_string().empty());

  const HttpResponse timeline = call(fixture.workbench, "GET",
                                     "/api/records/" + std::to_string(id) + "/timeline");
  CHECK_EQ(timeline.status, 200);
  const json::Value timeline_body = parse_body(timeline);
  CHECK(!timeline_body.find("lanes")->items().empty());
}

TEST(replay_endpoint_proves_stability_and_reports_mutation) {
  Fixture fixture;
  const HttpResponse recorded = call(
      fixture.workbench, "POST", "/api/record", {}, R"({"scenario":"counter-race","seed":9})");
  const std::int64_t id = parse_body(recorded).find("record")->find("id")->as_i64();
  const std::string id_text = std::to_string(id);

  const HttpResponse stable = call(
      fixture.workbench, "POST", "/api/replay", {},
      R"({"recordId":)" + id_text + R"(,"runs":100})");
  CHECK_EQ(stable.status, 200);
  const json::Value stable_body = parse_body(stable);
  CHECK(stable_body.find("identical")->as_bool());
  CHECK(stable_body.find("allHashesEqual")->as_bool());
  CHECK_EQ(stable_body.find("runs")->as_i64(), 100);
  CHECK_EQ(stable_body.find("hashes")->items().size(), std::size_t(100));
  CHECK(stable_body.find("elapsedNs")->as_number() > 0.0);

  const HttpResponse mutated = call(
      fixture.workbench, "POST", "/api/replay", {},
      R"({"recordId":)" + id_text + R"(,"mutateStep":1,"mutateChoice":0})");
  CHECK_EQ(mutated.status, 200);
  const json::Value mutated_body = parse_body(mutated);
  CHECK(!mutated_body.find("identical")->as_bool());
  CHECK(mutated_body.find("mutationApplied")->as_bool());
  const json::Value* divergence = mutated_body.find("divergence");
  CHECK(divergence != nullptr);
  CHECK(divergence->find("found")->as_bool());

  const std::int64_t divergence_index = divergence->find("index")->as_i64();
  const HttpResponse frame = call(fixture.workbench, "GET",
                                  "/api/records/" + id_text + "/frame",
                                  "index=" + std::to_string(divergence_index));
  CHECK(parse_body(frame).find("isDivergence")->as_bool());
}

TEST(breakpoints_are_managed_and_drive_navigation) {
  Fixture fixture;
  const HttpResponse recorded = call(
      fixture.workbench, "POST", "/api/record", {}, R"({"scenario":"counter-race","seed":3})");
  const std::int64_t id = parse_body(recorded).find("record")->find("id")->as_i64();
  const std::string base = "/api/records/" + std::to_string(id);

  const HttpResponse added =
      call(fixture.workbench, "POST", "/api/breakpoints", {}, R"({"type":"Watch"})");
  CHECK_EQ(added.status, 200);
  const json::Value added_body = parse_body(added);
  const std::int64_t breakpoint_id = added_body.find("id")->as_i64();

  const HttpResponse next = call(fixture.workbench, "GET", base + "/breakpoints/next",
                                 "from=0&direction=1");
  const json::Value next_body = parse_body(next);
  CHECK(next_body.find("index") != nullptr);
  const std::int64_t watch_index = next_body.find("index")->as_i64();
  CHECK(watch_index > 0);
  const HttpResponse frame = call(fixture.workbench, "GET", base + "/frame",
                                  "index=" + std::to_string(watch_index));
  CHECK_EQ(parse_body(frame).find("event")->find("type")->as_string(),
           std::string("Watch"));

  const HttpResponse disabled = call(
      fixture.workbench, "POST", "/api/breakpoints/toggle", {},
      R"({"id":)" + std::to_string(breakpoint_id) + R"(,"enabled":false})");
  CHECK_EQ(disabled.status, 200);
  const HttpResponse none = call(fixture.workbench, "GET", base + "/breakpoints/next",
                                 "from=0&direction=1");
  CHECK(parse_body(none).find("index")->is_null());

  const HttpResponse removed = call(
      fixture.workbench, "POST", "/api/breakpoints/remove", {},
      R"({"id":)" + std::to_string(breakpoint_id) + "}");
  CHECK_EQ(removed.status, 200);

  const HttpResponse invalid =
      call(fixture.workbench, "POST", "/api/breakpoints", {}, R"({"type":"NotAType"})");
  CHECK_EQ(invalid.status, 400);
}

TEST(records_can_be_diffed_and_reloaded) {
  Fixture fixture;
  const HttpResponse base_record = call(
      fixture.workbench, "POST", "/api/record", {}, R"({"scenario":"counter-race","seed":5})");
  const std::int64_t base_id = parse_body(base_record).find("record")->find("id")->as_i64();
  const HttpResponse mutated_record = call(
      fixture.workbench, "POST", "/api/record", {},
      R"({"scenario":"counter-race","seed":5,"mutateStep":1})");
  CHECK_EQ(mutated_record.status, 200);
  const std::int64_t mutated_id =
      parse_body(mutated_record).find("record")->find("id")->as_i64();
  CHECK(mutated_id != base_id);

  const HttpResponse diff = call(
      fixture.workbench, "GET",
      "/api/records/" + std::to_string(base_id) + "/diff",
      "other=" + std::to_string(mutated_id));
  CHECK_EQ(diff.status, 200);
  const json::Value diff_body = parse_body(diff);
  CHECK(!diff_body.find("equal")->as_bool());
  CHECK(diff_body.find("first")->find("found")->as_bool());
  CHECK(!diff_body.find("counts")->items().empty());

  WorkbenchOptions options;
  options.data_dir = fixture.dir.path();
  Workbench reloaded(options);
  std::string error;
  CHECK(reloaded.initialize(&error));
  const HttpResponse listing = call(reloaded, "GET", "/api/overview");
  CHECK_EQ(parse_body(listing).find("records")->items().size(), std::size_t(2));
  const auto reopened = reloaded.get_record(static_cast<RecordId>(base_id));
  CHECK(reopened != nullptr);
  CHECK(reopened->complete);
}

TEST(flaky_service_fixture_is_prepared_and_replayable) {
  Fixture fixture;
  const HttpResponse recorded = call(
      fixture.workbench, "POST", "/api/record", {}, R"({"scenario":"flaky-service","seed":2})");
  CHECK_EQ(recorded.status, 200);
  const json::Value body = parse_body(recorded);
  const std::int64_t id = body.find("record")->find("id")->as_i64();
  CHECK(std::filesystem::exists(fixture.dir.path() + "/fixtures/service.conf"));

  const HttpResponse replay = call(fixture.workbench, "POST", "/api/replay", {},
                                   R"({"recordId":)" + std::to_string(id) + "}");
  CHECK_EQ(replay.status, 200);
  CHECK(parse_body(replay).find("identical")->as_bool());
}

TEST(record_download_returns_the_portable_container) {
  Fixture fixture;
  const HttpResponse recorded = call(
      fixture.workbench, "POST", "/api/record", {}, R"({"scenario":"counter-race","seed":4})");
  const std::int64_t id = parse_body(recorded).find("record")->find("id")->as_i64();
  const HttpResponse download = call(
      fixture.workbench, "GET", "/api/records/" + std::to_string(id) + "/download");
  CHECK_EQ(download.status, 200);
  CHECK(download.content_type.find("application/octet-stream") != std::string::npos);
  bool disposition = false;
  for (const auto& [name, value] : download.extra_headers) {
    if (name == "Content-Disposition" && value.find(".tmpo") != std::string::npos) {
      disposition = true;
    }
  }
  CHECK(disposition);
  const auto record = fixture.workbench.get_record(static_cast<RecordId>(id));
  CHECK(record != nullptr);
  const SerializeOutcome expected = serialize_record(*record);
  CHECK(expected.ok);
  CHECK(download.body == expected.bytes);
  const LoadOutcome loaded = load_record_bytes(download.body);
  CHECK(loaded.ok);
}

TEST(batch_replay_api_reports_failed_run) {
  Fixture fixture;
  const HttpResponse recorded = call(
      fixture.workbench, "POST", "/api/record", {}, R"({"scenario":"counter-race","seed":8})");
  const std::int64_t id = parse_body(recorded).find("record")->find("id")->as_i64();
  const HttpResponse replay = call(
      fixture.workbench, "POST", "/api/replay", {},
      R"({"recordId":)" + std::to_string(id) + R"(,"runs":4,"mutateStep":1})");
  CHECK_EQ(replay.status, 200);
  const json::Value body = parse_body(replay);
  CHECK(!body.find("identical")->as_bool());
  CHECK_EQ(body.find("failedRun")->as_i64(), 0);
}

TEST(index_injects_the_session_token_into_asset_urls) {
  Fixture fixture;
  fixture.workbench.set_session_token("abcdef0123456789");
  const HttpResponse index = call(fixture.workbench, "GET", "/");
  CHECK_EQ(index.status, 200);
  CHECK(index.body.find("href=\"/style.css?token=abcdef0123456789\"") != std::string::npos);
  CHECK(index.body.find("src=\"/app.js?token=abcdef0123456789\"") != std::string::npos);
  CHECK(index.body.find("href=\"/favicon.svg?token=abcdef0123456789\"") != std::string::npos);
  CHECK(index.body.find("href=\"/style.css\"") == std::string::npos);
}

TEST(health_endpoint_reports_readiness) {
  Fixture fixture;
  const HttpResponse response = call(fixture.workbench, "GET", "/api/health");
  CHECK_EQ(response.status, 200);
  const json::Value body = parse_body(response);
  CHECK(body.find("ok")->as_bool());
  CHECK(body.find("initialized")->as_bool());
  CHECK_EQ(body.find("records")->as_i64(), 0);
}

TEST(imports_deduplicates_and_deletes_records) {
  Fixture fixture;
  const HttpResponse recorded = call(
      fixture.workbench, "POST", "/api/record", {}, R"({"scenario":"counter-race","seed":4})");
  const std::int64_t source_id = parse_body(recorded).find("record")->find("id")->as_i64();
  const HttpResponse download = call(
      fixture.workbench, "GET", "/api/records/" + std::to_string(source_id) + "/download");
  CHECK_EQ(download.status, 200);

  test::TempDir second_dir;
  Workbench second(WorkbenchOptions{.data_dir = second_dir.path()});
  std::string error;
  CHECK(second.initialize(&error));
  const HttpResponse imported =
      call(second, "POST", "/api/records/import", {}, download.body);
  CHECK_EQ(imported.status, 200);
  const json::Value import_body = parse_body(imported);
  CHECK(import_body.find("ok")->as_bool());
  CHECK(!import_body.find("duplicate")->as_bool());
  const std::int64_t imported_id = import_body.find("record")->find("id")->as_i64();
  CHECK(imported_id > 0);
  CHECK_EQ(import_body.find("record")->find("scenario")->as_string(),
           std::string("counter-race"));

  const HttpResponse duplicate =
      call(second, "POST", "/api/records/import", {}, download.body);
  CHECK_EQ(duplicate.status, 200);
  const json::Value duplicate_body = parse_body(duplicate);
  CHECK(duplicate_body.find("duplicate")->as_bool());
  CHECK_EQ(duplicate_body.find("record")->find("id")->as_i64(), imported_id);

  const HttpResponse replay = call(second, "POST", "/api/replay", {},
                                   R"({"recordId":)" + std::to_string(imported_id) + "}");
  CHECK_EQ(replay.status, 200);
  CHECK(parse_body(replay).find("identical")->as_bool());

  const HttpResponse malformed = call(second, "POST", "/api/records/import", {}, "not-a-record");
  CHECK_EQ(malformed.status, 400);

  const std::string file = second.records().front().file;
  CHECK(std::filesystem::exists(file));
  const HttpResponse deleted = call(
      second, "POST", "/api/records/" + std::to_string(imported_id) + "/delete", {},
      R"({"deleteFile":true})");
  CHECK_EQ(deleted.status, 200);
  CHECK(second.records().empty());
  CHECK(!std::filesystem::exists(file));
  CHECK_EQ(call(second, "POST", "/api/records/" + std::to_string(imported_id) + "/delete", {},
                "{}")
               .status,
           404);
}

TEST(clear_breakpoints_endpoint) {
  Fixture fixture;
  CHECK_EQ(call(fixture.workbench, "POST", "/api/breakpoints", {}, R"({"type":"Step"})").status,
           200);
  CHECK_EQ(call(fixture.workbench, "POST", "/api/breakpoints", {}, R"({"task":2})").status,
           200);
  CHECK_EQ(fixture.workbench.breakpoints().size(), std::size_t(2));
  const HttpResponse cleared =
      call(fixture.workbench, "POST", "/api/breakpoints/clear", {}, "{}");
  CHECK_EQ(cleared.status, 200);
  CHECK(fixture.workbench.breakpoints().empty());
}

TEST(record_cap_evicts_memory_but_keeps_files) {
  test::TempDir dir;
  Workbench workbench(WorkbenchOptions{.data_dir = dir.path(), .max_records = 2});
  std::string error;
  CHECK(workbench.initialize(&error));
  for (int i = 0; i < 3; ++i) {
    const HttpResponse response = call(
        workbench, "POST", "/api/record", {},
        R"({"scenario":"counter-race","seed":)" + std::to_string(i) + "}");
    CHECK_EQ(response.status, 200);
  }
  CHECK_EQ(workbench.records().size(), std::size_t(2));
  CHECK(workbench.get_record(1) == nullptr);
  CHECK(workbench.get_record(3) != nullptr);
  std::size_t files = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir.path() + "/records")) {
    if (entry.path().extension() == ".tmpo") ++files;
  }
  CHECK_EQ(files, std::size_t(3));
}

TEST(event_filters_page_over_matching_events) {
  Fixture fixture;
  const HttpResponse recorded = call(
      fixture.workbench, "POST", "/api/record", {}, R"({"scenario":"counter-race","seed":6})");
  const std::int64_t id = parse_body(recorded).find("record")->find("id")->as_i64();
  const std::string base = "/api/records/" + std::to_string(id) + "/events";

  const HttpResponse steps = call(fixture.workbench, "GET", base, "type=Step&limit=500");
  CHECK_EQ(steps.status, 200);
  const json::Value steps_body = parse_body(steps);
  CHECK(steps_body.find("filtered")->as_bool());
  CHECK(steps_body.find("total")->as_i64() > 0);
  for (const auto& event : steps_body.find("events")->items()) {
    CHECK_EQ(event.find("type")->as_string(), std::string("Step"));
  }

  const HttpResponse page = call(fixture.workbench, "GET", base, "type=Step&from=1&limit=1");
  CHECK_EQ(page.status, 200);
  CHECK_EQ(parse_body(page).find("events")->items().size(), std::size_t(1));

  const HttpResponse task = call(fixture.workbench, "GET", base, "task=2&limit=500");
  CHECK_EQ(task.status, 200);
  for (const auto& event : parse_body(task).find("events")->items()) {
    CHECK_EQ(event.find("task")->as_i64(), 2);
  }

  CHECK_EQ(call(fixture.workbench, "GET", base, "type=Nope").status, 400);
  CHECK_EQ(call(fixture.workbench, "GET", base, "task=not-a-number").status, 400);
}

TEST(api_rejects_bad_inputs) {
  Fixture fixture;
  CHECK_EQ(call(fixture.workbench, "POST", "/api/record", {}, R"({"scenario":"nope"})").status,
           400);
  CHECK_EQ(call(fixture.workbench, "POST", "/api/record", {}, R"({bad json)").status, 400);
  CHECK_EQ(call(fixture.workbench, "POST", "/api/record", {}, R"({"scenario":"counter-race"})").status,
           200);
  CHECK_EQ(call(fixture.workbench, "GET", "/api/records/999/events").status, 404);
  CHECK_EQ(call(fixture.workbench, "GET", "/api/records/abc/events").status, 400);
  CHECK_EQ(call(fixture.workbench, "DELETE", "/api/overview").status, 405);
  CHECK_EQ(call(fixture.workbench, "POST", "/api/replay", {}, R"({"recordId":1,"runs":0})").status,
           400);
}

TEST(broadcaster_reports_records_and_replays) {
  Fixture fixture;
  std::vector<std::pair<std::string, std::string>> messages;
  fixture.workbench.set_broadcaster(
      [&messages](std::string_view event, std::string_view data) {
        messages.emplace_back(std::string(event), std::string(data));
      });
  const HttpResponse recorded = call(
      fixture.workbench, "POST", "/api/record", {}, R"({"scenario":"pipeline","seed":1})");
  const std::int64_t id = parse_body(recorded).find("record")->find("id")->as_i64();
  (void)call(fixture.workbench, "POST", "/api/replay", {},
             R"({"recordId":)" + std::to_string(id) + "}");
  bool saw_overview = false;
  bool saw_replay = false;
  for (const auto& [event, data] : messages) {
    CHECK(!data.empty());
    if (event == "overview") saw_overview = true;
    if (event == "replay") saw_replay = true;
  }
  CHECK(saw_overview);
  CHECK(saw_replay);
}

TEST(uninitialized_workbench_returns_service_unavailable) {
  test::TempDir dir;
  WorkbenchOptions options;
  options.data_dir = dir.path();
  Workbench workbench(options);
  const HttpResponse response = call(workbench, "GET", "/api/overview");
  CHECK_EQ(response.status, 503);
}
