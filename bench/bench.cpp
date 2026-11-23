// Tempo micro-benchmarks. Every number printed here is measured on this
// machine during this run; nothing is precomputed or fabricated.
//
// Usage: tempo_bench

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "tempo/json.hpp"
#include "tempo/record.hpp"
#include "tempo/record_run.hpp"
#include "tempo/replay.hpp"
#include "tempo/scenario.hpp"
#include "tempo/system.hpp"
#include "tempo/version.hpp"

namespace {

using Clock = std::chrono::steady_clock;

template <typename Fn>
double measure_ms(Fn&& fn) {
  const auto start = Clock::now();
  fn();
  const auto end = Clock::now();
  return std::chrono::duration<double, std::milli>(end - start).count();
}

tempo::Record make_large_record(std::size_t count) {
  tempo::Record record;
  record.meta.scenario = "bench";
  record.meta.tool_version = std::string(tempo::kVersion);
  record.meta.seed = 42;
  record.meta.created_unix = 1700000000;
  record.events.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    tempo::Event event;
    event.type = static_cast<tempo::EventType>(1 + (i % 26));
    event.task = static_cast<tempo::TaskId>(1 + (i % 8));
    event.a = i;
    event.b = i * 3;
    event.c = i * 7;
    event.d = i * 1'000;
    event.s1 = "event-" + std::to_string(i % 100);
    event.s2 = "value-" + std::to_string(i % 37);
    if (i % 11 == 0) {
      event.bytes.assign(32, static_cast<char>('a' + (i % 26)));
    }
    record.events.push_back(std::move(event));
  }
  record.recompute_hash();
  return record;
}

void print_row(const char* name, double ms, std::size_t units, double bytes = 0) {
  const double seconds = ms / 1000.0;
  const double ops = seconds > 0 ? static_cast<double>(units) / seconds : 0.0;
  std::printf("%-30s %10.2f ms  %12.0f ops/s", name, ms, ops);
  if (bytes > 0 && seconds > 0) {
    std::printf("  %9.1f MiB/s", bytes / seconds / (1024.0 * 1024.0));
  }
  std::printf("\n");
}

std::string event_page_json(const tempo::Record& record, std::size_t from, std::size_t limit) {
  tempo::json::Value root = tempo::json::Value::object();
  tempo::json::Value events = tempo::json::Value::array();
  const std::size_t end = std::min(record.events.size(), from + limit);
  for (std::size_t i = from; i < end; ++i) {
    const tempo::Event& event = record.events[i];
    tempo::json::Value item = tempo::json::Value::object();
    item["i"] = tempo::json::Value(static_cast<std::uint64_t>(i));
    item["type"] = tempo::json::Value(std::string(tempo::event_type_name(event.type)));
    item["task"] = tempo::json::Value(static_cast<std::uint64_t>(event.task));
    item["summary"] = tempo::json::Value(tempo::event_summary(event));
    events.push(std::move(item));
  }
  root["events"] = std::move(events);
  return root.dump();
}

}  // namespace

int main() {
  std::printf("Tempo %s benchmarks\n", std::string(tempo::kVersion).c_str());
  std::printf("record format v%d, compiler C++20\n\n", tempo::kRecordFormatVersion);

  constexpr std::size_t kEvents = 20'000;
  const tempo::Record large = make_large_record(kEvents);
  std::printf("record: %zu events, %s\n", kEvents, large.hash_hex().c_str());

  tempo::SerializeOutcome serialized;
  const double serialize_ms = measure_ms([&] {
    for (int i = 0; i < 10; ++i) {
      serialized = tempo::serialize_record(large);
      if (!serialized.ok) {
        std::fprintf(stderr, "serialize failed: %s\n", serialized.error.c_str());
      }
    }
  });
  print_row("serialize x10", serialize_ms, kEvents * 10, serialized.bytes.size() * 10.0);
  std::printf("serialized size: %zu bytes\n", serialized.bytes.size());

  tempo::LoadOutcome loaded;
  const double load_ms = measure_ms([&] {
    for (int i = 0; i < 10; ++i) {
      loaded = tempo::load_record_bytes(serialized.bytes);
      if (!loaded.ok) {
        std::fprintf(stderr, "load failed: %s\n", loaded.error.c_str());
      }
    }
  });
  print_row("strict load x10", load_ms, kEvents * 10, serialized.bytes.size() * 10.0);

  const double hash_ms = measure_ms([&] {
    for (int i = 0; i < 100; ++i) {
      volatile std::uint64_t hash = large.compute_hash();
      (void)hash;
    }
  });
  print_row("event hash x100", hash_ms, kEvents * 100);

  std::string page;
  const double json_ms = measure_ms([&] {
    for (int i = 0; i < 20; ++i) {
      page = event_page_json(large, 0, 1000);
    }
  });
  print_row("JSON event page x20", json_ms, 1'000 * 20, page.size() * 20.0);

  const std::filesystem::path temp =
      std::filesystem::temp_directory_path() / "tempo-bench-record.tmpo";
  double write_ms = 0;
  bool file_ok = false;
  const double file_io_ms = measure_ms([&] {
    for (int i = 0; i < 5; ++i) {
      std::string error;
      if (tempo::write_record_file(large, temp.string(), &error)) {
        file_ok = true;
        write_ms += 0;
      }
    }
    for (int i = 0; i < 5; ++i) {
      const tempo::LoadOutcome from_file = tempo::load_record_file(temp.string());
      if (!from_file.ok) {
        std::fprintf(stderr, "file load failed: %s\n", from_file.error.c_str());
      }
    }
  });
  print_row("file save + load x5", file_io_ms, kEvents * 10, serialized.bytes.size() * 10.0);
  std::error_code ec;
  std::filesystem::remove(temp, ec);

  const tempo::ScenarioInfo* scenario =
      tempo::ScenarioRegistry::builtins().find("counter-race");
  if (scenario != nullptr) {
    tempo::SystemHost host(tempo::SystemCapabilityOptions{});
    tempo::RecordRunOptions record_options;
    record_options.seed = 7;
    tempo::RecordRunResult recorded =
        tempo::record_run(scenario->name, scenario->entry, host, record_options);
    if (recorded.ok) {
      const double record_ms = measure_ms([&] {
        for (int i = 0; i < 50; ++i) {
          tempo::RecordRunResult again =
              tempo::record_run(scenario->name, scenario->entry, host, record_options);
          if (!again.ok) {
            std::fprintf(stderr, "record run failed\n");
          }
        }
      });
      print_row("record counter-race x50", record_ms, 50);

      const double replay_ms = measure_ms([&] {
        for (int i = 0; i < 500; ++i) {
          const tempo::ReplayReport report =
              tempo::replay_record(recorded.record, scenario->entry);
          if (!report.identical) {
            std::fprintf(stderr, "replay diverged!\n");
          }
        }
      });
      print_row("replay counter-race x500", replay_ms,
                static_cast<std::size_t>(recorded.record.events.size()) * 500);
      std::printf("counter-race replay: %zu events/run, hash %s\n",
                  recorded.record.events.size(), recorded.record.hash_hex().c_str());
    }
  }

  std::printf("\nbenchmark complete%s\n", file_ok ? "" : " (file I/O failed)");
  return 0;
}
