#pragma once

#include <cstdint>
#include <string>

namespace tempo {

/// Identifier of a task inside one scheduler run. IDs start at 1 and are never
/// reused within a run; 0 (`kNoTask`) means "no task" (the system itself).
using TaskId = std::uint32_t;

inline constexpr TaskId kNoTask = 0;

/// Identifier of a persisted record. Assigned by the workbench, never by the
/// record format itself.
using RecordId = std::uint64_t;

/// Thrown by Tempo awaitables when a cancelled task resumes, and by
/// Context::check_cancelled(). Scenarios may catch it to clean up.
class CancelledError : public std::exception {
 public:
  const char* what() const noexcept override { return "tempo: task cancelled"; }
};

/// Thrown when a scenario exceeds a hard limit (watch value, log message,
/// nested spawn, ...). It is treated as a task failure, never ignored.
class LimitError : public std::exception {
 public:
  explicit LimitError(std::string message) : message_(std::move(message)) {}
  const char* what() const noexcept override { return message_.c_str(); }

 private:
  std::string message_;
};

}  // namespace tempo
