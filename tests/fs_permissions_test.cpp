#include <sys/stat.h>

#include "temp_dir.hpp"
#include "test_util.hpp"
#include "tempo/workbench.hpp"

using namespace tempo;

namespace {

mode_t mode_of(const std::string& path) {
  struct stat info {};
  if (::stat(path.c_str(), &info) != 0) {
    return 0;
  }
  return info.st_mode & 0777;
}

}  // namespace

TEST(data_and_record_directories_are_owner_only) {
  test::TempDir dir;
  Workbench workbench(WorkbenchOptions{.data_dir = dir.path()});
  std::string error;
  CHECK(workbench.initialize(&error));
  CHECK_EQ(mode_of(dir.path()), mode_t(0700));
  CHECK_EQ(mode_of(dir.path() + "/records"), mode_t(0700));
}

TEST(fixtures_and_records_are_owner_only_files) {
  test::TempDir dir;
  Workbench workbench(WorkbenchOptions{.data_dir = dir.path()});
  std::string error;
  CHECK(workbench.initialize(&error));
  HttpRequest request;
  request.method = "POST";
  request.path = "/api/record";
  request.body = R"({"scenario":"flaky-service","seed":1})";
  const HttpResponse response = workbench.handle(request);
  CHECK_EQ(response.status, 200);
  CHECK_EQ(mode_of(dir.path() + "/fixtures/service.conf"), mode_t(0600));
  CHECK_EQ(workbench.records().size(), std::size_t(1));
  CHECK(!workbench.records().front().file.empty());
  CHECK_EQ(mode_of(workbench.records().front().file), mode_t(0600));
}
