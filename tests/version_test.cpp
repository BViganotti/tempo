#include "test_util.hpp"
#include "tempo/version.hpp"

using namespace tempo;

TEST(version_constants) {
  CHECK_EQ(kVersion, std::string_view("0.2.0"));
  CHECK_EQ(kRecordFormatVersion, 1);
  CHECK_EQ(kProductName, std::string_view("Tempo"));
}

TEST(version_banner_contains_name) {
  const std::string_view b = version_banner();
  CHECK(b.find("Tempo") != std::string_view::npos);
  CHECK(b.find("0.2.0") != std::string_view::npos);
}
