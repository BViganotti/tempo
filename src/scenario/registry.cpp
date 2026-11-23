#include "tempo/scenario.hpp"

#include <utility>

#include "scenario/demo.hpp"

namespace tempo {

void ScenarioRegistry::add(ScenarioInfo info) {
  if (info.name.empty() || info.name.size() > 128 || !info.entry) {
    throw LimitError("scenario requires a short name and an entry");
  }
  if (find(info.name) != nullptr) {
    throw LimitError("duplicate scenario name: " + info.name);
  }
  scenarios_.push_back(std::move(info));
}

const ScenarioInfo* ScenarioRegistry::find(std::string_view name) const {
  for (const auto& scenario : scenarios_) {
    if (scenario.name == name) {
      return &scenario;
    }
  }
  return nullptr;
}

std::vector<ScenarioInfo> ScenarioRegistry::list() const {
  return scenarios_;
}

const ScenarioRegistry& ScenarioRegistry::builtins() {
  static const ScenarioRegistry registry = [] {
    ScenarioRegistry built;
    built.add(ScenarioInfo{
        "counter-race",
        "Two tasks race on a shared counter with a yield between read and write. "
        "The recorded schedule loses updates; replay proves the failure is stable.",
        {},
        scenarios::counter_race});
    built.add(ScenarioInfo{
        "flaky-service",
        "A cooperative service reads a fixture, an environment variable and a "
        "random draw, then two tasks race on shared state.",
        {{"fixtures/service.conf", "mode=recorded\nretries=2\nwindow_ms=5\n"}},
        scenarios::flaky_service});
    built.add(ScenarioInfo{
        "pipeline",
        "Three pipeline stages with virtual sleeps and joins, useful for the "
        "timeline view.",
        {},
        scenarios::pipeline});
    return built;
  }();
  return registry;
}

}  // namespace tempo
