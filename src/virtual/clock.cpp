#include "tempo/clock.hpp"

#include "tempo/format.hpp"

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
  if (ns < 1'000) {
    return std::to_string(ns) + "ns";
  }
  if (ns < 1'000'000) {
    return format_fixed(static_cast<double>(ns) / 1'000.0, 3) + "us";
  }
  if (ns < 1'000'000'000) {
    return format_fixed(static_cast<double>(ns) / 1'000'000.0, 3) + "ms";
  }
  return format_fixed(static_cast<double>(ns) / 1'000'000'000.0, 3) + "s";
}

}  // namespace tempo
