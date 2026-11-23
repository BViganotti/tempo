#include "tempo/diff.hpp"

#include <array>
#include <algorithm>

namespace tempo {

RecordDiff diff_records(const Record& a, const Record& b) {
  RecordDiff diff;
  diff.a_events = a.events.size();
  diff.b_events = b.events.size();
  diff.a_hash = a.hash;
  diff.b_hash = b.hash;
  diff.a_scenario = a.meta.scenario;
  diff.b_scenario = b.meta.scenario;

  const std::size_t limit = std::min(a.events.size(), b.events.size());
  while (diff.common_prefix < limit &&
         a.events[diff.common_prefix] == b.events[diff.common_prefix]) {
    ++diff.common_prefix;
  }
  if (diff.common_prefix < a.events.size() || diff.common_prefix < b.events.size()) {
    diff.equal = false;
    diff.first.found = true;
    diff.first.index = diff.common_prefix;
    if (diff.common_prefix < a.events.size()) {
      diff.first.has_a = true;
      diff.first.a = a.events[diff.common_prefix];
      diff.first.summary_a = event_summary(diff.first.a);
    }
    if (diff.common_prefix < b.events.size()) {
      diff.first.has_b = true;
      diff.first.b = b.events[diff.common_prefix];
      diff.first.summary_b = event_summary(diff.first.b);
    }
  } else {
    diff.equal = a.hash == b.hash;
  }

  std::array<std::uint32_t, 32> counts_a{};
  std::array<std::uint32_t, 32> counts_b{};
  auto tally = [](const std::vector<Event>& events, std::array<std::uint32_t, 32>& out) {
    for (const auto& event : events) {
      const auto index = static_cast<std::size_t>(event.type);
      if (index < out.size()) {
        ++out[index];
      }
    }
  };
  tally(a.events, counts_a);
  tally(b.events, counts_b);
  for (std::size_t i = 0; i < counts_a.size(); ++i) {
    if (counts_a[i] == 0 && counts_b[i] == 0) {
      continue;
    }
    diff.counts.push_back(
        {static_cast<EventType>(i), counts_a[i], counts_b[i]});
  }
  return diff;
}

}  // namespace tempo
