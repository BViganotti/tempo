#include <string>

#include "test_util.hpp"
#include "tempo/string_table.hpp"

using namespace tempo;

TEST(index_zero_is_empty) {
  StringTable table;
  CHECK_EQ(table.size(), 1u);
  CHECK_EQ(table.at(0), std::string());
  CHECK_EQ(table.intern(""), 0u);
}

TEST(intern_deduplicates) {
  StringTable table;
  const auto a = table.intern("alpha");
  const auto b = table.intern("beta");
  const auto again = table.intern("alpha");
  CHECK_EQ(a, again);
  CHECK(a != b);
  CHECK_EQ(table.size(), 3u);
  CHECK_EQ(table.at(a), "alpha");
  CHECK_EQ(table.at(b), "beta");
}

TEST(strings_may_contain_nul_bytes) {
  StringTable table;
  const std::string value("a\0b", 3);
  const auto id = table.intern(value);
  CHECK_EQ(table.at(id).size(), 3u);
  CHECK_EQ(table.at(id), value);
}

TEST(oversized_string_is_rejected) {
  StringTable table;
  CHECK_THROWS(table.intern(std::string(limits::kMaxStringBytes + 1, 'x')));
}

TEST(out_of_range_reference_throws) {
  StringTable table;
  CHECK_THROWS(table.at(999));
}

TEST(clear_restores_empty_table) {
  StringTable table;
  (void)table.intern("x");
  table.clear();
  CHECK_EQ(table.size(), 1u);
  CHECK_EQ(table.at(0), std::string());
}

TEST(add_loaded_keeps_order) {
  StringTable table;
  table.add_loaded("one");
  table.add_loaded("two");
  CHECK_EQ(table.at(0), std::string());
  CHECK_EQ(table.at(1), "one");
  CHECK_EQ(table.at(2), "two");
  CHECK_EQ(table.size(), 3u);
}
