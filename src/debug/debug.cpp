#include "tempo/debug.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace tempo {
namespace {

constexpr std::size_t kCheckpointInterval = 64;
constexpr std::size_t kCheckpointBudgetBytes = 4u * 1024u * 1024u;

std::string lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return out;
}

}  // namespace

DebugSession::DebugSession(std::shared_ptr<const Record> record)
    : record_(std::move(record)) {
  if (record_) {
    build_checkpoints();
  }
}

void DebugSession::build_checkpoints() {
  checkpoints_.clear();
  const std::size_t count = record_->events.size();
  if (count == 0) {
    return;
  }

  // Conservative estimate of one full state copy. If checkpoints would exceed
  // the budget, skip them and reconstruct from the beginning instead.
  std::map<std::pair<TaskId, std::string>, std::size_t> max_value_bytes;
  for (const auto& event : record_->events) {
    if (event.type != EventType::Watch) {
      continue;
    }
    auto& slot = max_value_bytes[{event.task, event.s1}];
    slot = std::max(slot, event.s2.size());
  }
  std::size_t state_estimate = 0;
  for (const auto& [key, value_bytes] : max_value_bytes) {
    state_estimate += key.second.size() + value_bytes + 96;
  }
  const std::size_t checkpoint_count = count / kCheckpointInterval + 1;
  const bool within_budget =
      max_value_bytes.size() <= 4096 &&
      state_estimate <= kCheckpointBudgetBytes / std::max<std::size_t>(checkpoint_count, 1);

  WatchMap state;
  for (std::size_t i = 0; i < count; ++i) {
    const Event& event = record_->events[i];
    if (event.type == EventType::Watch) {
      state[event.task][event.s1] = event.s2;
    }
    if (within_budget && ((i + 1) % kCheckpointInterval == 0 || i + 1 == count)) {
      checkpoints_.emplace(i, state);
    }
  }
}

void DebugSession::apply_event(WatchMap& state, const Event& event) const {
  if (event.type == EventType::Watch) {
    state[event.task][event.s1] = event.s2;
  }
}

const Event* DebugSession::event_at(std::size_t index) const noexcept {
  if (!record_ || index >= record_->events.size()) {
    return nullptr;
  }
  return &record_->events[index];
}

WatchMap DebugSession::state_at(std::size_t index) const {
  WatchMap state;
  if (!record_ || record_->events.empty() || index >= record_->events.size()) {
    return state;
  }
  std::size_t start = 0;
  if (!checkpoints_.empty()) {
    auto it = checkpoints_.upper_bound(index);
    if (it != checkpoints_.begin()) {
      --it;
      state = it->second;
      start = it->first + 1;
    }
  }
  for (std::size_t i = start; i <= index && i < record_->events.size(); ++i) {
    apply_event(state, record_->events[i]);
  }
  return state;
}

DebugFrame DebugSession::frame_at(std::size_t index) const {
  DebugFrame frame;
  frame.index = index;
  if (const Event* event = event_at(index); event != nullptr) {
    frame.event = *event;
  }
  frame.state = state_at(index);
  frame.is_divergence = divergence_index_.has_value() && *divergence_index_ == index;
  return frame;
}

std::optional<std::size_t> DebugSession::step_forward(std::size_t index) const noexcept {
  if (!record_ || index + 1 >= record_->events.size()) {
    return std::nullopt;
  }
  return index + 1;
}

std::optional<std::size_t> DebugSession::step_back(std::size_t index) const noexcept {
  if (!record_ || index == 0 || index > record_->events.size()) {
    return std::nullopt;
  }
  return index - 1;
}

std::vector<Breakpoint> DebugSession::breakpoints() const {
  return breakpoints_;
}

std::optional<std::uint32_t> DebugSession::add_breakpoint(Breakpoint breakpoint) {
  if (breakpoints_.size() >= limits::kMaxBreakpoints) {
    return std::nullopt;
  }
  if (breakpoint.has_text && breakpoint.text.size() > 128) {
    breakpoint.text.resize(128);
  }
  if (breakpoint.id == 0) {
    breakpoint.id = next_breakpoint_id_++;
  } else if (breakpoint.id >= next_breakpoint_id_) {
    next_breakpoint_id_ = breakpoint.id + 1;
  }
  breakpoints_.push_back(std::move(breakpoint));
  return breakpoints_.back().id;
}

bool DebugSession::remove_breakpoint(std::uint32_t id) {
  const auto before = breakpoints_.size();
  breakpoints_.erase(
      std::remove_if(breakpoints_.begin(), breakpoints_.end(),
                     [id](const Breakpoint& bp) { return bp.id == id; }),
      breakpoints_.end());
  return breakpoints_.size() != before;
}

bool DebugSession::set_breakpoint_enabled(std::uint32_t id, bool enabled) {
  for (auto& breakpoint : breakpoints_) {
    if (breakpoint.id == id) {
      breakpoint.enabled = enabled;
      return true;
    }
  }
  return false;
}

void DebugSession::clear_breakpoints() {
  breakpoints_.clear();
}

bool DebugSession::matches(const Event& event) const {
  for (const auto& breakpoint : breakpoints_) {
    if (!breakpoint.enabled) {
      continue;
    }
    if (breakpoint.has_type && event.type != breakpoint.type) {
      continue;
    }
    if (breakpoint.has_task && event.task != breakpoint.task) {
      continue;
    }
    if (breakpoint.has_text) {
      const std::string needle = lower(breakpoint.text);
      const bool hit = lower(event.s1).find(needle) != std::string::npos ||
                       lower(event.s2).find(needle) != std::string::npos ||
                       lower(event_type_name(event.type)).find(needle) != std::string::npos;
      if (!hit) {
        continue;
      }
    }
    return true;
  }
  return false;
}

std::optional<std::size_t> DebugSession::next_breakpoint(std::size_t from,
                                                         bool forward) const {
  if (!record_ || breakpoints_.empty()) {
    return std::nullopt;
  }
  if (forward) {
    for (std::size_t i = from + 1; i < record_->events.size(); ++i) {
      if (matches(record_->events[i])) {
        return i;
      }
    }
  } else {
    if (from == 0) {
      return std::nullopt;
    }
    std::size_t i = std::min(from - 1, record_->events.size() - 1);
    for (;;) {
      if (matches(record_->events[i])) {
        return i;
      }
      if (i == 0) {
        break;
      }
      --i;
    }
  }
  return std::nullopt;
}

void DebugSession::set_divergence(std::optional<std::size_t> index, std::string reason) {
  if (index.has_value() && record_ && *index >= record_->events.size()) {
    index.reset();
  }
  divergence_index_ = index;
  divergence_reason_ = std::move(reason);
}

std::vector<TaskLane> DebugSession::timeline() const {
  std::vector<TaskLane> lanes;
  if (!record_) {
    return lanes;
  }
  std::map<TaskId, std::size_t> lane_index;
  for (std::size_t i = 0; i < record_->events.size(); ++i) {
    const Event& event = record_->events[i];
    if (event.task == kNoTask && event.type != EventType::ScenarioStart &&
        event.type != EventType::ScenarioEnd) {
      continue;
    }
    auto [it, inserted] = lane_index.emplace(event.task, lanes.size());
    if (inserted) {
      lanes.push_back(TaskLane{});
      TaskLane& lane = lanes.back();
      lane.task = event.task;
      lane.name = event.task == kNoTask ? "system" : "task " + std::to_string(event.task);
    }
    TaskLane& lane = lanes[it->second];
    lane.events.push_back(i);
    ++lane.event_count;
    if (event.type == EventType::Spawn) {
      lane.name = event.s1;
      lane.spawn_ns = event.d;
    }
    if (event.type == EventType::Exit) {
      lane.exited = true;
      lane.failed = event.b == 0;
      lane.end_ns = event.a;
    }
    if (event.type == EventType::ScenarioEnd) {
      lane.end_ns = event.b;
    }
  }
  return lanes;
}

std::string DebugSession::render_state(const WatchMap& state) {
  std::string out;
  for (const auto& [task, values] : state) {
    out += "task ";
    out += std::to_string(task);
    out += "\n";
    for (const auto& [name, value] : values) {
      out += "  ";
      out += name;
      out += " = ";
      out += value;
      out += "\n";
    }
  }
  return out;
}

std::size_t DebugSession::state_key_count(const WatchMap& state) noexcept {
  std::size_t count = 0;
  for (const auto& [task, values] : state) {
    (void)task;
    count += values.size();
  }
  return count;
}

}  // namespace tempo
