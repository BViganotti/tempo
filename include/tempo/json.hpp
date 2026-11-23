#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace tempo::json {

/// Small strict JSON model used by the workbench API. It is intentionally not a
/// general-purpose library: parsing is bounded by hard limits, duplicate object
/// keys are rejected, and non-finite numbers never appear in output.
class Value {
 public:
  enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };

  using Array = std::vector<Value>;
  using Object = std::map<std::string, Value, std::less<>>;

  Value() = default;
  Value(std::nullptr_t) noexcept {}
  explicit Value(bool value) noexcept : type_(Type::Bool), bool_(value) {}
  explicit Value(int value) noexcept : type_(Type::Number), number_(value) {}
  explicit Value(std::int64_t value) noexcept : type_(Type::Number), number_(static_cast<double>(value)) {}
  explicit Value(std::uint64_t value) noexcept : type_(Type::Number), number_(static_cast<double>(value)) {}
  explicit Value(double value) noexcept : type_(Type::Number), number_(value) {}
  explicit Value(const char* value) : type_(Type::String), string_(value == nullptr ? "" : value) {}
  explicit Value(std::string value) : type_(Type::String), string_(std::move(value)) {}

  static Value array();
  static Value object();

  Type type() const noexcept { return type_; }
  bool is_null() const noexcept { return type_ == Type::Null; }
  bool is_bool() const noexcept { return type_ == Type::Bool; }
  bool is_number() const noexcept { return type_ == Type::Number; }
  bool is_string() const noexcept { return type_ == Type::String; }
  bool is_array() const noexcept { return type_ == Type::Array; }
  bool is_object() const noexcept { return type_ == Type::Object; }

  bool as_bool(bool fallback = false) const noexcept;
  double as_number(double fallback = 0.0) const noexcept;
  std::int64_t as_i64(std::int64_t fallback = 0) const noexcept;
  std::string as_string(std::string fallback = {}) const;
  const std::string& string_ref() const noexcept { return string_; }

  const Array& items() const noexcept { return array_; }
  Array& items() noexcept { return array_; }
  const Object& fields() const noexcept { return object_; }
  Object& fields() noexcept { return object_; }

  const Value* find(std::string_view key) const noexcept;
  Value& operator[](std::string_view key);
  void push(Value value);

  /// Serialize. Throws LimitError if the nesting depth exceeds 64.
  std::string dump() const;

 private:
  void dump_to(std::string& out, int depth) const;

  Type type_ = Type::Null;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  Array array_;
  Object object_;
};

struct ParseError {
  std::size_t offset = 0;
  std::string message;
};

struct ParseOutcome {
  bool ok = false;
  Value value;
  ParseError error;
};

/// Parse strict JSON with a 1 MiB input limit, depth 32, bounded string and
/// collection sizes. Never throws on malformed input.
ParseOutcome parse(std::string_view text);

/// Convenience wrappers used by the test suite and fuzzers.
std::string quote(std::string_view text);
bool valid_utf8(std::string_view text) noexcept;

}  // namespace tempo::json
