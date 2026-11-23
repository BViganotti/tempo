#include <limits>
#include <locale>
#include <sstream>
#include <string>

#include "test_util.hpp"
#include "tempo/format.hpp"
#include "tempo/json.hpp"

using namespace tempo;

namespace {

class CommaNumpunct : public std::numpunct<char> {
 protected:
  char do_decimal_point() const override { return ','; }
  char do_thousands_sep() const override { return '.'; }
  std::string do_grouping() const override { return "\3"; }
};

}  // namespace

TEST(formatting_uses_a_dot_independent_of_global_locale) {
  const std::locale original = std::locale::global(std::locale::classic());
  const std::locale comma(std::locale::classic(), new CommaNumpunct);
  std::locale::global(comma);
  const std::string general = format_general(1.5, 6);
  const std::string fixed = format_fixed(1.25, 3);
  const std::string json = format_json_number(1.5);
  std::locale::global(original);
  CHECK_EQ(general, std::string("1.5"));
  CHECK_EQ(fixed, std::string("1.250"));
  CHECK_EQ(json, std::string("1.5"));
}

TEST(json_numbers_survive_a_comma_locale) {
  const std::locale original = std::locale::global(std::locale::classic());
  std::locale::global(std::locale(std::locale::classic(), new CommaNumpunct));
  const std::string dumped = json::Value(1.5).dump();
  const auto parsed = json::parse(dumped);
  const std::locale restore = original;
  std::locale::global(restore);
  CHECK_EQ(dumped, std::string("1.5"));
  CHECK(parsed.ok);
  CHECK_EQ(parsed.value.as_number(), 1.5);
}

TEST(non_finite_json_numbers_are_null_and_finite_ones_roundtrip) {
  CHECK_EQ(format_json_number(std::numeric_limits<double>::infinity()), std::string("null"));
  CHECK_EQ(format_json_number(-std::numeric_limits<double>::infinity()), std::string("null"));
  CHECK_EQ(format_json_number(std::numeric_limits<double>::quiet_NaN()), std::string("null"));
  CHECK_EQ(format_json_number(0.0), std::string("0"));
  CHECK_EQ(format_general(1234567.0, 6), std::string("1.23457e+06"));
}
