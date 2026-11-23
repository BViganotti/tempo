#include <string>

#include "test_util.hpp"
#include "tempo/limits.hpp"
#include "tempo/json.hpp"

using namespace tempo;
using namespace tempo::json;

TEST(parses_scalars) {
  auto null_value = parse("null");
  CHECK(null_value.ok);
  CHECK(null_value.value.is_null());
  auto boolean = parse("true");
  CHECK(boolean.ok);
  CHECK(boolean.value.as_bool());
  auto number = parse("-12.5e2");
  CHECK(number.ok);
  CHECK_EQ(number.value.as_number(), -1250.0);
  auto text = parse("\"hello\"");
  CHECK(text.ok);
  CHECK_EQ(text.value.as_string(), std::string("hello"));
}

TEST(parses_arrays_and_objects) {
  const auto outcome = parse(R"({"b":[1,2,3],"a":{"nested":true},"c":null})");
  CHECK(outcome.ok);
  CHECK(outcome.value.is_object());
  CHECK_EQ(outcome.value.fields().size(), std::size_t(3));
  const Value* b = outcome.value.find("b");
  CHECK(b != nullptr);
  CHECK(b->is_array());
  CHECK_EQ(b->items().size(), std::size_t(3));
  CHECK_EQ(b->items()[2].as_i64(), 3);
  const Value* a = outcome.value.find("a");
  CHECK(a != nullptr);
  CHECK(a->find("nested")->as_bool());
  CHECK(outcome.value.find("missing") == nullptr);
}

TEST(decodes_string_escapes) {
  const auto outcome = parse(R"("a\nb\t\u00e9\ud83d\ude00\\\"")");
  CHECK(outcome.ok);
  const std::string value = outcome.value.as_string();
  CHECK(value.find("a\nb\t") == 0);
  CHECK(valid_utf8(value));
  CHECK(value.find("\xF0\x9F\x98\x80") != std::string::npos);
}

TEST(rejects_malformed_input) {
  for (const char* text : {"", "{", "[1,", "{\"a\":}", "tru", "01", "1.",
                           "\"unterminated", "\"\\q\"", "{} trailing", "[1,]",
                           "{\"a\":1,}", "{\"a\":1,\"a\":2}", "\"\x01\"",
                           "nul", "1e", "-", "+1", ".5"}) {
    const auto outcome = parse(text);
    if (outcome.ok) {
      std::fprintf(stderr, "unexpectedly parsed: %s\n", text);
      CHECK(false);
    }
    CHECK(!outcome.error.message.empty());
  }
}

TEST(rejects_excessive_depth) {
  std::string text;
  for (int i = 0; i < 40; ++i) {
    text.push_back('[');
  }
  for (int i = 0; i < 40; ++i) {
    text.push_back(']');
  }
  const auto outcome = parse(text);
  CHECK(!outcome.ok);
  CHECK(outcome.error.message.find("nesting") != std::string::npos);
}

TEST(rejects_number_overflow) {
  const auto outcome = parse("1e400");
  CHECK(!outcome.ok);
}

TEST(dump_is_compact_and_deterministic) {
  Value object = Value::object();
  object["z"] = Value(1);
  object["a"] = Value("x");
  Value array = Value::array();
  array.push(Value(true));
  array.push(Value(nullptr));
  object["list"] = std::move(array);
  const std::string dumped = object.dump();
  CHECK_EQ(dumped, std::string(R"({"a":"x","list":[true,null],"z":1})"));
  CHECK_EQ(object.dump(), dumped);
}

TEST(dump_escapes_control_characters) {
  Value value(std::string("a\"b\\c\n\t\x01"));
  const std::string dumped = value.dump();
  CHECK_EQ(dumped, std::string("\"a\\\"b\\\\c\\n\\t\\u0001\""));
  const auto roundtrip = parse(dumped);
  CHECK(roundtrip.ok);
  CHECK_EQ(roundtrip.value.as_string(), std::string("a\"b\\c\n\t\x01"));
}

TEST(roundtrip_preserves_structure) {
  const std::string text = R"({"events":[{"i":0,"type":"Step","t":1}],"ok":true,"n":3})";
  const auto parsed = parse(text);
  CHECK(parsed.ok);
  const auto again = parse(parsed.value.dump());
  CHECK(again.ok);
  CHECK_EQ(again.value.dump(), parsed.value.dump());
}

TEST(type_accessors_use_fallbacks) {
  const Value text("abc");
  CHECK_EQ(text.as_i64(7), 7);
  CHECK_EQ(text.as_bool(true), true);
  CHECK_EQ(text.as_string(), std::string("abc"));
  const Value number(42);
  CHECK_EQ(number.as_i64(), 42);
  CHECK_EQ(number.as_string("fallback"), std::string("fallback"));
}

TEST(quote_escapes_and_roundtrips) {
  const std::string text("line\nquote\"slash\\");
  const std::string quoted = quote(text);
  const auto parsed = parse(quoted);
  CHECK(parsed.ok);
  CHECK_EQ(parsed.value.as_string(), text);
}

TEST(parser_is_bounded_on_oversize_input) {
  const std::string huge(limits::kMaxJsonBytes + 1, ' ');
  const auto outcome = parse(huge);
  CHECK(!outcome.ok);
  CHECK(outcome.error.message.find("1 MiB") != std::string::npos);
}
