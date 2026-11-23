#include "tempo/record_run.hpp"

#include <ctime>
#include <utility>
#include <vector>

#include "tempo/limits.hpp"
#include "tempo/version.hpp"

namespace tempo {
namespace {

class RecordingHost final : public RunHost {
 public:
  RecordingHost(RunHost& inner, RecordRunOptions options)
      : inner_(inner), options_(options) {}

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
    }
    return chosen;
  }

  CapResult file_read(const std::string& path) override { return inner_.file_read(path); }
  CapResult file_write(const std::string& path, const std::string& data) override {
    return inner_.file_write(path, data);
  }
  CapResult env_get(const std::string& name) override { return inner_.env_get(name); }
  SocketConnectResult socket_connect(const std::string& host, std::uint16_t port) override {
    return inner_.socket_connect(host, port);
  }
  CapResult socket_send(std::uint32_t handle, const std::string& data) override {
    return inner_.socket_send(handle, data);
  }
  CapResult socket_recv(std::uint32_t handle, std::uint32_t max_bytes) override {
    return inner_.socket_recv(handle, max_bytes);
  }
  CapResult socket_close(std::uint32_t handle) override {
    return inner_.socket_close(handle);
  }

  void emit(Event event) override {
    if (events_.size() >= limits::kMaxEvents) {
      throw LimitError("record event limit reached during capture");
    }
    if (event.s1.size() > limits::kMaxEventString ||
        event.s2.size() > limits::kMaxEventString) {
      throw LimitError("event string exceeds capture limit");
    }
    if (event.bytes.size() > limits::kMaxEventPayload) {
      throw LimitError("event payload exceeds capture limit");
    }
    events_.push_back(std::move(event));
  }

  std::vector<Event> release_events() { return std::move(events_); }
  bool mutation_applied() const noexcept { return mutation_applied_; }

 private:
  RunHost& inner_;
  RecordRunOptions options_;
  bool mutation_applied_ = false;
  std::vector<Event> events_;
};

}  // namespace

RecordRunResult record_run(std::string scenario_name, std::function<Task(Context)> entry,
                           RunHost& capabilities, RecordRunOptions options) {
  RecordRunResult result;
  const std::string original_name = scenario_name;
  RecordingHost host(capabilities, options);
  SchedulerOptions scheduler_options;
  scheduler_options.seed = options.seed;
  scheduler_options.max_steps = options.max_steps;
  scheduler_options.max_tasks = options.max_tasks;
  scheduler_options.start_ns = options.start_ns;
  Scheduler scheduler(host, scheduler_options);

  try {
    scheduler.spawn_root(std::move(scenario_name), std::move(entry));
    result.summary = scheduler.run();
  } catch (const std::exception& e) {
    result.error = e.what();
  } catch (...) {
    result.error = "unknown failure while recording";
  }

  std::vector<Event> events;
  try {
    events = host.release_events();
  } catch (...) {
    result.error = "failed to release captured events";
  }
  result.record.meta.scenario = original_name;
  result.record.meta.tool_version = std::string(kVersion);
  result.record.meta.seed = options.seed;
  result.record.meta.start_ns = options.start_ns;
  result.record.meta.created_unix = static_cast<std::uint64_t>(std::time(nullptr));
  if (!options.build_id.empty()) {
    result.record.meta.attributes["build_id"] = options.build_id;
  }
  if (options.mutate_step != kNoMutation) {
    result.record.meta.attributes["mutate_step"] = std::to_string(options.mutate_step);
    result.record.meta.attributes["mutate_choice"] = std::to_string(options.mutate_choice);
    result.record.meta.attributes["mutation_applied"] =
        host.mutation_applied() ? "1" : "0";
  }
  result.mutation_applied = host.mutation_applied();
  result.record.events = std::move(events);
  result.record.recompute_hash();
  result.ok = result.error.empty() && result.summary.ok;
  return result;
}

}  // namespace tempo
