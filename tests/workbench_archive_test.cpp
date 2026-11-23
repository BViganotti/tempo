#include <filesystem>
#include <string>

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

json::Value body_of(const HttpResponse& response) {
  const auto parsed = json::parse(response.body);
  CHECK(parsed.ok);
  return parsed.value;
}

std::string record_body(int seed) {
  return R"({"scenario":"counter-race","seed":)" + std::to_string(seed) + "}";
}

}  // namespace

TEST(archived_records_can_be_listed_and_loaded) {
  test::TempDir dir;
  Workbench workbench(WorkbenchOptions{.data_dir = dir.path(), .max_records = 1});
  std::string error;
  CHECK(workbench.initialize(&error));
  for (int seed = 1; seed <= 3; ++seed) {
    CHECK_EQ(call(workbench, "POST", "/api/record", record_body(seed)).status, 200);
  }
  CHECK_EQ(workbench.records().size(), std::size_t(1));
  const HttpResponse archive = call(workbench, "GET", "/api/archive");
  CHECK_EQ(archive.status, 200);
  const json::Value archive_body = body_of(archive);
  const auto& entries = archive_body.find("records")->items();
  CHECK_EQ(entries.size(), std::size_t(2));
  const std::string file = entries.front().find("file")->as_string();
  CHECK(!file.empty());

  const HttpResponse opened =
      call(workbench, "POST", "/api/records/open", R"({"file":")" + file + R"("})");
  CHECK_EQ(opened.status, 200);
  const json::Value opened_body = body_of(opened);
  CHECK(opened_body.find("ok")->as_bool());
  CHECK_EQ(workbench.records().size(), std::size_t(1));
  const HttpResponse overview = call(workbench, "GET", "/api/overview");
  CHECK(overview.status == 200);
  CHECK(!body_of(overview).find("archive")->items().empty());
}

TEST(archive_open_rejects_unsafe_paths_and_unknown_files) {
  test::TempDir dir;
  Workbench workbench(WorkbenchOptions{.data_dir = dir.path()});
  std::string error;
  CHECK(workbench.initialize(&error));
  CHECK_EQ(call(workbench, "POST", "/api/records/open", R"({"file":"../secret.tmpo"})")
               .status,
           400);
  CHECK_EQ(call(workbench, "POST", "/api/records/open", R"({"file":"missing.tmpo"})")
               .status,
           400);
  CHECK_EQ(call(workbench, "POST", "/api/records/open", R"({"file":"x.txt"})").status, 400);
}
