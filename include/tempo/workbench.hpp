#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tempo/debug.hpp"
#include "tempo/http.hpp"
#include "tempo/json.hpp"
#include "tempo/record.hpp"
#include "tempo/record_run.hpp"
#include "tempo/replay.hpp"
#include "tempo/scenario.hpp"

namespace tempo {

struct WorkbenchOptions {
  /// Root for records, scratch fixtures and capability file access. Empty uses
  /// the platform default (macOS: ~/Library/Application Support/Tempo).
  std::string data_dir;
  std::size_t max_records = limits::kMaxStoredRecords;
};

/// A recorded run held by the workbench.
struct StoredRecord {
  RecordId id = 0;
  std::shared_ptr<const Record> record;
  RunSummary summary;
  std::string file;
  bool saved = false;
  std::uint64_t created_unix = 0;
};

/// Local workbench service: record store, replay driver, debugger sessions,
/// breakpoints and the JSON API used by the embedded UI.
///
/// Thread-safety: not thread-safe. It is designed to be driven by the HTTP
/// server's single event-loop thread (or a single test thread). Debug sessions
/// and records are read-only after creation; breakpoint mutations are applied
/// synchronously.
class Workbench {
 public:
  explicit Workbench(WorkbenchOptions options = {});
  ~Workbench();
  Workbench(const Workbench&) = delete;
  Workbench& operator=(const Workbench&) = delete;

  bool initialize(std::string* error);
  bool initialized() const noexcept { return initialized_; }
  const std::string& data_dir() const noexcept { return options_.data_dir; }
  const std::vector<std::string>& load_warnings() const noexcept { return load_warnings_; }

  /// Serve a static asset or API request. Never throws; errors become JSON.
  HttpResponse handle(const HttpRequest& request);

  /// Broadcast hook installed by the launcher (wired to HttpServer::broadcast_sse).
  void set_broadcaster(std::function<void(std::string_view, std::string_view)> broadcast) {
    broadcaster_ = std::move(broadcast);
  }

  struct RecordOutcome {
    bool ok = false;
    bool run_ok = false;
    RecordId id = 0;
    std::string error;
    std::string warning;
    RunSummary summary;
    std::shared_ptr<const Record> record;
  };

  RecordOutcome record_scenario(std::string_view scenario, std::uint64_t seed,
                                std::uint64_t mutate_step = kNoMutation,
                                std::uint32_t mutate_choice = 0);

  struct ReplayOutcome {
    bool ok = false;
    bool batch = false;
    ReplayReport single;
    ReplayBatchReport multi;
    std::uint64_t elapsed_ns = 0;
    std::string error;
  };
  ReplayOutcome replay(RecordId id, std::uint32_t runs, ReplayOptions options = {});

  std::shared_ptr<const Record> get_record(RecordId id) const;
  std::vector<StoredRecord> records() const;

  const std::vector<Breakpoint>& breakpoints() const noexcept { return breakpoints_; }
  std::optional<std::uint32_t> add_breakpoint(Breakpoint breakpoint);
  bool toggle_breakpoint(std::uint32_t id, bool enabled);
  bool remove_breakpoint(std::uint32_t id);
  void clear_breakpoints();
  DebugSession* session_for(RecordId id, bool create = true);

 private:
  HttpResponse handle_api(const HttpRequest& request);
  json::Value overview_json() const;
  json::Value record_json(const StoredRecord& record) const;
  json::Value event_json(std::size_t index, const Event& event) const;
  json::Value state_json(const WatchMap& state) const;
  json::Value summary_json(const RunSummary& summary) const;
  json::Value breakpoints_json() const;
  json::Value replay_json(RecordId id, const ReplayOutcome& outcome,
                          std::uint32_t runs, ReplayOptions options) const;
  HttpResponse static_response(std::string_view path) const;

  RecordId add_record(Record record, RunSummary summary);
  bool save_record_file(const StoredRecord& stored, std::string* warning);
  bool prepare_fixtures(const ScenarioInfo& scenario, std::string* error);
  void apply_breakpoints(DebugSession& session);
  void broadcast_overview();
  void broadcast_replay(const json::Value& payload);

  WorkbenchOptions options_;
  bool initialized_ = false;
  std::vector<StoredRecord> records_;
  std::map<RecordId, std::unique_ptr<DebugSession>> sessions_;
  std::vector<Breakpoint> breakpoints_;
  std::uint32_t next_breakpoint_id_ = 1;
  RecordId next_record_id_ = 1;
  RecordId last_record_ = 0;
  std::vector<std::string> load_warnings_;
  std::function<void(std::string_view, std::string_view)> broadcaster_;
};

/// Default workbench data directory for this platform.
std::string default_data_dir();

}  // namespace tempo
