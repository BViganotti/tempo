#include "tempo/runtime.hpp"

#include <cinttypes>
#include <cstdio>
#include <limits>

#include "tempo/limits.hpp"

namespace tempo {

// ---------------------------------------------------------------------------
// Awaitables
// ---------------------------------------------------------------------------

YieldAwaiter::YieldAwaiter(Scheduler* scheduler, TaskId id) noexcept
    : scheduler_(scheduler), id_(id) {}

void YieldAwaiter::await_suspend(std::coroutine_handle<>) const {
  if (scheduler_ == nullptr) {
    throw LimitError("yield on an invalid context");
  }
  scheduler_->on_yield(id_);
}

void YieldAwaiter::await_resume() const {
  if (scheduler_ != nullptr && scheduler_->is_cancelled(id_)) {
    throw CancelledError();
  }
}

SleepAwaiter::SleepAwaiter(Scheduler* scheduler, TaskId id, std::uint64_t deadline_ns) noexcept
    : scheduler_(scheduler), id_(id), deadline_ns_(deadline_ns) {}

void SleepAwaiter::await_suspend(std::coroutine_handle<>) const {
  if (scheduler_ == nullptr) {
    throw LimitError("sleep on an invalid context");
  }
  scheduler_->on_sleep(id_, deadline_ns_);
}

void SleepAwaiter::await_resume() const {
  if (scheduler_ != nullptr && scheduler_->is_cancelled(id_)) {
    throw CancelledError();
  }
}

JoinAwaiter::JoinAwaiter(Scheduler* scheduler, TaskId waiter, TaskId target) noexcept
    : scheduler_(scheduler), waiter_(waiter), target_(target) {}

bool JoinAwaiter::await_ready() const noexcept {
  return scheduler_ == nullptr || scheduler_->task_done(target_);
}

void JoinAwaiter::await_suspend(std::coroutine_handle<>) const {
  if (scheduler_ == nullptr) {
    throw LimitError("join on an invalid context");
  }
  scheduler_->on_join(waiter_, target_);
}

bool JoinAwaiter::await_resume() const {
  if (scheduler_ != nullptr && scheduler_->is_cancelled(waiter_)) {
    throw CancelledError();
  }
  return scheduler_ != nullptr && scheduler_->task_ok(target_);
}

// ---------------------------------------------------------------------------
// Context
// ---------------------------------------------------------------------------

std::uint64_t Context::now_ns() const noexcept {
  return scheduler_ == nullptr ? 0 : scheduler_->now_ns();
}

std::uint64_t Context::now_ms() const noexcept {
  return now_ns() / 1'000'000;
}

SleepAwaiter Context::sleep_ns(std::uint64_t duration_ns) const {
  if (scheduler_ == nullptr) {
    throw LimitError("sleep on an invalid context");
  }
  const std::uint64_t now = scheduler_->now_ns();
  if (duration_ns > std::numeric_limits<std::uint64_t>::max() - now) {
    throw LimitError("sleep duration overflows the virtual clock");
  }
  const std::uint64_t deadline = now + duration_ns;
  if (deadline > limits::kMaxVirtualNs) {
    throw LimitError("sleep deadline exceeds the virtual clock limit");
  }
  return SleepAwaiter(scheduler_, id_, deadline);
}

SleepAwaiter Context::sleep_ms(std::uint64_t duration_ms) const {
  constexpr std::uint64_t kScale = 1'000'000;
  if (duration_ms > std::numeric_limits<std::uint64_t>::max() / kScale) {
    throw LimitError("sleep duration in milliseconds overflows");
  }
  return sleep_ns(duration_ms * kScale);
}

SleepAwaiter Context::sleep_until_ns(std::uint64_t deadline_ns) const {
  if (scheduler_ == nullptr) {
    throw LimitError("sleep on an invalid context");
  }
  std::uint64_t deadline = deadline_ns;
  const std::uint64_t now = scheduler_->now_ns();
  if (deadline < now) {
    deadline = now;
  }
  if (deadline > limits::kMaxVirtualNs) {
    throw LimitError("sleep deadline exceeds the virtual clock limit");
  }
  return SleepAwaiter(scheduler_, id_, deadline);
}

bool Context::cancelled() const noexcept {
  return scheduler_ != nullptr && scheduler_->is_cancelled(id_);
}

void Context::check_cancelled() const {
  if (cancelled()) {
    throw CancelledError();
  }
}

void Context::cancel(TaskId target) const {
  if (scheduler_ != nullptr) {
    scheduler_->request_cancel(target);
  }
}

TaskId Context::spawn(std::string name, std::function<Task(Context)> body) const {
  if (scheduler_ == nullptr) {
    throw LimitError("spawn on an invalid context");
  }
  return scheduler_->spawn(std::move(name), std::move(body), id_);
}

void Context::watch(std::string_view name, std::string_view value) const {
  if (scheduler_ == nullptr) {
    throw LimitError("watch on an invalid context");
  }
  scheduler_->watch(id_, name, value);
}

void Context::watch(std::string_view name, std::int64_t value) const {
  watch(name, std::to_string(value));
}

void Context::watch(std::string_view name, double value) const {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.6g", value);
  watch(name, std::string_view(buffer));
}

void Context::watch(std::string_view name, bool value) const {
  watch(name, std::string_view(value ? "true" : "false"));
}

void Context::note(std::string_view key, std::string_view value) const {
  if (scheduler_ == nullptr) {
    throw LimitError("note on an invalid context");
  }
  scheduler_->note(id_, key, value);
}

void Context::log(std::string_view message) const {
  if (scheduler_ == nullptr) {
    throw LimitError("log on an invalid context");
  }
  scheduler_->log(id_, message);
}

std::uint64_t Context::random_u64() const {
  if (scheduler_ == nullptr) {
    throw LimitError("random on an invalid context");
  }
  return scheduler_->random_u64(id_);
}

CapResult Context::read_file(std::string_view path) const {
  if (scheduler_ == nullptr) {
    throw LimitError("read_file on an invalid context");
  }
  return scheduler_->read_file(id_, path);
}

CapResult Context::write_file(std::string_view path, std::string_view data) const {
  if (scheduler_ == nullptr) {
    throw LimitError("write_file on an invalid context");
  }
  return scheduler_->write_file(id_, path, data);
}

CapResult Context::env_get(std::string_view name) const {
  if (scheduler_ == nullptr) {
    throw LimitError("env_get on an invalid context");
  }
  return scheduler_->env_get(id_, name);
}

std::optional<std::string> Context::env(std::string_view name) const {
  CapResult result = env_get(name);
  if (!result.ok) {
    return std::nullopt;
  }
  return result.data;
}

SocketConnectResult Context::connect(std::string_view host, std::uint16_t port) const {
  if (scheduler_ == nullptr) {
    throw LimitError("connect on an invalid context");
  }
  return scheduler_->socket_connect(id_, host, port);
}

CapResult Context::send(std::uint32_t handle, std::string_view data) const {
  if (scheduler_ == nullptr) {
    throw LimitError("send on an invalid context");
  }
  return scheduler_->socket_send(id_, handle, data);
}

CapResult Context::recv(std::uint32_t handle, std::uint32_t max_bytes) const {
  if (scheduler_ == nullptr) {
    throw LimitError("recv on an invalid context");
  }
  return scheduler_->socket_recv(id_, handle, max_bytes);
}

CapResult Context::close_socket(std::uint32_t handle) const {
  if (scheduler_ == nullptr) {
    throw LimitError("close_socket on an invalid context");
  }
  return scheduler_->socket_close(id_, handle);
}

}  // namespace tempo
