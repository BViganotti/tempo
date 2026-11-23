#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "tempo/event.hpp"
#include "tempo/record.hpp"
#include "tempo/record_run.hpp"
#include "tempo/runtime.hpp"

namespace tempo {

struct ReplayOptions {
  /// Optional single schedule intervention. Setting it deliberately produces a
  /// divergence; use it to find which decision matters.
  std::uint64_t mutate_step = kNoMutation;
  std::uint32_t mutate_choice = 0;
  std::uint32_t max_steps = limits::kMaxSteps;
  std::uint32_t max_tasks = limits::kMaxTasks;
};

/// The first place a replay stopped matching its record.
struct Divergence {
  bool found = false;
  std::size_t index = 0;
  std::string reason;
  bool has_expected = false;
  bool has_actual = false;
  Event expected;
  Event actual;
  std::string expected_summary;
  std::string actual_summary;
};

struct ReplayReport {
  /// Infrastructure success: the scenario was spawned and ran to a stop.
  bool ok = false;
  /// The replay regenerated the exact recorded event stream.
  bool identical = false;
  bool mutation_applied = false;
  bool complete = false;
  std::uint64_t recorded_hash = 0;
  std::uint64_t replayed_hash = 0;
  std::size_t recorded_events = 0;
  std::size_t replayed_events = 0;
  std::size_t compared_events = 0;
  std::string error;
  Divergence first_divergence;
  RunSummary summary;
};

/// Replay a complete record by re-executing `entry` against recorded decisions
/// and capability results. No OS effects occur: file, env and socket calls are
/// answered from the record only.
///
/// The scenario entry must be the same code that produced the record for an
/// identical result; a changed program is expected to diverge and is reported.
/// The returned `first_divergence` is the authoritative diagnostic.
///
/// Thread-safety: the replay runs on the calling thread and may be invoked
/// concurrently for different records as long as the entry has no shared state.
ReplayReport replay_record(const Record& record, std::function<Task(Context)> entry,
                           ReplayOptions options = {});

/// Result of replaying one record `runs` times and comparing hashes.
struct ReplayBatchReport {
  bool ok = false;
  std::uint32_t runs = 0;
  bool all_identical = false;
  bool all_hashes_equal = false;
  std::uint64_t expected_hash = 0;
  std::vector<std::uint64_t> hashes;
  std::uint64_t elapsed_ns = 0;
  /// 0-based run that first diverged, or -1 when every run matched.
  std::int64_t first_failed_run = -1;
  std::string error;
};

/// Replay the record repeatedly (1..200 runs) and prove hash stability.
ReplayBatchReport replay_many(const Record& record, std::function<Task(Context)> entry,
                              std::uint32_t runs, ReplayOptions options = {});

}  // namespace tempo
