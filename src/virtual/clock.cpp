#include "tempo/clock.hpp"

#include <cstdio>
#include <limits>
#include <string>

namespace tempo {

VirtualClock::VirtualClock(std::uint64_t start_ns) noexcept
    : now_ns_(start_ns == 0 ? 1 : start_ns) {}

bool VirtualClock::advance_to(std::uint64_t target_ns) noexcept {
  if (target_ns < now_ns_) {
    return false;
  }
  now_ns_ = target_ns;
  return true;
}

void VirtualClock::advance_by(std::uint64_t delta_ns) noexcept {
  if (std::numeric_limits<std::uint64_t>::max() - now_ns_ < delta_ns) {
    now_ns_ = std::numeric_limits<std::uint64_t>::max();
    return;
  }
  now_ns_ += delta_ns;
}

std::string format_ns(std::uint64_t ns) {
  char buffer[64];
  if (ns < 1'000) {
    std::snprintf(buffer, sizeof(buffer), "%lluns",
                  static_cast<unsigned long long>(ns));
  } else if (ns < 1'000'000) {
    std::snprintf(buffer, sizeof(buffer), "%.3fus",
                  static_cast<double>(ns) / 1'000.0);
  } else if (ns < 1'000'000'000) {
    std::snprintf(buffer, sizeof(buffer), "%.3fms",
                  static_cast<double>(ns) / 1'000'000.0);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.3fs",
                  static_cast<double>(ns) / 1'000'000'000.0);
  }
  return std::string(buffer);
}

}  // namespace tempo
