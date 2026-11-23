#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "tempo/ids.hpp"

namespace tempo {

/// Every observable action in a Tempo run is one of these events. Numeric values
/// are part of the record format (v1) and must never be renumbered.
enum class EventType : std::uint8_t {
  Unknown = 0,
  ScenarioStart = 1,
  ScenarioEnd = 2,
  Spawn = 3,
  Step = 4,
  Yield = 5,
  Sleep = 6,
  Wake = 7,
  Exit = 8,
  Cancel = 9,
  Error = 10,
  Log = 11,
  Watch = 12,
  Note = 13,
  Rng = 14,
  ClockJump = 15,
  Join = 16,

  FileRead = 20,
  FileWrite = 21,
  EnvGet = 22,
  SocketConnect = 23,
  SocketSend = 24,
  SocketRecv = 25,
  SocketClose = 26,
};

/// A recorded observable action.
///
/// `a`, `b`, `c`, `d` carry numeric payload whose meaning depends on the type
/// (see docs/RECORD_FORMAT.md). `s1`, `s2` are short UTF-8 strings; `bytes` is a
/// bounded binary payload. All sizes are limited by include/tempo/limits.hpp.
struct Event {
  EventType type = EventType::Unknown;
  TaskId task = kNoTask;
  std::uint64_t a = 0;
  std::uint64_t b = 0;
  std::uint64_t c = 0;
  std::uint64_t d = 0;
  std::string s1;
  std::string s2;
  std::string bytes;

  bool operator==(const Event& other) const noexcept;
  bool operator!=(const Event& other) const noexcept { return !(*this == other); }
};

/// Stable display name, e.g. "FileRead".
std::string_view event_type_name(EventType type) noexcept;

/// True for events produced by capability calls.
bool is_capability_event(EventType type) noexcept;

/// True for task lifecycle events (spawn/step/yield/sleep/wake/exit/cancel).
bool is_lifecycle_event(EventType type) noexcept;

/// One-line human summary; used by the UI and diff output. Never throws.
std::string event_summary(const Event& event);

/// Rolling FNV-1a 64 hash over the canonical event encoding. Used for the
/// determinism hash and record fingerprints.
std::uint64_t event_hash(std::uint64_t state, const Event& event) noexcept;

/// FNV-1a 64 over an arbitrary byte range.
std::uint64_t fnv1a64(std::uint64_t state, std::string_view bytes) noexcept;

}  // namespace tempo
