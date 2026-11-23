#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tempo/event.hpp"
#include "tempo/ids.hpp"

namespace tempo {

/// Result of a single mediated capability call.
///
/// `ok == false` is a *recorded* outcome, not an infrastructure error: replay
/// returns exactly the same failure the recording saw. `data` is the payload
/// (file contents, environment value, bytes sent/received), `num` is a numeric
/// result such as a byte count.
struct CapResult {
  bool ok = false;
  std::string error;
  std::string data;
  std::uint64_t num = 0;
};

struct SocketConnectResult {
  bool ok = false;
  std::string error;
  std::uint32_t handle = 0;
};

/// Interface between the deterministic scheduler and the outside world.
///
/// Exactly one host is active per run. It decides which ready task runs next
/// and supplies every external effect. Recording hosts execute real effects and
/// store them; replay hosts return stored values without touching the OS.
///
/// Thread-safety: a host is used only by the scheduler thread that owns it. It
/// must not be shared between concurrent runs.
///
/// Lifetime: the scheduler keeps a non-owning pointer; the host must outlive the
/// scheduler run.
class RunHost {
 public:
  virtual ~RunHost() = default;

  /// Choose the next task from `ready`. `draw_index` is a scheduler-provided
  /// deterministic index into `ready` (already reduced modulo size); `step` is
  /// the 0-based scheduler step. Implementations must return a member of
  /// `ready` or throw; returning anything else is treated as a host bug.
  virtual TaskId choose_ready(const std::vector<TaskId>& ready, std::uint64_t draw_index,
                              std::uint64_t step) = 0;

  virtual CapResult file_read(const std::string& path) = 0;
  virtual CapResult file_write(const std::string& path, const std::string& data) = 0;
  virtual CapResult env_get(const std::string& name) = 0;

  virtual SocketConnectResult socket_connect(const std::string& host, std::uint16_t port) = 0;
  virtual CapResult socket_send(std::uint32_t handle, const std::string& data) = 0;
  virtual CapResult socket_recv(std::uint32_t handle, std::uint32_t max_bytes) = 0;
  virtual CapResult socket_close(std::uint32_t handle) = 0;

  /// Consume one event. Recorders append it; replayers compare it with the
  /// recording and stop recording further diagnostics after the first mismatch.
  virtual void emit(Event event) = 0;
};

}  // namespace tempo
