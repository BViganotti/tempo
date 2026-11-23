#include "tempo/rng.hpp"

namespace tempo {
namespace {

constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept {
  return (x << k) | (x >> (64 - k));
}

std::uint64_t splitmix64(std::uint64_t& state) noexcept {
  state += 0x9E3779B97F4A7C15ull;
  std::uint64_t z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

}  // namespace

Rng::Rng(std::uint64_t seed) noexcept {
  reseed(seed);
}

void Rng::reseed(std::uint64_t seed) noexcept {
  std::uint64_t s = seed;
  state_[0] = splitmix64(s);
  state_[1] = splitmix64(s);
  state_[2] = splitmix64(s);
  state_[3] = splitmix64(s);
  // xoshiro must never be all-zero.
  if ((state_[0] | state_[1] | state_[2] | state_[3]) == 0) {
    state_[0] = 0x9E3779B97F4A7C15ull;
  }
}

std::uint64_t Rng::next_u64() noexcept {
  const std::uint64_t result = rotl(state_[1] * 5, 7) * 9;
  const std::uint64_t t = state_[1] << 17;
  state_[2] ^= state_[0];
  state_[3] ^= state_[1];
  state_[1] ^= state_[2];
  state_[0] ^= state_[3];
  state_[2] ^= t;
  state_[3] = rotl(state_[3], 45);
  return result;
}

std::uint64_t Rng::bounded(std::uint64_t bound) noexcept {
  if (bound <= 1) {
    return 0;
  }
  const std::uint64_t threshold = (0ull - bound) % bound;
  for (;;) {
    const std::uint64_t value = next_u64();
    if (value >= threshold) {
      return value % bound;
    }
  }
}

double Rng::next_double() noexcept {
  return static_cast<double>(next_u64() >> 11) * (1.0 / 9007199254740992.0);
}

}  // namespace tempo
