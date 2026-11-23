#include "tempo/json.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "tempo/format.hpp"
#include "tempo/ids.hpp"
#include "tempo/limits.hpp"

namespace tempo::json {
namespace {

constexpr int kMaxDepth = 32;
constexpr std::size_t kMaxString = 65'536;
constexpr std::size_t kMaxItems = 100'000;
constexpr std::size_t kMaxFields = 2'048;
constexpr std::size_t kMaxKey = 256;
constexpr int kMaxDumpDepth = 64;

void append_utf8(std::string& out, std::uint32_t code) {
  if (code <= 0x7F) {
    out.push_back(static_cast<char>(code));
  } else if (code <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else if (code <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  }
}

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  ParseOutcome run() {
    ParseOutcome outcome;
    skip_ws();
    Value value;
    if (!parse_value(value, 0)) {
      outcome.error = error_;
      return outcome;
    }
    skip_ws();
    if (pos_ != text_.size()) {
      fail("trailing characters after JSON value");
      outcome.error = error_;
      return outcome;
    }
    outcome.ok = true;
    outcome.value = std::move(value);
    return outcome;
  }

 private:
  void fail(std::string message) {
    if (error_.message.empty()) {
      error_.offset = pos_;
      error_.message = std::move(message);
    }
  }

  void skip_ws() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool consume(char expected) {
    if (pos_ < text_.size() && text_[pos_] == expected) {
      ++pos_;
      return true;
    }
    return false;
  }

  bool parse_value(Value& out, int depth) {
    if (depth > kMaxDepth) {
      fail("JSON nesting exceeds 32 levels");
      return false;
    }
    skip_ws();
    if (pos_ >= text_.size()) {
      fail("unexpected end of JSON");
      return false;
    }
    const char c = text_[pos_];
    switch (c) {
      case 'n':
        return parse_literal("null", Value(), out);
      case 't':
        return parse_literal("true", Value(true), out);
      case 'f':
        return parse_literal("false", Value(false), out);
      case '"': {
        std::string value;
        if (!parse_string(value)) {
          return false;
        }
        out = Value(std::move(value));
        return true;
      }
      case '[':
        return parse_array(out, depth);
      case '{':
        return parse_object(out, depth);
      default:
        return parse_number(out);
    }
  }

  bool parse_literal(std::string_view keyword, Value value, Value& out) {
    if (text_.substr(pos_, keyword.size()) != keyword) {
      fail("invalid literal");
      return false;
    }
    pos_ += keyword.size();
    out = std::move(value);
    return true;
  }

  bool parse_array(Value& out, int depth) {
    ++pos_;  // '['
    Value array = Value::array();
    skip_ws();
    if (consume(']')) {
      out = std::move(array);
      return true;
    }
    for (;;) {
      if (array.items().size() >= kMaxItems) {
        fail("JSON array exceeds 100000 items");
        return false;
      }
      Value item;
      if (!parse_value(item, depth + 1)) {
        return false;
      }
      array.push(std::move(item));
      skip_ws();
      if (consume(']')) {
        break;
      }
      if (!consume(',')) {
        fail("expected ',' or ']' in array");
        return false;
      }
    }
    out = std::move(array);
    return true;
  }

  bool parse_object(Value& out, int depth) {
    ++pos_;  // '{'
    Value object = Value::object();
    skip_ws();
    if (consume('}')) {
      out = std::move(object);
      return true;
    }
    for (;;) {
      if (object.fields().size() >= kMaxFields) {
        fail("JSON object exceeds 2048 fields");
        return false;
      }
      std::string key;
      skip_ws();
      if (!parse_string(key)) {
        return false;
      }
      if (key.size() > kMaxKey) {
        fail("JSON object key exceeds 256 bytes");
        return false;
      }
      skip_ws();
      if (!consume(':')) {
        fail("expected ':' after object key");
        return false;
      }
      Value value;
      if (!parse_value(value, depth + 1)) {
        return false;
      }
      if (object.fields().find(key) != object.fields().end()) {
        fail("duplicate object key");
        return false;
      }
      object.fields().emplace(std::move(key), std::move(value));
      skip_ws();
      if (consume('}')) {
        break;
      }
      if (!consume(',')) {
        fail("expected ',' or '}' in object");
        return false;
      }
    }
    out = std::move(object);
    return true;
  }

  int hex_digit(char c) const {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    return -1;
  }

  bool parse_hex4(std::uint32_t& value) {
    if (pos_ + 4 > text_.size()) {
      fail("truncated \\u escape");
      return false;
    }
    value = 0;
    for (int i = 0; i < 4; ++i) {
      const int digit = hex_digit(text_[pos_ + i]);
      if (digit < 0) {
        fail("invalid \\u escape");
        return false;
      }
      value = (value << 4) | static_cast<std::uint32_t>(digit);
    }
    pos_ += 4;
    return true;
  }

  bool parse_string(std::string& out) {
    if (!consume('"')) {
      fail("expected string");
      return false;
    }
    out.clear();
    while (pos_ < text_.size()) {
      const unsigned char c = static_cast<unsigned char>(text_[pos_]);
      if (c == '"') {
        ++pos_;
        return true;
      }
      if (c < 0x20) {
        fail("unescaped control character in string");
        return false;
      }
      if (c != '\\') {
        out.push_back(static_cast<char>(c));
        if (out.size() > kMaxString) {
          fail("JSON string exceeds 65536 bytes");
          return false;
        }
        ++pos_;
        continue;
      }
      ++pos_;
      if (pos_ >= text_.size()) {
        fail("truncated escape in string");
        return false;
      }
      const char escape = text_[pos_++];
      switch (escape) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          std::uint32_t code = 0;
          if (!parse_hex4(code)) {
            return false;
          }
          if (code >= 0xD800 && code <= 0xDBFF) {
            if (pos_ + 2 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
              fail("unpaired high surrogate");
              return false;
            }
            pos_ += 2;
            std::uint32_t low = 0;
            if (!parse_hex4(low)) {
              return false;
            }
            if (low < 0xDC00 || low > 0xDFFF) {
              fail("invalid low surrogate");
              return false;
            }
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
          } else if (code >= 0xDC00 && code <= 0xDFFF) {
            fail("unpaired low surrogate");
            return false;
          }
          append_utf8(out, code);
          break;
        }
        default:
          fail("invalid escape in string");
          return false;
      }
      if (out.size() > kMaxString) {
        fail("JSON string exceeds 65536 bytes");
        return false;
      }
    }
    fail("unterminated string");
    return false;
  }

  bool parse_number(Value& out) {
    const std::size_t start = pos_;
    if (pos_ < text_.size() && text_[pos_] == '-') {
      ++pos_;
    }
    if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
      fail("invalid number");
      return false;
    }
    if (text_[pos_] == '0') {
      ++pos_;
      if (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        fail("leading zero in number");
        return false;
      }
    } else {
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
        fail("missing fraction digits");
        return false;
      }
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
        ++pos_;
      }
      if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
        fail("missing exponent digits");
        return false;
      }
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    const std::string number_text(text_.substr(start, pos_ - start));
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(number_text.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(value)) {
      fail("number out of range");
      return false;
    }
    out = Value(value);
    return true;
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  ParseError error_;
};

}  // namespace

Value Value::array() {
  Value value;
  value.type_ = Type::Array;
  return value;
}

Value Value::object() {
  Value value;
  value.type_ = Type::Object;
  return value;
}

bool Value::as_bool(bool fallback) const noexcept {
  return type_ == Type::Bool ? bool_ : fallback;
}

double Value::as_number(double fallback) const noexcept {
  return type_ == Type::Number ? number_ : fallback;
}

std::int64_t Value::as_i64(std::int64_t fallback) const noexcept {
  if (type_ != Type::Number || !std::isfinite(number_)) {
    return fallback;
  }
  if (number_ < -9.007199254740992e15 || number_ > 9.007199254740992e15) {
    return fallback;
  }
  return static_cast<std::int64_t>(number_);
}

std::string Value::as_string(std::string fallback) const {
  return type_ == Type::String ? string_ : std::move(fallback);
}

const Value* Value::find(std::string_view key) const noexcept {
  if (type_ != Type::Object) {
    return nullptr;
  }
  auto it = object_.find(key);
  return it == object_.end() ? nullptr : &it->second;
}

Value& Value::operator[](std::string_view key) {
  if (type_ == Type::Null) {
    type_ = Type::Object;
  }
  if (type_ != Type::Object) {
    throw LimitError("JSON value is not an object");
  }
  if (key.size() > kMaxKey) {
    throw LimitError("JSON key exceeds 256 bytes");
  }
  if (object_.size() >= kMaxFields && object_.find(key) == object_.end()) {
    throw LimitError("JSON object exceeds 2048 fields");
  }
  return object_[std::string(key)];
}

void Value::push(Value value) {
  if (type_ == Type::Null) {
    type_ = Type::Array;
  }
  if (type_ != Type::Array) {
    throw LimitError("JSON value is not an array");
  }
  if (array_.size() >= kMaxItems) {
    throw LimitError("JSON array exceeds 100000 items");
  }
  array_.push_back(std::move(value));
}

std::string quote(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (unsigned char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          out += buffer;
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  out.push_back('"');
  return out;
}

void Value::dump_to(std::string& out, int depth) const {
  if (depth > kMaxDumpDepth) {
    throw LimitError("JSON dump depth exceeds 64");
  }
  switch (type_) {
    case Type::Null:
      out += "null";
      break;
    case Type::Bool:
      out += bool_ ? "true" : "false";
      break;
    case Type::Number: {
      if (!std::isfinite(number_)) {
        out += "null";
        break;
      }
      if (std::fabs(number_) < 9.0e15 && number_ == std::floor(number_)) {
        out += std::to_string(static_cast<std::int64_t>(number_));
      } else {
        out += format_json_number(number_);
      }
      break;
    }
    case Type::String:
      out += quote(string_);
      break;
    case Type::Array: {
      out.push_back('[');
      bool first = true;
      for (const auto& item : array_) {
        if (!first) {
          out.push_back(',');
        }
        first = false;
        item.dump_to(out, depth + 1);
      }
      out.push_back(']');
      break;
    }
    case Type::Object: {
      out.push_back('{');
      bool first = true;
      for (const auto& [key, value] : object_) {
        if (!first) {
          out.push_back(',');
        }
        first = false;
        out += quote(key);
        out.push_back(':');
        value.dump_to(out, depth + 1);
      }
      out.push_back('}');
      break;
    }
  }
}

std::string Value::dump() const {
  std::string out;
  dump_to(out, 0);
  return out;
}

ParseOutcome parse(std::string_view text) {
  ParseOutcome outcome;
  if (text.size() > limits::kMaxJsonBytes) {
    outcome.error.offset = 0;
    outcome.error.message = "JSON exceeds 1 MiB limit";
    return outcome;
  }
  Parser parser(text);
  return parser.run();
}

bool valid_utf8(std::string_view text) noexcept {
  std::size_t i = 0;
  while (i < text.size()) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    std::size_t extra = 0;
    if (c < 0x80) {
      ++i;
      continue;
    } else if ((c & 0xE0) == 0xC0) {
      extra = 1;
      if (c < 0xC2) {
        return false;
      }
    } else if ((c & 0xF0) == 0xE0) {
      extra = 2;
    } else if ((c & 0xF8) == 0xF0) {
      extra = 3;
      if (c > 0xF4) {
        return false;
      }
    } else {
      return false;
    }
    if (i + extra >= text.size()) {
      return false;
    }
    for (std::size_t j = 1; j <= extra; ++j) {
      const unsigned char cc = static_cast<unsigned char>(text[i + j]);
      if ((cc & 0xC0) != 0x80) {
        return false;
      }
    }
    i += extra + 1;
  }
  return true;
}

}  // namespace tempo::json
