#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "tempo/host.hpp"
#include "tempo/limits.hpp"

namespace tempo {

/// Configuration for the real-system capability provider.
///
/// File access is sandboxed to `data_root`: relative paths only, no `..`, no
/// symlinks, and every read/write is capped. Network access is restricted to
/// loopback addresses. Environment reads can be disabled entirely.
struct SystemCapabilityOptions {
  /// Existing directory used as the root for all file capabilities. If empty,
  /// file capabilities fail with a recorded error instead of touching the OS.
  std::string data_root;
  bool allow_env = true;
  bool allow_loopback_sockets = true;
  std::uint32_t socket_timeout_ms = 2000;
  std::size_t max_open_sockets = limits::kMaxSocketConnections;
};

/// Executes real file, environment and loopback-socket effects for recording.
///
/// Recorder wrappers delegate capability calls here, so `emit` is a no-op by
/// default. For headless runs you can install an event sink to observe events.
///
/// Thread-safety: single-threaded, like the scheduler that owns it. The host
/// keeps OS file descriptors and closes them on destruction.
class SystemHost final : public RunHost {
 public:
  explicit SystemHost(SystemCapabilityOptions options);
  ~SystemHost() override;

  SystemHost(const SystemHost&) = delete;
  SystemHost& operator=(const SystemHost&) = delete;

  TaskId choose_ready(const std::vector<TaskId>& ready, std::uint64_t draw_index,
                      std::uint64_t step) override;

  CapResult file_read(const std::string& path) override;
  CapResult file_write(const std::string& path, const std::string& data) override;
  CapResult env_get(const std::string& name) override;

  SocketConnectResult socket_connect(const std::string& host, std::uint16_t port) override;
  CapResult socket_send(std::uint32_t handle, const std::string& data) override;
  CapResult socket_recv(std::uint32_t handle, std::uint32_t max_bytes) override;
  CapResult socket_close(std::uint32_t handle) override;

  void emit(Event event) override;

  void set_event_sink(std::function<void(Event)> sink) { sink_ = std::move(sink); }
  const SystemCapabilityOptions& options() const noexcept { return options_; }
  const std::string& data_root() const noexcept { return options_.data_root; }

 private:
  bool safe_relative_path(const std::string& path, std::string* full) const;
  void close_all() noexcept;

  SystemCapabilityOptions options_;
  bool root_ok_ = false;
  std::map<std::uint32_t, int> sockets_;
  std::uint32_t next_handle_ = 1;
  std::function<void(Event)> sink_;
};

/// Lexical path policy used by SystemHost; exposed for direct unit testing.
/// Returns true for a relative, normalized path with no empty, "." or ".."
/// components and no NUL bytes.
bool safe_relative_path_policy(const std::string& path) noexcept;

}  // namespace tempo
