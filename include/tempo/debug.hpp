#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "tempo/event.hpp"
#include "tempo/limits.hpp"
#include "tempo/record.hpp"

namespace tempo {

/// Reconstructed inspector state: task id -> watch name -> value.
using WatchMap = std::map<TaskId, std::map<std::string, std::string>>;

/// A breakpoint over the recorded event log. All populated criteria must match.
struct Breakpoint {
  std::uint32_t id = 0;
  bool enabled = true;
  bool has_type = false;
  EventType type = EventType::Unknown;
  bool has_task = false;
  TaskId task = kNoTask;
  /// Case-sensitive substring searched in s1, s2 and the event type name.
  bool has_text = false;
  std::string text;
};

struct DebugFrame {
  std::size_t index = 0;
  Event event;
  WatchMap state;
  bool is_divergence = false;
};

struct TaskLane {
  TaskId task = kNoTask;
  std::string name;
  std::size_t event_count = 0;
  std::vector<std::size_t> events;
  std::uint64_t spawn_ns = 0;
  std::uint64_t end_ns = 0;
  bool exited = false;
  bool failed = false;
};

/// Read-only debugger over one record.
///
/// State is reconstructed from Watch events. Checkpoints are built only when a
/// conservative memory estimate stays under the 4 MiB budget; otherwise state
/// is rebuilt by scanning from the start. Both paths return identical state.
/// This keeps reverse stepping exact and memory bounded.
///
/// Thread-safety: not thread-safe. The workbench uses one per UI thread; callers
/// must serialize access. The stored record must outlive the session.
class DebugSession {
 public:
  explicit DebugSession(std::shared_ptr<const Record> record);

  const Record& record() const noexcept { return *record_; }
  std::size_t event_count() const noexcept { return record_->events.size(); }
  bool valid() const noexcept { return record_ != nullptr; }
  bool has_checkpoints() const noexcept { return !checkpoints_.empty(); }

  /// Event at `index` or nullptr when out of range.
  const Event* event_at(std::size_t index) const noexcept;

  /// Exact scenario state after event `index`. Out-of-range indices yield an
  /// empty state.
  WatchMap state_at(std::size_t index) const;

  /// Combined frame used by the inspector.
  DebugFrame frame_at(std::size_t index) const;

  // -- navigation ----------------------------------------------------------
  std::optional<std::size_t> step_forward(std::size_t index) const noexcept;
  std::optional<std::size_t> step_back(std::size_t index) const noexcept;

  // -- breakpoints ---------------------------------------------------------
  std::vector<Breakpoint> breakpoints() const;
  /// Add a breakpoint (assigning a fresh id) or return nullopt at the limit.
  std::optional<std::uint32_t> add_breakpoint(Breakpoint breakpoint);
  bool remove_breakpoint(std::uint32_t id);
  bool set_breakpoint_enabled(std::uint32_t id, bool enabled);
  void clear_breakpoints();
  bool matches(const Event& event) const;
  /// Strictly after `from` when forward, strictly before when backward.
  std::optional<std::size_t> next_breakpoint(std::size_t from, bool forward) const;

  // -- divergence annotation ----------------------------------------------
  void set_divergence(std::optional<std::size_t> index, std::string reason);
  std::optional<std::size_t> divergence_index() const noexcept { return divergence_index_; }
  const std::string& divergence_reason() const noexcept { return divergence_reason_; }

  // -- timeline ------------------------------------------------------------
  std::vector<TaskLane> timeline() const;

  // -- state helpers -------------------------------------------------------
  static std::string render_state(const WatchMap& state);
  static std::size_t state_key_count(const WatchMap& state) noexcept;

 private:
  void build_checkpoints();
  void apply_event(WatchMap& state, const Event& event) const;

  std::shared_ptr<const Record> record_;
  std::map<std::size_t, WatchMap> checkpoints_;
  std::vector<Breakpoint> breakpoints_;
  std::uint32_t next_breakpoint_id_ = 1;
  std::optional<std::size_t> divergence_index_;
  std::string divergence_reason_;
};

}  // namespace tempo
