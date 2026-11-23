#pragma once

#include <cstdint>
#include <string>

namespace tempo {

/// Virtual, monotonic wall clock for one scheduler run.
///
/// Time only moves when the scheduler has no ready task and jumps to the
/// earliest sleep deadline. Reading it never touches the OS clock, so replay
/// reproduces every timestamp exactly.
///
/// Thread-safety: not thread-safe; owned by the scheduler.
class VirtualClock {
 public:
  explicit VirtualClock(std::uint64_t start_ns = 1'000'000) noexcept;

  std::uint64_t now_ns() const noexcept { return now_ns_; }

  /// Jump forward. Returns false without changing time if `target < now_ns_`.
  bool advance_to(std::uint64_t target_ns) noexcept;

  /// Add `delta_ns`; saturates at UINT64_MAX (scheduler enforces its own cap).
  void advance_by(std::uint64_t delta_ns) noexcept;

 private:
  std::uint64_t now_ns_;
};

/// Format a virtual duration in nanoseconds for display, e.g. "1.250ms".
std::string format_ns(std::uint64_t ns);

}  // namespace tempo
