#include "tempo/record.hpp"

#include <cstdio>
#include <utility>

namespace tempo {

std::uint64_t Record::compute_hash() const noexcept {
  std::uint64_t value = 14695981039346656037ull;
  for (const auto& event : events) {
    value = event_hash(value, event);
  }
  return value;
}

std::uint64_t Record::recompute_hash() noexcept {
  hash = compute_hash();
  return hash;
}

std::string Record::hash_hex() const {
  char buffer[17];
  std::snprintf(buffer, sizeof(buffer), "%016llx",
                static_cast<unsigned long long>(hash));
  return std::string(buffer);
}

}  // namespace tempo
