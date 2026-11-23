#pragma once

#include <array>
#include <cstdint>

namespace tempo {

/// Deterministic xoshiro256** generator.
///
/// Seeded through SplitMix64. The stream is fully specified by the seed and the
/// format (v1); replay re-creates it rather than replaying RNG state.
///
/// Thread-safety: not thread-safe; owned by the single-threaded scheduler.
class Rng {
 public:
  explicit Rng(std::uint64_t seed = 0x9E3779B97F4A7C15ull) noexcept;

  void reseed(std::uint64_t seed) noexcept;

  /// Next raw 64-bit value.
  std::uint64_t next_u64() noexcept;

  /// Uniform value in [0, bound); returns 0 for bound == 0. Uses rejection
  /// sampling, so it is unbiased.
  std::uint64_t bounded(std::uint64_t bound) noexcept;

  /// Uniform double in [0, 1).
  double next_double() noexcept;

 private:
  std::array<std::uint64_t, 4> state_{};
};

}  // namespace tempo
