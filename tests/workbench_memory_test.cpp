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

std::string record_body(int seed) {
  return R"({"scenario":"counter-race","seed":)" + std::to_string(seed) + "}";
}

std::size_t record_file_count(const std::string& dir) {
  std::size_t count = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir + "/records")) {
    if (entry.path().extension() == ".tmpo") ++count;
  }
  return count;
}

}  // namespace

TEST(memory_budget_evicts_old_records_but_keeps_files) {
  test::TempDir dir;
  Workbench probe(WorkbenchOptions{.data_dir = dir.path()});
  std::string error;
  CHECK(probe.initialize(&error));
  CHECK_EQ(call(probe, "POST", "/api/record", record_body(1)).status, 200);
  const std::size_t one_record = probe.records().front().memory_bytes;
  CHECK_EQ(one_record, probe.records().front().record->approximate_bytes());
  CHECK(one_record > 0);

  test::TempDir dir2;
  Workbench workbench(
      WorkbenchOptions{.data_dir = dir2.path(), .max_total_bytes = one_record + 512});
  CHECK(workbench.initialize(&error));
  CHECK_EQ(call(workbench, "POST", "/api/record", record_body(1)).status, 200);
  CHECK_EQ(call(workbench, "POST", "/api/record", record_body(2)).status, 200);
  CHECK_EQ(workbench.records().size(), std::size_t(1));
  CHECK(workbench.retained_bytes() <= workbench.memory_budget());
  CHECK_EQ(record_file_count(dir2.path()), std::size_t(2));
}

TEST(a_record_larger_than_the_budget_is_rejected_without_a_file) {
  test::TempDir dir;
  Workbench workbench(WorkbenchOptions{.data_dir = dir.path(), .max_total_bytes = 512});
  std::string error;
  CHECK(workbench.initialize(&error));
  const HttpResponse response = call(workbench, "POST", "/api/record", record_body(1));
  CHECK_EQ(response.status, 400);
  const auto parsed = json::parse(response.body);
  CHECK(parsed.ok);
  CHECK(parsed.value.find("error")->as_string().find("memory budget") != std::string::npos);
  CHECK(workbench.records().empty());
  CHECK_EQ(record_file_count(dir.path()), std::size_t(0));
  const std::size_t used = workbench.retained_bytes();
  CHECK_EQ(used, std::size_t(0));
}

TEST(startup_respects_the_budget_and_warns) {
  test::TempDir dir;
  Workbench writer(WorkbenchOptions{.data_dir = dir.path()});
  std::string error;
  CHECK(writer.initialize(&error));
  CHECK_EQ(call(writer, "POST", "/api/record", record_body(1)).status, 200);
  CHECK_EQ(call(writer, "POST", "/api/record", record_body(2)).status, 200);
  const std::size_t one = writer.records().front().memory_bytes;

  Workbench reader(
      WorkbenchOptions{.data_dir = dir.path(), .max_total_bytes = one + 512});
  CHECK(reader.initialize(&error));
  CHECK_EQ(reader.records().size(), std::size_t(1));
  CHECK(reader.retained_bytes() <= reader.memory_budget());
  CHECK(!reader.load_warnings().empty());
}

TEST(health_reports_memory_usage_and_budget) {
  test::TempDir dir;
  Workbench workbench(WorkbenchOptions{.data_dir = dir.path(), .max_total_bytes = 4u << 20});
  std::string error;
  CHECK(workbench.initialize(&error));
  const HttpResponse health = call(workbench, "GET", "/api/health");
  const auto parsed = json::parse(health.body);
  CHECK(parsed.ok);
  CHECK_EQ(parsed.value.find("memoryBudget")->as_i64(), std::int64_t(4u << 20));
  CHECK_EQ(parsed.value.find("memoryUsed")->as_i64(), 0);
}
