#include "tempo/runtime.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "tempo/limits.hpp"

namespace tempo {
namespace {

std::string exception_message(const std::exception_ptr& error) {
  if (!error) {
    return {};
  }
  try {
    std::rethrow_exception(error);
  } catch (const CancelledError&) {
    return "cancelled";
  } catch (const std::exception& e) {
    return e.what();
  } catch (...) {
    return "unknown exception";
  }
}

bool is_cancelled_exception(const std::exception_ptr& error) {
  if (!error) {
    return false;
  }
  try {
    std::rethrow_exception(error);
  } catch (const CancelledError&) {
    return true;
  } catch (...) {
    return false;
  }
}

std::string clipped(std::string_view text, std::size_t limit) {
  if (text.size() <= limit) {
    return std::string(text);
  }
  std::string out(text.substr(0, limit));
  out += "…[truncated]";
  return out;
}

}  // namespace

std::string_view task_state_name(TaskState state) noexcept {
  switch (state) {
    case TaskState::Ready:
      return "ready";
    case TaskState::Running:
      return "running";
    case TaskState::Sleeping:
      return "sleeping";
    case TaskState::Waiting:
      return "waiting";
    case TaskState::Done:
      return "done";
    case TaskState::Failed:
      return "failed";
    case TaskState::Cancelled:
      return "cancelled";
  }
  return "unknown";
}

Scheduler::Scheduler(RunHost& host, SchedulerOptions options)
    : host_(host), options_(options), clock_(options.start_ns), rng_(options.seed) {
  if (options_.max_steps == 0) {
    options_.max_steps = limits::kMaxSteps;
  }
  if (options_.max_tasks == 0) {
    options_.max_tasks = limits::kMaxTasks;
  }
}

Scheduler::~Scheduler() {
  for (auto& slot : slots_) {
    if (slot) {
      release(*slot);
    }
  }
}

Scheduler::Slot* Scheduler::slot(TaskId id) noexcept {
  if (id == kNoTask || id > slots_.size()) {
    return nullptr;
  }
  return slots_[id - 1].get();
}

const Scheduler::Slot* Scheduler::slot(TaskId id) const noexcept {
  if (id == kNoTask || id > slots_.size()) {
    return nullptr;
  }
  return slots_[id - 1].get();
}

TaskId Scheduler::spawn_root(std::string name, Task (*entry)(Context)) {
  if (started_) {
    throw LimitError("spawn_root called twice");
  }
  if (entry == nullptr) {
    throw LimitError("null scenario entry");
  }
  started_ = true;
  scenario_name_ = name;
  Event start;
  start.type = EventType::ScenarioStart;
  start.task = kNoTask;
  start.a = options_.seed;
  start.b = options_.start_ns;
  start.d = clock_.now_ns();
  start.s1 = scenario_name_;
  emit(std::move(start));
  return spawn(std::move(name), [entry](Context ctx) { return entry(ctx); }, kNoTask);
}

TaskId Scheduler::spawn(std::string name, std::function<Task(Context)> body, TaskId parent) {
  if (slots_.size() >= options_.max_tasks) {
    throw LimitError("task limit reached (" + std::to_string(options_.max_tasks) + ")");
  }
  if (!body) {
    throw LimitError("empty task body");
  }
  if (name.size() > limits::kMaxEventString) {
    throw LimitError("task name too long");
  }

  auto slot_ptr = std::make_unique<Slot>();
  const TaskId id = static_cast<TaskId>(slots_.size() + 1);
  slot_ptr->id = id;
  slot_ptr->parent = parent;
  slot_ptr->name = std::move(name);
  slot_ptr->state = TaskState::Ready;
  slot_ptr->spawned_at_ns = clock_.now_ns();

  Slot& s = *slot_ptr;
  // The body is stored first and never moved again: coroutine frames created by
  // a lambda body reference the closure object, which lives inside this
  // std::function for the lifetime of the task.
  s.body = std::move(body);
  Task task = s.body(Context{this, id});
  if (!task.valid()) {
    throw LimitError("task body produced an empty coroutine");
  }
  s.handle = task.release();
  s.handle.promise().scheduler = this;
  s.handle.promise().id = id;

  slots_.push_back(std::move(slot_ptr));
  ready_.push_back(id);

  Event event;
  event.type = EventType::Spawn;
  event.task = id;
  event.a = parent;
  event.b = id;
  event.d = clock_.now_ns();
  event.s1 = slots_.back()->name;
  emit(std::move(event));
  return id;
}

void Scheduler::enqueue_ready(TaskId id) {
  ready_.push_back(id);
}

void Scheduler::release(Slot& s) {
  if (s.handle) {
    s.handle.destroy();
    s.handle = {};
  }
  // Destroy the closure only after the coroutine frame that may point at it.
  s.body = nullptr;
}

void Scheduler::emit(Event event) {
  host_.emit(std::move(event));
}

void Scheduler::wake_joiners(Slot& target) {
  const std::vector<TaskId> joiners = std::move(target.joiners);
  target.joiners.clear();
  for (TaskId id : joiners) {
    Slot* waiter = slot(id);
    if (waiter == nullptr || waiter->waiting_on != target.id) {
      continue;
    }
    waiter->waiting_on = kNoTask;
    waiter->state = TaskState::Ready;
    enqueue_ready(id);
    Event event;
    event.type = EventType::Wake;
    event.task = id;
    event.a = target.id;
    event.b = clock_.now_ns();
    event.s1 = "join";
    emit(std::move(event));
  }
}

void Scheduler::remove_sleeper(TaskId id) {
  Slot* s = slot(id);
  if (s == nullptr) {
    return;
  }
  auto it = sleepers_.find(s->deadline_ns);
  if (it == sleepers_.end()) {
    return;
  }
  auto& bucket = it->second;
  bucket.erase(std::remove(bucket.begin(), bucket.end(), id), bucket.end());
  if (bucket.empty()) {
    sleepers_.erase(it);
  }
}

void Scheduler::finish_task(Slot& s) {
  const std::exception_ptr error = s.handle.promise().error;
  const bool cancelled_exception = is_cancelled_exception(error);
  std::string message = exception_message(error);
  bool success = false;

  if (error) {
    if (cancelled_exception && s.cancel_requested) {
      s.state = TaskState::Cancelled;
      message = "cancelled";
    } else {
      s.state = TaskState::Failed;
      ++failed_;
      success = false;
    }
  } else if (s.cancel_requested) {
    s.state = TaskState::Cancelled;
    message = "cancelled";
  } else {
    s.state = TaskState::Done;
    success = true;
  }
  s.error = message;

  if (s.state == TaskState::Failed) {
    Event event;
    event.type = EventType::Error;
    event.task = s.id;
    event.a = clock_.now_ns();
    event.s1 = message;
    emit(std::move(event));
  }

  Event exit;
  exit.type = EventType::Exit;
  exit.task = s.id;
  exit.a = clock_.now_ns();
  exit.b = success ? 1 : 0;
  exit.c = s.id;
  exit.d = clock_.now_ns() - s.spawned_at_ns;
  exit.s1 = message;
  emit(std::move(exit));

  wake_joiners(s);
  release(s);
}

void Scheduler::on_yield(TaskId id) {
  Slot* s = slot(id);
  if (s == nullptr || s->state != TaskState::Running) {
    return;
  }
  s->state = TaskState::Ready;
  enqueue_ready(id);
  Event event;
  event.type = EventType::Yield;
  event.task = id;
  event.a = clock_.now_ns();
  emit(std::move(event));
}

void Scheduler::on_sleep(TaskId id, std::uint64_t deadline_ns) {
  Slot* s = slot(id);
  if (s == nullptr || s->state != TaskState::Running) {
    return;
  }
  if (deadline_ns < clock_.now_ns()) {
    deadline_ns = clock_.now_ns();
  }
  if (deadline_ns > options_.max_virtual_ns) {
    throw LimitError("sleep deadline exceeds the virtual clock limit");
  }
  s->state = TaskState::Sleeping;
  s->deadline_ns = deadline_ns;
  sleepers_[deadline_ns].push_back(id);
  Event event;
  event.type = EventType::Sleep;
  event.task = id;
  event.a = deadline_ns;
  event.b = clock_.now_ns();
  event.c = deadline_ns - clock_.now_ns();
  emit(std::move(event));
}

void Scheduler::on_join(TaskId waiter, TaskId target) {
  Slot* w = slot(waiter);
  Slot* t = slot(target);
  if (w == nullptr || t == nullptr) {
    return;
  }
  if (t->joiners.size() >= limits::kMaxJoiners) {
    throw LimitError("too many tasks joining one task");
  }
  w->state = TaskState::Waiting;
  w->waiting_on = target;
  t->joiners.push_back(waiter);
  Event event;
  event.type = EventType::Join;
  event.task = waiter;
  event.a = target;
  event.b = clock_.now_ns();
  emit(std::move(event));
}

bool Scheduler::task_done(TaskId id) const noexcept {
  const Slot* s = slot(id);
  if (s == nullptr) {
    return true;
  }
  return s->state == TaskState::Done || s->state == TaskState::Failed ||
         s->state == TaskState::Cancelled;
}

bool Scheduler::task_ok(TaskId id) const noexcept {
  const Slot* s = slot(id);
  return s != nullptr && s->state == TaskState::Done;
}

bool Scheduler::is_cancelled(TaskId id) const noexcept {
  const Slot* s = slot(id);
  return s != nullptr && s->cancel_requested;
}

TaskState Scheduler::state_of(TaskId id) const noexcept {
  const Slot* s = slot(id);
  return s == nullptr ? TaskState::Done : s->state;
}

void Scheduler::request_cancel(TaskId id) {
  Slot* s = slot(id);
  if (s == nullptr || task_done(id) || s->cancel_requested) {
    return;
  }
  s->cancel_requested = true;
  Event event;
  event.type = EventType::Cancel;
  event.task = id;
  event.a = clock_.now_ns();
  event.s1 = "requested";
  emit(std::move(event));

  if (s->state == TaskState::Sleeping) {
    remove_sleeper(id);
    s->state = TaskState::Ready;
    enqueue_ready(id);
    Event wake;
    wake.type = EventType::Wake;
    wake.task = id;
    wake.a = clock_.now_ns();
    wake.s1 = "cancel";
    emit(std::move(wake));
  } else if (s->state == TaskState::Waiting) {
    Slot* target = slot(s->waiting_on);
    if (target != nullptr) {
      auto& joiners = target->joiners;
      joiners.erase(std::remove(joiners.begin(), joiners.end(), id), joiners.end());
    }
    s->waiting_on = kNoTask;
    s->state = TaskState::Ready;
    enqueue_ready(id);
    Event wake;
    wake.type = EventType::Wake;
    wake.task = id;
    wake.a = clock_.now_ns();
    wake.s1 = "cancel";
    emit(std::move(wake));
  }
}

void Scheduler::cancel_all() {
  std::vector<TaskId> ids;
  ids.reserve(slots_.size());
  for (const auto& s : slots_) {
    if (s && !task_done(s->id)) {
      ids.push_back(s->id);
    }
  }
  for (TaskId id : ids) {
    request_cancel(id);
  }
}

bool Scheduler::has_waiting() const noexcept {
  for (const auto& s : slots_) {
    if (s && s->state == TaskState::Waiting) {
      return true;
    }
  }
  return false;
}

void Scheduler::fail_run(RunSummary& summary, bool deadlock, std::string message) {
  summary.ok = false;
  if (deadlock) {
    summary.deadlock = true;
  } else {
    summary.step_limit = true;
  }
  summary.error = message;
  Event event;
  event.type = EventType::Error;
  event.task = kNoTask;
  event.a = clock_.now_ns();
  event.s1 = message;
  emit(std::move(event));
}

RunSummary Scheduler::run() {
  RunSummary summary;
  if (!started_ || slots_.empty()) {
    summary.ok = false;
    summary.error = "no root task";
    return summary;
  }

  for (;;) {
    if (ready_.empty()) {
      if (!sleepers_.empty()) {
        auto it = sleepers_.begin();
        const std::uint64_t deadline = it->first;
        if (deadline > options_.max_virtual_ns) {
          fail_run(summary, false, "virtual clock limit reached");
          break;
        }
        const std::uint64_t before = clock_.now_ns();
        if (deadline > before) {
          clock_.advance_to(deadline);
          Event jump;
          jump.type = EventType::ClockJump;
          jump.task = kNoTask;
          jump.a = deadline;
          jump.b = before;
          jump.d = clock_.now_ns();
          emit(std::move(jump));
        }
        std::vector<TaskId> waking = std::move(it->second);
        sleepers_.erase(it);
        for (TaskId id : waking) {
          Slot* s = slot(id);
          if (s == nullptr || s->state != TaskState::Sleeping || s->deadline_ns != deadline) {
            continue;
          }
          s->state = TaskState::Ready;
          enqueue_ready(id);
          Event wake;
          wake.type = EventType::Wake;
          wake.task = id;
          wake.a = clock_.now_ns();
          wake.s1 = "sleep";
          emit(std::move(wake));
        }
        continue;
      }
      if (has_waiting()) {
        fail_run(summary, true, "deadlock: all remaining tasks are waiting");
        break;
      }
      break;
    }

    if (steps_ >= options_.max_steps) {
      fail_run(summary, false, "step limit reached");
      break;
    }

    const std::uint64_t draw = rng_.next_u64();
    const std::uint64_t draw_index = draw % ready_.size();
    const std::vector<TaskId> ready_snapshot = ready_;
    TaskId chosen = kNoTask;
    bool host_error = false;
    try {
      chosen = host_.choose_ready(ready_snapshot, draw_index, steps_);
    } catch (const std::exception& e) {
      host_error = true;
      summary.error = std::string("host choose failed: ") + e.what();
    } catch (...) {
      host_error = true;
      summary.error = "host choose failed";
    }
    if (host_error) {
      summary.ok = false;
      summary.host_error = true;
      Event error;
      error.type = EventType::Error;
      error.a = clock_.now_ns();
      error.s1 = summary.error;
      emit(std::move(error));
      break;
    }
    auto chosen_it = std::find(ready_.begin(), ready_.end(), chosen);
    if (chosen_it == ready_.end()) {
      Event error;
      error.type = EventType::Error;
      error.a = clock_.now_ns();
      error.s1 = "host returned a task that is not ready";
      emit(std::move(error));
      summary.ok = false;
      summary.host_error = true;
      summary.error = error.s1;
      break;
    }
    ready_.erase(chosen_it);

    Slot* s = slot(chosen);
    if (s == nullptr || s->state != TaskState::Ready) {
      summary.ok = false;
      summary.host_error = true;
      summary.error = "scheduler invariant violated";
      break;
    }

    Event step;
    step.type = EventType::Step;
    step.task = chosen;
    step.a = steps_;
    step.b = chosen;
    step.c = draw;
    step.d = clock_.now_ns();
    step.s1 = s->name;
    emit(std::move(step));

    s->state = TaskState::Running;
    s->handle.resume();
    ++steps_;

    if (s->handle.done()) {
      finish_task(*s);
      continue;
    }
    if (s->state == TaskState::Running) {
      // Suspended on something that is not a Tempo awaitable. This can never
      // resume, so declare it failed rather than hanging silently.
      s->state = TaskState::Failed;
      s->error = "task suspended outside a Tempo awaitable";
      ++failed_;
      Event error;
      error.type = EventType::Error;
      error.task = chosen;
      error.a = clock_.now_ns();
      error.s1 = s->error;
      emit(std::move(error));
      Event exit;
      exit.type = EventType::Exit;
      exit.task = chosen;
      exit.a = clock_.now_ns();
      exit.b = 0;
      exit.s1 = s->error;
      emit(std::move(exit));
      wake_joiners(*s);
      release(*s);
    }
  }

  // Destroy any still-suspended frames. Metadata stays available for summaries.
  for (auto& s : slots_) {
    if (s && (s->handle || s->body)) {
      release(*s);
    }
  }

  summary.steps = steps_;
  summary.final_ns = clock_.now_ns();
  summary.tasks_spawned = static_cast<std::uint32_t>(slots_.size());
  summary.tasks_failed = failed_;
  if (summary.ok && (failed_ != 0 || !summary.error.empty())) {
    summary.ok = false;
  }

  Event end;
  end.type = EventType::ScenarioEnd;
  end.task = kNoTask;
  end.a = steps_;
  end.b = clock_.now_ns();
  end.c = failed_;
  end.s1 = summary.ok ? "ok" : "failed";
  emit(std::move(end));
  return summary;
}

std::uint64_t Scheduler::random_u64(TaskId id) {
  const std::uint64_t value = rng_.next_u64();
  Event event;
  event.type = EventType::Rng;
  event.task = id;
  event.a = value;
  event.d = clock_.now_ns();
  emit(std::move(event));
  return value;
}

void Scheduler::watch(TaskId id, std::string_view name, std::string_view value) {
  Slot* s = slot(id);
  if (s == nullptr) {
    return;
  }
  if (name.empty() || name.size() > limits::kMaxWatchName) {
    throw LimitError("watch name must be 1..64 bytes");
  }
  if (value.size() > limits::kMaxWatchValue) {
    throw LimitError("watch value exceeds 512 bytes");
  }
  auto it = s->watches.find(std::string(name));
  if (it == s->watches.end()) {
    if (s->watches.size() >= limits::kMaxWatchVarsPerTask) {
      throw LimitError("too many watch variables in one task");
    }
    s->watches.emplace(std::string(name), std::string(value));
  } else {
    if (it->second == value) {
      return;
    }
    it->second = std::string(value);
  }
  Event event;
  event.type = EventType::Watch;
  event.task = id;
  event.a = clock_.now_ns();
  event.s1 = std::string(name);
  event.s2 = std::string(value);
  emit(std::move(event));
}

void Scheduler::note(TaskId id, std::string_view key, std::string_view value) {
  if (key.empty() || key.size() > limits::kMaxWatchName) {
    throw LimitError("note key must be 1..64 bytes");
  }
  if (value.size() > limits::kMaxWatchValue) {
    throw LimitError("note value exceeds 512 bytes");
  }
  Event event;
  event.type = EventType::Note;
  event.task = id;
  event.a = clock_.now_ns();
  event.s1 = std::string(key);
  event.s2 = std::string(value);
  emit(std::move(event));
}

void Scheduler::log(TaskId id, std::string_view message) {
  std::size_t original = 0;
  Event event;
  event.type = EventType::Log;
  event.task = id;
  event.a = clock_.now_ns();
  event.d = original;
  const std::size_t limit = limits::kMaxLogBytes;
  if (message.size() > limit) {
    original = message.size();
    event.s1 = clipped(message, limit);
    event.d = original;
  } else {
    event.s1 = std::string(message);
    event.d = message.size();
  }
  emit(std::move(event));
}

CapResult Scheduler::read_file(TaskId id, std::string_view path) {
  CapResult result;
  Event event;
  event.type = EventType::FileRead;
  event.task = id;
  event.d = clock_.now_ns();
  if (path.empty() || path.size() > limits::kMaxPathBytes) {
    result.ok = false;
    result.error = "path empty or longer than 1024 bytes";
  } else {
    result = host_.file_read(std::string(path));
    if (result.data.size() > limits::kMaxFileBytes) {
      result.ok = false;
      result.error = "file exceeds 65536 byte capability limit";
      result.data.clear();
    }
  }
  event.a = result.ok ? 1 : 0;
  event.b = result.data.size();
  event.s1 = clipped(path, limits::kMaxPathBytes);
  event.s2 = result.error;
  event.bytes = result.data;
  emit(std::move(event));
  return result;
}

CapResult Scheduler::write_file(TaskId id, std::string_view path, std::string_view data) {
  CapResult result;
  Event event;
  event.type = EventType::FileWrite;
  event.task = id;
  event.d = clock_.now_ns();
  if (path.empty() || path.size() > limits::kMaxPathBytes) {
    result.error = "path empty or longer than 1024 bytes";
  } else if (data.size() > limits::kMaxFileBytes) {
    result.error = "write exceeds 65536 byte capability limit";
  } else {
    result = host_.file_write(std::string(path), std::string(data));
  }
  event.a = result.ok ? 1 : 0;
  event.b = data.size();
  event.s1 = clipped(path, limits::kMaxPathBytes);
  event.s2 = result.error;
  event.bytes = std::string(data);
  emit(std::move(event));
  return result;
}

CapResult Scheduler::env_get(TaskId id, std::string_view name) {
  CapResult result;
  Event event;
  event.type = EventType::EnvGet;
  event.task = id;
  event.d = clock_.now_ns();
  if (name.empty() || name.size() > limits::kMaxPathBytes) {
    result.error = "environment name empty or too long";
  } else {
    result = host_.env_get(std::string(name));
    if (result.data.size() > limits::kMaxEnvBytes) {
      result.ok = false;
      result.error = "environment value exceeds 4096 byte limit";
      result.data.clear();
    }
  }
  event.a = result.ok ? 1 : 0;
  event.b = result.data.size();
  event.s1 = std::string(name);
  if (result.ok) {
    event.s2 = result.data;
  } else {
    event.s2 = result.error;
  }
  emit(std::move(event));
  return result;
}

SocketConnectResult Scheduler::socket_connect(TaskId id, std::string_view host,
                                              std::uint16_t port) {
  SocketConnectResult result;
  Event event;
  event.type = EventType::SocketConnect;
  event.task = id;
  event.d = clock_.now_ns();
  if (host.empty() || host.size() > 255) {
    result.error = "socket host empty or too long";
  } else {
    result = host_.socket_connect(std::string(host), port);
  }
  event.a = port;
  event.b = result.handle;
  event.c = result.ok ? 1 : 0;
  event.s1 = std::string(host);
  event.s2 = result.error;
  emit(std::move(event));
  return result;
}

CapResult Scheduler::socket_send(TaskId id, std::uint32_t handle, std::string_view data) {
  CapResult result;
  Event event;
  event.type = EventType::SocketSend;
  event.task = id;
  event.d = clock_.now_ns();
  if (data.size() > limits::kMaxSocketPayload) {
    result.error = "socket send exceeds 16384 byte limit";
  } else {
    result = host_.socket_send(handle, std::string(data));
  }
  event.a = handle;
  event.b = data.size();
  event.c = result.ok ? 1 : 0;
  event.s2 = result.error;
  event.bytes = std::string(data);
  emit(std::move(event));
  return result;
}

CapResult Scheduler::socket_recv(TaskId id, std::uint32_t handle, std::uint32_t max_bytes) {
  CapResult result;
  Event event;
  event.type = EventType::SocketRecv;
  event.task = id;
  event.d = clock_.now_ns();
  if (max_bytes == 0 || max_bytes > limits::kMaxSocketPayload) {
    result.error = "socket recv limit must be 1..16384";
  } else {
    result = host_.socket_recv(handle, max_bytes);
    if (result.data.size() > limits::kMaxSocketPayload) {
      result.ok = false;
      result.error = "socket recv returned more than the requested limit";
      result.data.clear();
    }
  }
  event.a = handle;
  event.b = max_bytes;
  event.c = result.ok ? 1 : 0;
  event.s2 = result.error;
  event.bytes = result.data;
  emit(std::move(event));
  return result;
}

CapResult Scheduler::socket_close(TaskId id, std::uint32_t handle) {
  CapResult result;
  Event event;
  event.type = EventType::SocketClose;
  event.task = id;
  event.d = clock_.now_ns();
  result = host_.socket_close(handle);
  event.a = handle;
  event.b = result.ok ? 1 : 0;
  event.s2 = result.error;
  emit(std::move(event));
  return result;
}

}  // namespace tempo
