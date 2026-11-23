#pragma once

#include <string>
#include <vector>

#include "tempo/record.hpp"

namespace tempo::test {

inline Record sample_record() {
  Record record;
  record.meta.scenario = "sample";
  record.meta.tool_version = "test";
  record.meta.seed = 42;
  record.meta.created_unix = 1700000000;
  record.meta.start_ns = 1'000'000;
  record.meta.attributes["kind"] = "unit-test";

  Event start;
  start.type = EventType::ScenarioStart;
  start.a = 42;
  start.s1 = "sample";
  record.events.push_back(start);

  Event spawn;
  spawn.type = EventType::Spawn;
  spawn.task = 1;
  spawn.a = 0;
  spawn.b = 1;
  spawn.s1 = "worker";
  record.events.push_back(spawn);

  Event step;
  step.type = EventType::Step;
  step.task = 1;
  step.a = 0;
  step.b = 1;
  step.c = 0xDEADBEEFCAFEF00Dull;
  step.d = 1'000'000;
  step.s1 = "worker";
  record.events.push_back(step);

  Event watch;
  watch.type = EventType::Watch;
  watch.task = 1;
  watch.s1 = "counter";
  watch.s2 = "7";
  record.events.push_back(watch);

  Event read;
  read.type = EventType::FileRead;
  read.task = 1;
  read.a = 1;
  read.b = 5;
  read.s1 = "data/input.txt";
  read.bytes = std::string("a\0b\0c", 5);
  record.events.push_back(read);

  Event end;
  end.type = EventType::ScenarioEnd;
  end.a = 1;
  end.b = 2'000'000;
  end.c = 0;
  end.s1 = "ok";
  record.events.push_back(end);

  record.recompute_hash();
  return record;
}

inline bool same_events(const std::vector<Event>& a, const std::vector<Event>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (!(a[i] == b[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace tempo::test
