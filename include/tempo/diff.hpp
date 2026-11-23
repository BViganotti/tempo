#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tempo/event.hpp"
#include "tempo/record.hpp"

namespace tempo {

struct EventTypeCount {
  EventType type = EventType::Unknown;
  std::uint32_t in_a = 0;
  std::uint32_t in_b = 0;
};

/// First place two event streams differ.
struct EventDifference {
  bool found = false;
  std::size_t index = 0;
  bool has_a = false;
  bool has_b = false;
  Event a;
  Event b;
  std::string summary_a;
  std::string summary_b;
};

/// Structural comparison of two records, used by the run-diff view.
struct RecordDiff {
  bool equal = true;
  std::size_t a_events = 0;
  std::size_t b_events = 0;
  std::size_t common_prefix = 0;
  std::uint64_t a_hash = 0;
  std::uint64_t b_hash = 0;
  std::string a_scenario;
  std::string b_scenario;
  EventDifference first;
  std::vector<EventTypeCount> counts;
};

/// Compare events in order. Equality requires identical event streams; hashes
/// are reported for diagnostics even when streams match.
RecordDiff diff_records(const Record& a, const Record& b);

}  // namespace tempo
