#include <chrono>
#include <string>
#include <thread>

#include "temp_dir.hpp"
#include "test_util.hpp"
#include "tempo/json.hpp"
#include "tempo/workbench.hpp"

using namespace tempo;

namespace {

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

json::Value parse_body(const HttpResponse& response) {
  const auto parsed = json::parse(response.body);
  CHECK(parsed.ok);
  return parsed.value;
}

json::Value wait_for_job(Workbench& workbench, std::uint64_t job_id) {
  for (int attempt = 0; attempt < 1000; ++attempt) {
    const HttpResponse response =
        call(workbench, "GET", "/api/jobs/" + std::to_string(job_id));
    CHECK_EQ(response.status, 200);
    json::Value body = parse_body(response);
    const std::string status = body.find("status")->as_string();
    if (status == "done" || status == "error") {
      return body;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(false);
  return {};
}

}  // namespace

TEST(background_batch_replay_returns_a_stable_result) {
  test::TempDir dir;
  Workbench workbench(WorkbenchOptions{.data_dir = dir.path()});
  std::string error;
  CHECK(workbench.initialize(&error));
  const HttpResponse recorded = call(
      workbench, "POST", "/api/record", R"({"scenario":"counter-race","seed":7})");
  CHECK_EQ(recorded.status, 200);
  const std::int64_t id = parse_body(recorded).find("record")->find("id")->as_i64();

  const HttpResponse queued = call(
      workbench, "POST", "/api/replay",
      R"({"recordId":)" + std::to_string(id) + R"(,"runs":5,"async":true})");
  CHECK_EQ(queued.status, 202);
  const json::Value queued_body = parse_body(queued);
  const std::uint64_t job_id = static_cast<std::uint64_t>(queued_body.find("jobId")->as_i64());
  CHECK(job_id > 0);

  const json::Value done = wait_for_job(workbench, job_id);
  CHECK_EQ(done.find("status")->as_string(), std::string("done"));
  const json::Value* result = done.find("result");
  CHECK(result != nullptr);
  CHECK(result->find("identical")->as_bool());
  CHECK(result->find("allIdentical")->as_bool());
  CHECK_EQ(result->find("runs")->as_i64(), 5);
  CHECK_EQ(result->find("hashes")->items().size(), std::size_t(5));

  const HttpResponse list = call(workbench, "GET", "/api/jobs");
  CHECK_EQ(list.status, 200);
  CHECK(!parse_body(list).items().empty());
}

TEST(background_mutation_replay_reports_divergence) {
  test::TempDir dir;
  Workbench workbench(WorkbenchOptions{.data_dir = dir.path()});
  std::string error;
  CHECK(workbench.initialize(&error));
  const HttpResponse recorded = call(
      workbench, "POST", "/api/record", R"({"scenario":"counter-race","seed":7})");
  const std::int64_t id = parse_body(recorded).find("record")->find("id")->as_i64();
  const HttpResponse queued = call(
      workbench, "POST", "/api/replay",
      R"({"recordId":)" + std::to_string(id) +
          R"(,"runs":1,"mutateStep":1,"async":true})");
  CHECK_EQ(queued.status, 202);
  const std::uint64_t job_id =
      static_cast<std::uint64_t>(parse_body(queued).find("jobId")->as_i64());
  const json::Value done = wait_for_job(workbench, job_id);
  CHECK_EQ(done.find("status")->as_string(), std::string("done"));
  const json::Value* result = done.find("result");
  CHECK(result != nullptr);
  CHECK(!result->find("identical")->as_bool());
  CHECK(result->find("divergence")->find("found")->as_bool());
}

TEST(async_endpoint_rejects_unknown_records_and_jobs) {
  test::TempDir dir;
  Workbench workbench(WorkbenchOptions{.data_dir = dir.path()});
  std::string error;
  CHECK(workbench.initialize(&error));
  CHECK_EQ(call(workbench, "POST", "/api/replay", R"({"recordId":99,"async":true})").status,
           400);
  CHECK_EQ(call(workbench, "GET", "/api/jobs/999").status, 404);
  CHECK_EQ(call(workbench, "GET", "/api/jobs/not-a-number").status, 400);
}
