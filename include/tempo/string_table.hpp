#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "tempo/limits.hpp"

namespace tempo {

/// Deduplicating string table used by the record writer. Index 0 is always the
/// empty string, so an empty reference is valid and means "".
///
/// Thread-safety: not thread-safe; used by one writer.
class StringTable {
 public:
  StringTable();

  /// Intern a string. Throws LimitError if the string exceeds the per-string
  /// limit or the table exceeds the count/byte limits.
  std::uint32_t intern(std::string_view value);

  const std::string& at(std::uint32_t index) const;
  std::uint32_t size() const noexcept { return static_cast<std::uint32_t>(values_.size()); }
  const std::vector<std::string>& values() const noexcept { return values_; }

  /// Loaded records append directly; throws LimitError on violation.
  void add_loaded(std::string value);

  /// Remove every entry, including the implicit empty string. Used by the
  /// record loader so on-disk indices map exactly.
  void clear_all();

  void clear();

 private:
  std::vector<std::string> values_;
  std::map<std::string, std::uint32_t> index_;
  std::size_t total_bytes_ = 0;
};

}  // namespace tempo
