#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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
  /// Global approximate memory budget for retained records. Records are evicted
  /// oldest-first from memory (files are kept); a single recording larger than
  /// the budget is rejected rather than silently dropped.
  std::size_t max_total_bytes = 256u * 1024u * 1024u;
};

/// A recorded run held by the workbench.
struct StoredRecord {
  RecordId id = 0;
  std::shared_ptr<const Record> record;
  RunSummary summary;
  std::string file;
  bool saved = false;
  std::uint64_t created_unix = 0;
  std::size_t memory_bytes = 0;
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
  std::size_t retained_bytes() const noexcept { return used_bytes_; }
  std::size_t memory_budget() const noexcept { return options_.max_total_bytes; }

  /// Serve a static asset or API request. Never throws; errors become JSON.
  HttpResponse handle(const HttpRequest& request);

  /// Broadcast hook installed by the launcher (wired to HttpServer::broadcast_sse).
  void set_broadcaster(std::function<void(std::string_view, std::string_view)> broadcast) {
    broadcaster_ = std::move(broadcast);
  }

  /// Token injected into the embedded index document so CSS/JS asset requests
  /// carry the same session credential as the page. Set by the launcher before
  /// the server starts.
  void set_session_token(std::string token) { session_token_ = std::move(token); }

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

  struct AsyncJobOutcome {
    bool ok = false;
    std::uint64_t job_id = 0;
    std::string error;
  };
  /// Queue a replay on the single background job worker and return immediately.
  /// Long batches no longer block the HTTP/SSE loop.
  AsyncJobOutcome enqueue_replay(RecordId id, std::uint32_t runs, ReplayOptions options = {});
  /// JSON status/result for a job, or null when unknown.
  std::optional<json::Value> job_status(std::uint64_t job_id);
  json::Value jobs_json();

  struct ImportOutcome {
    bool ok = false;
    bool duplicate = false;
    RecordId id = 0;
    std::string error;
    std::shared_ptr<const Record> record;
  };
  /// Import a strict, complete record container. Idempotent by hash.
  ImportOutcome import_record(std::string_view bytes);

  /// Remove a record from memory and, when `delete_file` is set, from disk.
  bool delete_record(RecordId id, bool delete_file, std::string* error);

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
  json::Value replay_json(RecordId id, const std::shared_ptr<const Record>& record,
                          const ReplayOutcome& outcome, std::uint32_t runs,
                          ReplayOptions options) const;
  void start_worker_locked();
  void worker_loop();
  struct ReplayJob {
    std::uint64_t id = 0;
    std::string status = "queued";
    ReplayOutcome outcome;
    std::shared_ptr<const Record> record;
    ReplayOptions options;
    std::uint32_t runs = 1;
    std::string error;
  };
  HttpResponse static_response(std::string_view path) const;

  RecordId add_record(Record record, RunSummary summary);
  bool save_record_file(StoredRecord& stored, std::string* warning);
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
  std::size_t used_bytes_ = 0;
  std::function<void(std::string_view, std::string_view)> broadcaster_;
  std::string session_token_;

  std::mutex jobs_mutex_;
  std::condition_variable jobs_cv_;
  std::deque<std::uint64_t> job_queue_;
  std::map<std::uint64_t, ReplayJob> jobs_;
  std::uint64_t next_job_id_ = 1;
  bool worker_stop_ = false;
  std::thread worker_;
};

/// Default workbench data directory for this platform.
std::string default_data_dir();

}  // namespace tempo
