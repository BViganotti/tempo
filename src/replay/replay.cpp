#include "tempo/replay.hpp"

#include <algorithm>
#include <chrono>
#include <utility>

#include "tempo/limits.hpp"

namespace tempo {
namespace {

class ReplayHost final : public RunHost {
 public:
  ReplayHost(const Record& record, ReplayOptions options)
      : record_(record), options_(options) {}

  TaskId choose_ready(const std::vector<TaskId>& ready, std::uint64_t draw_index,
                      std::uint64_t step) override {
    if (ready.empty()) {
      return kNoTask;
    }
    const std::size_t index = draw_index % ready.size();
    TaskId chosen = ready[index];
    if (step == options_.mutate_step && ready.size() > 1) {
      std::size_t alt = (index + 1 + options_.mutate_choice) % ready.size();
      if (alt == index) {
        alt = (alt + 1) % ready.size();
      }
      chosen = ready[alt];
      mutation_applied_ = true;
      return chosen;
    }
    const Event* expected = peek();
    if (expected != nullptr && expected->type == EventType::Step && expected->a == step) {
      const TaskId recorded = static_cast<TaskId>(expected->b);
      if (std::find(ready.begin(), ready.end(), recorded) != ready.end()) {
        return recorded;
      }
    }
    return chosen;
  }

  CapResult file_read(const std::string& path) override {
    const Event* expected = peek();
    if (expected == nullptr || expected->type != EventType::FileRead ||
        expected->s1 != path) {
      return replay_error("file read");
    }
    CapResult result;
    result.ok = expected->a != 0;
    result.error = expected->s2;
    result.data = expected->bytes;
    result.num = result.data.size();
    return result;
  }

  CapResult file_write(const std::string& path, const std::string& /*data*/) override {
    const Event* expected = peek();
    if (expected == nullptr || expected->type != EventType::FileWrite ||
        expected->s1 != path) {
      return replay_error("file write");
    }
    CapResult result;
    result.ok = expected->a != 0;
    result.error = expected->s2;
    result.num = expected->b;
    return result;
  }

  CapResult env_get(const std::string& name) override {
    const Event* expected = peek();
    if (expected == nullptr || expected->type != EventType::EnvGet ||
        expected->s1 != name) {
      return replay_error("environment read");
    }
    CapResult result;
    result.ok = expected->a != 0;
    if (result.ok) {
      result.data = expected->s2;
      result.num = result.data.size();
    } else {
      result.error = expected->s2;
    }
    return result;
  }

  SocketConnectResult socket_connect(const std::string& host, std::uint16_t port) override {
    const Event* expected = peek();
    SocketConnectResult result;
    if (expected == nullptr || expected->type != EventType::SocketConnect ||
        expected->s1 != host || expected->a != port) {
      result.error = "replay: unexpected socket connect";
      return result;
    }
    result.ok = expected->c != 0;
    result.handle = static_cast<std::uint32_t>(expected->b);
    result.error = expected->s2;
    return result;
  }

  CapResult socket_send(std::uint32_t handle, const std::string& data) override {
    const Event* expected = peek();
    if (expected == nullptr || expected->type != EventType::SocketSend ||
        expected->a != handle || expected->bytes != data) {
      return replay_error("socket send");
    }
    CapResult result;
    result.ok = expected->c != 0;
    result.error = expected->s2;
    result.num = expected->b;
    return result;
  }

  CapResult socket_recv(std::uint32_t handle, std::uint32_t max_bytes) override {
    const Event* expected = peek();
    if (expected == nullptr || expected->type != EventType::SocketRecv ||
        expected->a != handle || expected->b != max_bytes) {
      return replay_error("socket receive");
    }
    CapResult result;
    result.ok = expected->c != 0;
    result.error = expected->s2;
    result.data = expected->bytes;
    result.num = result.data.size();
    return result;
  }

  CapResult socket_close(std::uint32_t handle) override {
    const Event* expected = peek();
    if (expected == nullptr || expected->type != EventType::SocketClose ||
        expected->a != handle) {
      return replay_error("socket close");
    }
    CapResult result;
    result.ok = expected->b != 0;
    result.error = expected->s2;
    return result;
  }

  void emit(Event event) override {
    replayed_hash_ = event_hash(replayed_hash_, event);
    ++replayed_events_;
    if (diverged_) {
      return;
    }
    if (cursor_ >= record_.events.size()) {
      record_divergence(cursor_, nullptr, &event, "record ended before replay");
      return;
    }
    const Event& expected = record_.events[cursor_];
    if (!(expected == event)) {
      record_divergence(cursor_, &expected, &event, "event mismatch");
      return;
    }
    ++cursor_;
  }

  void finalize() {
    if (diverged_) {
      return;
    }
    if (cursor_ < record_.events.size()) {
      const Event& expected = record_.events[cursor_];
      record_divergence(cursor_, &expected, nullptr, "record has extra events");
      return;
    }
    identical_ = replayed_events_ == record_.events.size() &&
                 replayed_hash_ == record_.hash;
  }

  bool identical() const noexcept { return identical_; }
  bool mutation_applied() const noexcept { return mutation_applied_; }
  std::uint64_t replayed_hash() const noexcept { return replayed_hash_; }
  std::size_t replayed_events() const noexcept { return replayed_events_; }
  std::size_t compared_events() const noexcept { return cursor_; }

  Divergence take_divergence() const {
    Divergence result;
    result.found = diverged_;
    result.index = divergence_index_;
    result.reason = divergence_reason_;
    if (has_expected_) {
      result.has_expected = true;
      result.expected = expected_event_;
      result.expected_summary = event_summary(expected_event_);
    }
    if (has_actual_) {
      result.has_actual = true;
      result.actual = actual_event_;
      result.actual_summary = event_summary(actual_event_);
    }
    return result;
  }

 private:
  const Event* peek() const {
    if (diverged_ || cursor_ >= record_.events.size()) {
      return nullptr;
    }
    return &record_.events[cursor_];
  }

  static CapResult replay_error(const char* what) {
    CapResult result;
    result.error = std::string("replay: unexpected ") + what;
    return result;
  }

  void record_divergence(std::size_t index, const Event* expected, const Event* actual,
                         const char* reason) {
    if (diverged_) {
      return;
    }
    diverged_ = true;
    identical_ = false;
    divergence_index_ = index;
    divergence_reason_ = reason;
    if (expected != nullptr) {
      has_expected_ = true;
      expected_event_ = *expected;
    }
    if (actual != nullptr) {
      has_actual_ = true;
      actual_event_ = *actual;
    }
  }

  const Record& record_;
  ReplayOptions options_;
  std::size_t cursor_ = 0;
  std::uint64_t replayed_hash_ = 14695981039346656037ull;
  std::size_t replayed_events_ = 0;
  bool diverged_ = false;
  bool identical_ = false;
  bool mutation_applied_ = false;
  std::size_t divergence_index_ = 0;
  std::string divergence_reason_;
  bool has_expected_ = false;
  bool has_actual_ = false;
  Event expected_event_;
  Event actual_event_;
};

}  // namespace

ReplayReport replay_record(const Record& record, std::function<Task(Context)> entry,
                           ReplayOptions options) {
  ReplayReport report;
  report.recorded_hash = record.hash;
  report.recorded_events = record.events.size();
  if (!record.complete) {
    report.error = "cannot replay an incomplete record";
    return report;
  }
  if (!entry) {
    report.error = "empty replay entry";
    return report;
  }
  if (record.events.empty()) {
    report.error = "record has no events";
    return report;
  }

  ReplayHost host(record, options);
  SchedulerOptions scheduler_options;
  scheduler_options.seed = record.meta.seed;
  scheduler_options.start_ns = record.meta.start_ns;
  scheduler_options.max_steps = options.max_steps;
  scheduler_options.max_tasks = options.max_tasks;
  Scheduler scheduler(host, scheduler_options);

  try {
    scheduler.spawn_root(record.meta.scenario, std::move(entry));
    report.summary = scheduler.run();
    host.finalize();
    report.ok = true;
  } catch (const std::exception& e) {
    report.error = e.what();
  } catch (...) {
    report.error = "unknown failure while replaying";
  }

  report.first_divergence = host.take_divergence();
  report.mutation_applied = host.mutation_applied();
  report.replayed_hash = host.replayed_hash();
  report.replayed_events = host.replayed_events();
  report.compared_events = host.compared_events();
  report.identical = report.ok && host.identical();
  report.complete = report.summary.steps > 0 &&
                    !report.summary.step_limit && !report.summary.deadlock;
  if (report.ok && !report.identical && !report.first_divergence.found) {
    report.first_divergence.found = true;
    report.first_divergence.reason = "hash mismatch";
    report.first_divergence.index = 0;
  }
  return report;
}

ReplayBatchReport replay_many(const Record& record, std::function<Task(Context)> entry,
                              std::uint32_t runs, ReplayOptions options) {
  ReplayBatchReport report;
  if (runs == 0) {
    report.error = "runs must be at least 1";
    return report;
  }
  const std::uint32_t capped = std::min(runs, limits::kMaxReplayRuns);
  const auto start = std::chrono::steady_clock::now();
  report.runs = capped;
  report.expected_hash = record.hash;
  report.hashes.reserve(capped);
  bool ok = true;
  bool identical = true;
  for (std::uint32_t i = 0; i < capped; ++i) {
    const ReplayReport one = replay_record(record, entry, options);
    report.hashes.push_back(one.replayed_hash);
    if (!one.ok) {
      ok = false;
      if (report.error.empty()) {
        report.error = one.error;
      }
    }
    if (!one.identical) {
      identical = false;
    }
  }
  const auto end = std::chrono::steady_clock::now();
  report.elapsed_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
  report.ok = ok;
  report.all_identical = identical;
  report.all_hashes_equal = true;
  for (std::uint64_t hash : report.hashes) {
    if (hash != record.hash) {
      report.all_hashes_equal = false;
      break;
    }
  }
  return report;
}

}  // namespace tempo
