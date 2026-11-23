#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <string>

#include "tempo/host.hpp"
#include "tempo/record.hpp"
#include "tempo/runtime.hpp"

namespace tempo {

inline constexpr std::uint64_t kNoMutation = std::numeric_limits<std::uint64_t>::max();

struct RecordRunOptions {
  std::uint64_t seed = 1;
  std::uint32_t max_steps = limits::kMaxSteps;
  std::uint32_t max_tasks = limits::kMaxTasks;
  std::uint64_t start_ns = 1'000'000;
  /// Optional schedule intervention for producing a second, genuinely different
  /// recording. At `mutate_step`, the recorder picks another ready task.
  std::uint64_t mutate_step = kNoMutation;
  std::uint32_t mutate_choice = 0;
  /// Optional scenario/source build id stored in record metadata.
  std::string build_id;
};

struct RecordRunResult {
  bool ok = false;
  Record record;
  RunSummary summary;
  std::string error;
  bool mutation_applied = false;
};

/// Record one cooperative run.
///
/// `capabilities` supplies real external effects; all scheduler events are
/// captured by an internal recording host. The returned record is complete even
/// when the run itself failed (a failed run is a reproducible fact); `ok` is
/// false in that case but the record may still be saved and replayed.
///
/// Throws nothing; infrastructure failures are reported in `error`. If capture
/// hits a hard event limit, no partial record is returned.
RecordRunResult record_run(std::string scenario_name, std::function<Task(Context)> entry,
                           RunHost& capabilities, RecordRunOptions options = {});

}  // namespace tempo
