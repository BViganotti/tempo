#pragma once

#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tempo/clock.hpp"
#include "tempo/host.hpp"
#include "tempo/ids.hpp"
#include "tempo/limits.hpp"
#include "tempo/rng.hpp"

namespace tempo {

class Scheduler;
class Context;

/// A cooperative task. Created by invoking a coroutine that returns Task and is
/// `co_await`-ing Tempo awaitables. A Task starts suspended; the scheduler owns
/// it after spawn().
///
/// Move-only. Destroying an unstarted Task releases its frame without running
/// it. Tasks are not thread-safe and must stay on the scheduler's thread.
class Task {
 public:
  struct promise_type {
    Scheduler* scheduler = nullptr;
    TaskId id = kNoTask;
    std::exception_ptr error;

    Task get_return_object() noexcept;
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { error = std::current_exception(); }
  };

  using handle_type = std::coroutine_handle<promise_type>;

  Task() noexcept = default;
  explicit Task(handle_type handle) noexcept : handle_(handle) {}
  Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
  Task& operator=(Task&& other) noexcept;
  ~Task();

  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;

  bool valid() const noexcept { return static_cast<bool>(handle_); }
  handle_type release() noexcept { return std::exchange(handle_, {}); }

 private:
  handle_type handle_{};
};

// ---------------------------------------------------------------------------
// Awaitables
// ---------------------------------------------------------------------------

/// Suspend and immediately re-enqueue the task at the back of the ready queue.
class YieldAwaiter {
 public:
  YieldAwaiter(Scheduler* scheduler, TaskId id) noexcept;
  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<>) const;
  void await_resume() const;

 private:
  Scheduler* scheduler_;
  TaskId id_;
};

/// Suspend until the virtual clock reaches `deadline_ns`.
class SleepAwaiter {
 public:
  SleepAwaiter(Scheduler* scheduler, TaskId id, std::uint64_t deadline_ns) noexcept;
  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<>) const;
  void await_resume() const;

 private:
  Scheduler* scheduler_;
  TaskId id_;
  std::uint64_t deadline_ns_;
};

/// Suspend until another task reaches a terminal state. Returns true if the
/// target finished successfully, false if it failed or was cancelled.
class JoinAwaiter {
 public:
  JoinAwaiter(Scheduler* scheduler, TaskId waiter, TaskId target) noexcept;
  bool await_ready() const noexcept;
  void await_suspend(std::coroutine_handle<>) const;
  bool await_resume() const;

 private:
  Scheduler* scheduler_;
  TaskId waiter_;
  TaskId target_;
};

/// Per-task view of the runtime. Contexts are cheap value types (scheduler
/// pointer + task id) and remain valid as long as the task exists. Pass by value.
///
/// All methods must be called from the task's own coroutine (except cancel,
/// which may target another task from the currently running task).
class Context {
 public:
  Context() noexcept = default;
  Context(Scheduler* scheduler, TaskId id) noexcept : scheduler_(scheduler), id_(id) {}

  TaskId id() const noexcept { return id_; }
  bool valid() const noexcept { return scheduler_ != nullptr && id_ != kNoTask; }
  Scheduler* scheduler() const noexcept { return scheduler_; }

  std::uint64_t now_ns() const noexcept;
  std::uint64_t now_ms() const noexcept;

  YieldAwaiter yield() const noexcept { return YieldAwaiter(scheduler_, id_); }
  SleepAwaiter sleep_ns(std::uint64_t duration_ns) const;
  SleepAwaiter sleep_ms(std::uint64_t duration_ms) const;
  JoinAwaiter join(TaskId target) const noexcept {
    return JoinAwaiter(scheduler_, id_, target);
  }

  bool cancelled() const noexcept;
  void check_cancelled() const;
  void cancel(TaskId target) const;

  /// Spawn a child task. The body is stored by the scheduler for the lifetime of
  /// the task; captures are therefore safe. Throws LimitError at the task limit.
  TaskId spawn(std::string name, std::function<Task(Context)> body) const;

  /// Update a watch variable (inspector state). Emitted only when changed.
  /// Throws LimitError when the name or value exceeds its limit.
  void watch(std::string_view name, std::string_view value) const;
  void watch(std::string_view name, const char* value) const {
    watch(name, std::string_view(value == nullptr ? "" : value));
  }
  void watch(std::string_view name, std::int64_t value) const;
  void watch(std::string_view name, double value) const;
  void watch(std::string_view name, bool value) const;

  /// Record a scenario output/annotation. Unlike watch(), notes never affect
  /// reconstructed state; they are metadata for the UI.
  void note(std::string_view key, std::string_view value) const;
  void note(std::string_view key, const char* value) const {
    note(key, std::string_view(value == nullptr ? "" : value));
  }

  /// Human log line for the timeline. Long messages are explicitly truncated.
  void log(std::string_view message) const;

  /// Deterministic random draw. The value is recorded and replayed.
  std::uint64_t random_u64() const;

  CapResult read_file(std::string_view path) const;
  CapResult write_file(std::string_view path, std::string_view data) const;
  CapResult env_get(std::string_view name) const;
  std::optional<std::string> env(std::string_view name) const;

  SocketConnectResult connect(std::string_view host, std::uint16_t port) const;
  CapResult send(std::uint32_t handle, std::string_view data) const;
  CapResult recv(std::uint32_t handle, std::uint32_t max_bytes) const;
  CapResult close_socket(std::uint32_t handle) const;

 private:
  Scheduler* scheduler_ = nullptr;
  TaskId id_ = kNoTask;
};

// ---------------------------------------------------------------------------
// Scheduler
// ---------------------------------------------------------------------------

/// Lifecycle state of one task, as visible in run summaries and the debugger.
enum class TaskState : std::uint8_t {
  Ready,
  Running,
  Sleeping,
  Waiting,
  Done,
  Failed,
  Cancelled,
};

std::string_view task_state_name(TaskState state) noexcept;

struct SchedulerOptions {
  std::uint64_t seed = 1;
  std::uint32_t max_steps = limits::kMaxSteps;
  std::uint32_t max_tasks = limits::kMaxTasks;
  std::uint64_t start_ns = 1'000'000;
  std::uint64_t max_virtual_ns = limits::kMaxVirtualNs;
};

/// Outcome of one scheduler run. `ok` is true only when every task finished
/// successfully and no limit or deadlock was hit.
struct RunSummary {
  bool ok = true;
  bool step_limit = false;
  bool deadlock = false;
  bool host_error = false;
  std::uint64_t steps = 0;
  std::uint64_t final_ns = 0;
  std::uint32_t tasks_spawned = 0;
  std::uint32_t tasks_failed = 0;
  std::string error;
};

/// Deterministic cooperative scheduler.
///
/// Contract: single-threaded. Exactly one task resumes at a time; tasks suspend
/// only through Tempo awaitables. A task that loops forever without awaiting
/// will hang the run (documented limitation; built-in scenarios always await).
///
/// Memory is bounded by SchedulerOptions::max_tasks and max_steps. The scheduler
/// holds a non-owning reference to its RunHost.
class Scheduler {
 public:
  explicit Scheduler(RunHost& host, SchedulerOptions options = {});
  ~Scheduler();

  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;
  Scheduler(Scheduler&&) = delete;
  Scheduler& operator=(Scheduler&&) = delete;

  /// Spawn the first task of the run. Must be called once, before run().
  TaskId spawn_root(std::string name, Task (*entry)(Context));

  /// Spawn a task from inside a running task (also used by spawn_root).
  TaskId spawn(std::string name, std::function<Task(Context)> body, TaskId parent);

  /// Run until all tasks finish, the step limit is reached, or a deadlock.
  RunSummary run();

  // -- helpers used by Context / awaitables (public for the runtime only) ----
  void on_yield(TaskId id);
  void on_sleep(TaskId id, std::uint64_t deadline_ns);
  void on_join(TaskId waiter, TaskId target);

  bool task_done(TaskId id) const noexcept;
  bool task_ok(TaskId id) const noexcept;
  bool is_cancelled(TaskId id) const noexcept;
  void request_cancel(TaskId id);
  void cancel_all();

  std::uint64_t now_ns() const noexcept { return clock_.now_ns(); }
  std::uint64_t steps() const noexcept { return steps_; }
  const SchedulerOptions& options() const noexcept { return options_; }
  TaskState state_of(TaskId id) const noexcept;

  std::uint64_t random_u64(TaskId id);
  void watch(TaskId id, std::string_view name, std::string_view value);
  void note(TaskId id, std::string_view key, std::string_view value);
  void log(TaskId id, std::string_view message);

  CapResult read_file(TaskId id, std::string_view path);
  CapResult write_file(TaskId id, std::string_view path, std::string_view data);
  CapResult env_get(TaskId id, std::string_view name);
  SocketConnectResult socket_connect(TaskId id, std::string_view host, std::uint16_t port);
  CapResult socket_send(TaskId id, std::uint32_t handle, std::string_view data);
  CapResult socket_recv(TaskId id, std::uint32_t handle, std::uint32_t max_bytes);
  CapResult socket_close(TaskId id, std::uint32_t handle);

 private:
  struct Slot {
    TaskId id = kNoTask;
    TaskId parent = kNoTask;
    std::string name;
    Task::handle_type handle{};
    std::function<Task(Context)> body;
    TaskState state = TaskState::Ready;
    bool cancel_requested = false;
    std::uint64_t deadline_ns = 0;
    TaskId waiting_on = kNoTask;
    std::vector<TaskId> joiners;
    std::map<std::string, std::string> watches;
    std::string error;
    std::uint64_t spawned_at_ns = 0;
  };

  Slot* slot(TaskId id) noexcept;
  const Slot* slot(TaskId id) const noexcept;
  void emit(Event event);
  void release(Slot& slot);
  void wake_joiners(Slot& target);
  void remove_sleeper(TaskId id);
  void enqueue_ready(TaskId id);
  bool has_waiting() const noexcept;
  void fail_run(RunSummary& summary, bool deadlock, std::string message);
  void finish_task(Slot& slot);

  RunHost& host_;
  SchedulerOptions options_;
  VirtualClock clock_;
  Rng rng_;
  std::vector<std::unique_ptr<Slot>> slots_;
  std::vector<TaskId> ready_;
  std::map<std::uint64_t, std::vector<TaskId>> sleepers_;
  std::uint64_t steps_ = 0;
  std::uint32_t failed_ = 0;
  bool started_ = false;
  std::string scenario_name_;
};

}  // namespace tempo
