#include "tempo/string_table.hpp"

#include <utility>

#include "tempo/ids.hpp"

namespace tempo {

StringTable::StringTable() {
  values_.emplace_back();
  index_.emplace(std::string(), 0);
}

void StringTable::clear_all() {
  values_.clear();
  index_.clear();
  total_bytes_ = 0;
}

void StringTable::clear() {
  values_.clear();
  index_.clear();
  total_bytes_ = 0;
  values_.emplace_back();
  index_.emplace(std::string(), 0);
}

std::uint32_t StringTable::intern(std::string_view value) {
  if (value.size() > limits::kMaxStringBytes) {
    throw LimitError("string exceeds 65536 byte record limit");
  }
  auto it = index_.find(std::string(value));
  if (it != index_.end()) {
    return it->second;
  }
  if (values_.size() >= limits::kMaxStringCount) {
    throw LimitError("string table count limit reached");
  }
  if (total_bytes_ + value.size() > limits::kMaxStringTableBytes) {
    throw LimitError("string table byte limit reached");
  }
  const std::uint32_t id = static_cast<std::uint32_t>(values_.size());
  values_.emplace_back(value);
  index_.emplace(values_.back(), id);
  total_bytes_ += value.size();
  return id;
}

const std::string& StringTable::at(std::uint32_t index_value) const {
  if (index_value >= values_.size()) {
    throw LimitError("string reference out of range");
  }
  return values_[index_value];
}

void StringTable::add_loaded(std::string value) {
  if (value.size() > limits::kMaxStringBytes) {
    throw LimitError("string exceeds 65536 byte record limit");
  }
  if (values_.size() >= limits::kMaxStringCount) {
    throw LimitError("string table count limit reached");
  }
  if (total_bytes_ + value.size() > limits::kMaxStringTableBytes) {
    throw LimitError("string table byte limit reached");
  }
  const std::uint32_t id = static_cast<std::uint32_t>(values_.size());
  values_.push_back(std::move(value));
  index_.emplace(values_.back(), id);
  total_bytes_ += values_.back().size();
}

}  // namespace tempo
