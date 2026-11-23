#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "tempo/runtime.hpp"

namespace tempo {

/// A built-in or embedded scenario that the workbench can record.
struct ScenarioInfo {
  std::string name;
  std::string description;
  /// Files written into the data root before recording, as (relative path,
  /// content). Paths follow the same sandbox rules as the file capability.
  std::vector<std::pair<std::string, std::string>> fixtures;
  std::function<Task(Context)> entry;
};

/// Ordered collection of scenarios. Copyable; the entries are std::function so
/// applications can embed their own cooperative programs.
class ScenarioRegistry {
 public:
  void add(ScenarioInfo info);
  const ScenarioInfo* find(std::string_view name) const;
  std::vector<ScenarioInfo> list() const;
  std::size_t size() const noexcept { return scenarios_.size(); }

  /// The scenarios shipped with Tempo: counter-race, flaky-service, pipeline.
  static const ScenarioRegistry& builtins();

 private:
  std::vector<ScenarioInfo> scenarios_;
};

}  // namespace tempo
