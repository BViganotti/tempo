#include "tempo/workbench.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <type_traits>
#include <utility>

#include "tempo/assets.hpp"
#include "tempo/diff.hpp"
#include "tempo/limits.hpp"
#include "tempo/system.hpp"
#include "tempo/version.hpp"

namespace tempo {
namespace {

using json::Value;

Value jstr(std::string_view text) { return Value(std::string(text)); }

template <typename T>
  requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
Value number(T value) {
  if constexpr (std::is_signed_v<T>) {
    return Value(static_cast<std::int64_t>(value));
  } else {
    return Value(static_cast<std::uint64_t>(value));
  }
}

Value error_body(std::string_view message) {
  Value body = Value::object();
  body["error"] = jstr(message);
  return body;
}

HttpResponse json_response(Value body, int status = 200) {
  HttpResponse response;
  response.status = status;
  response.content_type = "application/json; charset=utf-8";
  response.body = body.dump();
  return response;
}

HttpResponse error_response(int status, std::string_view message) {
  return json_response(error_body(message), status);
}

bool parse_u64(std::string_view text, std::uint64_t& value) {
  if (text.empty() || text.size() > 20) {
    return false;
  }
  value = 0;
  for (char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
    const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
    if (value > (UINT64_MAX - digit) / 10) {
      return false;
    }
    value = value * 10 + digit;
  }
  return true;
}

std::uint64_t event_time(const Event& event) {
  switch (event.type) {
    case EventType::Step:
      return event.d;
    case EventType::Spawn:
      return event.d;
    case EventType::Exit:
      return event.a;
    case EventType::Sleep:
      return event.b;
    case EventType::Wake:
      return event.b;
    case EventType::Yield:
      return event.a;
    case EventType::Cancel:
      return event.a;
    case EventType::Watch:
      return event.a;
    case EventType::Note:
      return event.a;
    case EventType::Log:
      return event.a;
    case EventType::Rng:
      return event.d;
    case EventType::ClockJump:
      return event.d;
    case EventType::Join:
      return event.b;
    case EventType::ScenarioStart:
      return event.d;
    case EventType::ScenarioEnd:
      return event.b;
    case EventType::Error:
      return event.a;
    default:
      return event.d;
  }
}

void tighten_directory(const std::filesystem::path& path) noexcept {
  std::error_code ec;
  std::filesystem::permissions(path, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, ec);
}

void tighten_file(const std::filesystem::path& path) noexcept {
  std::error_code ec;
  std::filesystem::permissions(path,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, ec);
}

Value safe_u64(std::uint64_t value) {
  if (value <= 9'007'199'254'740'991ull) {
    return Value(value);
  }
  return Value(std::to_string(value));
}

bool read_json_uint(const Value* value, std::uint64_t& out) {
  if (value == nullptr) {
    return false;
  }
  if (value->is_number()) {
    const std::int64_t parsed = value->as_i64(-1);
    if (parsed < 0) {
      return false;
    }
    out = static_cast<std::uint64_t>(parsed);
    return true;
  }
  if (value->is_string()) {
    return parse_u64(value->as_string(), out);
  }
  return false;
}

std::string slugify(std::string_view name) {
  std::string out;
  for (char c : name) {
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
      out.push_back(c);
    } else if (c >= 'A' && c <= 'Z') {
      out.push_back(static_cast<char>(c - 'A' + 'a'));
    } else if (c == '-' || c == '_') {
      out.push_back('-');
    }
    if (out.size() >= 32) {
      break;
    }
  }
  if (out.empty()) {
    out = "record";
  }
  return out;
}

std::string hash_short(const Record& record) {
  return record.hash_hex().substr(0, 8);
}

const ScenarioInfo* find_scenario(std::string_view name) {
  return ScenarioRegistry::builtins().find(name);
}

std::vector<std::string> split_path(std::string_view path) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t slash = path.find('/', start);
    const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
    if (end > start) {
      parts.emplace_back(path.substr(start, end - start));
    }
    if (slash == std::string_view::npos) {
      break;
    }
    start = slash + 1;
  }
  return parts;
}

bool event_type_from_name(std::string_view name, EventType& out) {
  for (int i = 0; i <= 26; ++i) {
    const EventType type = static_cast<EventType>(i);
    if (event_type_name(type) == name) {
      out = type;
      return true;
    }
  }
  return false;
}

}  // namespace

std::string default_data_dir() {
  const char* home = std::getenv("HOME");
  if (home != nullptr && home[0] != '\0') {
    return std::string(home) + "/Library/Application Support/Tempo";
  }
  return "/tmp/tempo-workbench";
}

Workbench::Workbench(WorkbenchOptions options) : options_(std::move(options)) {
  if (options_.data_dir.empty()) {
    options_.data_dir = default_data_dir();
  }
  if (options_.max_records == 0) {
    options_.max_records = limits::kMaxStoredRecords;
  }
  if (options_.max_records > 64) {
    options_.max_records = 64;
  }
  if (options_.max_total_bytes == 0) {
    options_.max_total_bytes = 256u * 1024u * 1024u;
  }
}

Workbench::~Workbench() {
  {
    std::lock_guard<std::mutex> lock(jobs_mutex_);
    worker_stop_ = true;
  }
  jobs_cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

bool Workbench::initialize(std::string* error) {
  if (initialized_) {
    return true;
  }
  std::error_code ec;
  std::filesystem::create_directories(options_.data_dir, ec);
  if (ec) {
    if (error) {
      *error = "cannot create data directory: " + ec.message();
    }
    return false;
  }
  tighten_directory(options_.data_dir);
  std::filesystem::path records_dir = std::filesystem::path(options_.data_dir) / "records";
  std::filesystem::create_directories(records_dir, ec);
  if (ec) {
    if (error) {
      *error = "cannot create records directory: " + ec.message();
    }
    return false;
  }
  tighten_directory(records_dir);

  std::vector<std::filesystem::path> files;
  for (std::filesystem::directory_iterator it(records_dir, ec), end; it != end;
       it.increment(ec)) {
    if (ec) {
      load_warnings_.push_back("directory scan stopped: " + ec.message());
      break;
    }
    std::error_code type_error;
    if (!it->is_regular_file(type_error) || type_error) {
      continue;
    }
    if (it->path().extension() != ".tmpo") {
      continue;
    }
    files.push_back(it->path());
    if (files.size() > 512) {
      break;
    }
  }
  if (ec) {
    load_warnings_.push_back("directory scan failed: " + ec.message());
  }
  std::sort(files.begin(), files.end());
  auto remember_archive = [this](const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    if (std::find(archive_files_.begin(), archive_files_.end(), name) ==
        archive_files_.end()) {
      archive_files_.push_back(name);
    }
  };
  struct LoadedFile {
    Record record;
    std::string path;
  };
  std::vector<LoadedFile> loaded_files;
  std::size_t loaded_bytes = 0;
  std::uint64_t known_hashes[512];
  std::size_t known_hash_count = 0;
  for (auto it = files.rbegin(); it != files.rend(); ++it) {
    if (loaded_files.size() >= options_.max_records) {
      load_warnings_.push_back("older records kept on disk outside the memory cap: " +
                               it->filename().string());
      remember_archive(*it);
      continue;
    }
    std::error_code size_error;
    const auto file_size = std::filesystem::file_size(*it, size_error);
    if (!size_error && file_size > options_.max_total_bytes - loaded_bytes) {
      load_warnings_.push_back("skipped " + it->filename().string() +
                               ": file exceeds the remaining workbench memory budget");
      remember_archive(*it);
      continue;
    }
    const LoadOutcome loaded = load_record_file(it->string(), LoadMode::Recover);
    if (!loaded.ok) {
      load_warnings_.push_back("skipped " + it->filename().string() + ": " + loaded.error);
      continue;
    }
    bool duplicate = false;
    for (std::size_t i = 0; i < known_hash_count; ++i) {
      if (known_hashes[i] == loaded.record.hash) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }
    const std::size_t estimate = loaded.record.approximate_bytes();
    if (loaded_bytes + estimate > options_.max_total_bytes) {
      load_warnings_.push_back("skipped " + it->filename().string() +
                               ": record exceeds the workbench memory budget");
      remember_archive(*it);
      continue;
    }
    loaded_bytes += estimate;
    if (known_hash_count < 512) {
      known_hashes[known_hash_count++] = loaded.record.hash;
    }
    loaded_files.push_back(LoadedFile{loaded.record, it->string()});
    if (!loaded.record.complete && !loaded.record.warning.empty()) {
      load_warnings_.push_back(it->filename().string() + ": " + loaded.record.warning);
    }
  }
  std::reverse(loaded_files.begin(), loaded_files.end());
  for (auto& loaded : loaded_files) {
    auto shared = std::make_shared<Record>(std::move(loaded.record));
    StoredRecord stored;
    stored.id = next_record_id_++;
    stored.record = std::move(shared);
    stored.file = std::move(loaded.path);
    stored.saved = true;
    stored.created_unix = stored.record->meta.created_unix;
    stored.memory_bytes = stored.record->approximate_bytes();
    used_bytes_ += stored.memory_bytes;
    records_.push_back(std::move(stored));
  }
  if (!records_.empty()) {
    last_record_ = records_.back().id;
  }
  initialized_ = true;
  return true;
}

HttpResponse Workbench::handle(const HttpRequest& request) {
  try {
    if (request.path.rfind("/api/", 0) == 0 || request.path == "/api") {
      return handle_api(request);
    }
    return static_response(request.path);
  } catch (const std::exception& e) {
    return error_response(500, std::string("workbench error: ") + e.what());
  } catch (...) {
    return error_response(500, "workbench error");
  }
}

HttpResponse Workbench::static_response(std::string_view path) const {
  std::string name;
  if (path == "/" || path.empty() || path == "/index.html") {
    name = "index.html";
  } else {
    if (path.size() < 2 || path.size() > 64 || path.find('/', 1) != std::string_view::npos ||
        path.find('\\') != std::string_view::npos || path.find("..") != std::string_view::npos) {
      HttpResponse response;
      response.status = 404;
      response.content_type = "text/plain; charset=utf-8";
      response.body = "not found";
      return response;
    }
    name = std::string(path.substr(1));
  }
  const assets::EmbeddedAsset* asset = assets::find_asset(name);
  if (asset == nullptr) {
    HttpResponse response;
    response.status = 404;
    response.content_type = "text/plain; charset=utf-8";
    response.body = "not found";
    return response;
  }
  HttpResponse response;
  response.status = 200;
  response.content_type = std::string(assets::asset_mime(name));
  response.body = std::string(asset->body);
  if (name == "index.html" && !session_token_.empty()) {
    auto rewrite = [this, &response](std::string_view needle, std::string_view attribute) {
      std::string replacement(attribute);
      replacement += "?token=";
      replacement += session_token_;
      replacement += "\"";
      std::size_t position = 0;
      while ((position = response.body.find(needle, position)) != std::string::npos) {
        response.body.replace(position, needle.size(), replacement);
        position += replacement.size();
      }
    };
    rewrite("href=\"/style.css\"", "href=\"/style.css");
    rewrite("src=\"/app.js\"", "src=\"/app.js");
    rewrite("href=\"/favicon.svg\"", "href=\"/favicon.svg");
  }
  return response;
}

Value Workbench::record_json(const StoredRecord& stored) const {
  Value value = Value::object();
  value["id"] = number(stored.id);
  if (!stored.record) {
    return value;
  }
  const Record& record = *stored.record;
  value["scenario"] = jstr(record.meta.scenario);
  value["seed"] = safe_u64(record.meta.seed);
  value["events"] = number(static_cast<std::uint64_t>(record.events.size()));
  value["hash"] = jstr(record.hash_hex());
  value["hashShort"] = jstr(hash_short(record));
  value["createdUnix"] = number(stored.created_unix);
  value["memoryBytes"] = number(static_cast<std::uint64_t>(stored.memory_bytes));
  value["uid"] = jstr(record.hash_hex());
  value["complete"] = Value(record.complete);
  value["recovered"] = Value(record.recovered);
  value["toolVersion"] = jstr(record.meta.tool_version);
  value["saved"] = Value(stored.saved);
  if (!record.warning.empty()) {
    value["warning"] = jstr(record.warning);
  }
  return value;
}

Value Workbench::summary_json(const RunSummary& summary) const {
  Value value = Value::object();
  value["ok"] = Value(summary.ok);
  value["steps"] = number(summary.steps);
  value["finalNs"] = number(summary.final_ns);
  value["tasksSpawned"] = number(summary.tasks_spawned);
  value["tasksFailed"] = number(summary.tasks_failed);
  value["stepLimit"] = Value(summary.step_limit);
  value["deadlock"] = Value(summary.deadlock);
  value["hostError"] = Value(summary.host_error);
  if (!summary.error.empty()) {
    value["error"] = jstr(summary.error);
  }
  return value;
}

Value Workbench::overview_json() const {
  Value value = Value::object();
  value["version"] = jstr(kVersion);
  value["lastRecord"] = number(last_record_);
  value["memoryUsed"] = number(static_cast<std::uint64_t>(used_bytes_));
  value["memoryBudget"] = number(static_cast<std::uint64_t>(options_.max_total_bytes));
  Value scenarios = Value::array();
  for (const auto& scenario : ScenarioRegistry::builtins().list()) {
    Value item = Value::object();
    item["name"] = jstr(scenario.name);
    item["description"] = jstr(scenario.description);
    scenarios.push(std::move(item));
  }
  value["scenarios"] = std::move(scenarios);
  Value records = Value::array();
  for (const auto& stored : records_) {
    records.push(record_json(stored));
  }
  value["records"] = std::move(records);
  value["breakpoints"] = breakpoints_json();
  value["archive"] = archive_json();
  Value warnings = Value::array();
  for (const auto& warning : load_warnings_) {
    warnings.push(jstr(warning));
  }
  value["loadWarnings"] = std::move(warnings);
  value["dataDir"] = jstr(options_.data_dir);
  return value;
}

Value Workbench::event_json(std::size_t index, const Event& event) const {
  // JSON numbers are IEEE doubles here. Values above 2^53 (notably 64-bit RNG
  // draws) would lose precision, so they are rendered as decimal strings.
  auto safe_number = [](std::uint64_t value) { return safe_u64(value); };
  Value value = Value::object();
  value["i"] = number(static_cast<std::uint64_t>(index));
  value["type"] = jstr(event_type_name(event.type));
  value["typeId"] = number(static_cast<std::uint64_t>(event.type));
  value["task"] = number(static_cast<std::uint64_t>(event.task));
  value["a"] = safe_number(event.a);
  value["b"] = safe_number(event.b);
  value["c"] = safe_number(event.c);
  value["d"] = safe_number(event.d);
  value["time"] = safe_number(event_time(event));
  value["bytesLen"] = number(static_cast<std::uint64_t>(event.bytes.size()));
  if (!event.s1.empty()) {
    value["s1"] = jstr(event.s1);
  }
  if (!event.s2.empty()) {
    value["s2"] = jstr(event.s2);
  }
  value["summary"] = jstr(event_summary(event));
  return value;
}

Value Workbench::state_json(const WatchMap& state) const {
  Value value = Value::object();
  for (const auto& [task, vars] : state) {
    Value task_value = Value::object();
    for (const auto& [name, var_value] : vars) {
      task_value[name] = jstr(var_value);
    }
    value[std::to_string(task)] = std::move(task_value);
  }
  return value;
}

Value Workbench::archive_json() const {
  Value value = Value::array();
  const std::filesystem::path records_dir =
      std::filesystem::path(options_.data_dir) / "records";
  for (const auto& name : archive_files_) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(records_dir / name, ec);
    Value item = Value::object();
    item["file"] = jstr(name);
    if (!ec) {
      item["size"] = number(static_cast<std::uint64_t>(size));
    }
    value.push(std::move(item));
  }
  return value;
}

Value Workbench::breakpoints_json() const {
  Value value = Value::array();
  for (const auto& breakpoint : breakpoints_) {
    Value item = Value::object();
    item["id"] = number(breakpoint.id);
    item["enabled"] = Value(breakpoint.enabled);
    if (breakpoint.has_type) {
      item["type"] = jstr(event_type_name(breakpoint.type));
    }
    if (breakpoint.has_task) {
      item["task"] = number(static_cast<std::uint64_t>(breakpoint.task));
    }
    if (breakpoint.has_text) {
      item["text"] = jstr(breakpoint.text);
    }
    value.push(std::move(item));
  }
  return value;
}

Value Workbench::replay_json(RecordId id, const std::shared_ptr<const Record>& record,
                             const ReplayOutcome& outcome, std::uint32_t runs,
                             ReplayOptions options) const {
  Value value = Value::object();
  value["ok"] = Value(outcome.ok);
  value["recordId"] = number(id);
  value["runs"] = number(runs);
  value["mutationApplied"] = Value(outcome.batch ? false : outcome.single.mutation_applied);
  value["buildMismatch"] = Value(outcome.batch ? false : outcome.single.build_mismatch);
  if (!outcome.batch && !outcome.single.build_warning.empty()) {
    value["buildWarning"] = jstr(outcome.single.build_warning);
  }
  if (!outcome.error.empty()) {
    value["error"] = jstr(outcome.error);
  }
  value["expectedHash"] = jstr(record ? record->hash_hex() : std::string());
  value["recordedEvents"] =
      number(record ? static_cast<std::uint64_t>(record->events.size()) : 0);
  if (outcome.batch) {
    const ReplayBatchReport& batch = outcome.multi;
    value["identical"] = Value(batch.all_identical);
    value["allHashesEqual"] = Value(batch.all_hashes_equal);
    value["allIdentical"] = Value(batch.all_identical);
    value["replayedHash"] = jstr(batch.hashes.empty() ? std::string() : [&] {
      char buffer[17];
      std::snprintf(buffer, sizeof(buffer), "%016llx",
                    static_cast<unsigned long long>(batch.hashes.back()));
      return std::string(buffer);
    }());
    value["replayedEvents"] =
        number(record ? static_cast<std::uint64_t>(record->events.size()) : 0);
    Value hashes = Value::array();
    for (std::uint64_t hash : batch.hashes) {
      char buffer[17];
      std::snprintf(buffer, sizeof(buffer), "%016llx",
                    static_cast<unsigned long long>(hash));
      hashes.push(jstr(buffer));
    }
    value["hashes"] = std::move(hashes);
    value["elapsedNs"] = number(batch.elapsed_ns);
    value["failedRun"] = Value(batch.first_failed_run);
    value["divergence"] = Value();
  } else {
    const ReplayReport& report = outcome.single;
    value["identical"] = Value(report.identical);
    value["allHashesEqual"] = Value(report.identical);
    value["allIdentical"] = Value(report.identical);
    value["replayedHash"] = jstr([&] {
      char buffer[17];
      std::snprintf(buffer, sizeof(buffer), "%016llx",
                    static_cast<unsigned long long>(report.replayed_hash));
      return std::string(buffer);
    }());
    value["replayedEvents"] = number(static_cast<std::uint64_t>(report.replayed_events));
    value["elapsedNs"] = number(outcome.elapsed_ns);
    value["summary"] = summary_json(report.summary);
    Value divergence = Value::object();
    divergence["found"] = Value(report.first_divergence.found);
    if (report.first_divergence.found) {
      divergence["index"] = number(static_cast<std::uint64_t>(report.first_divergence.index));
      divergence["reason"] = jstr(report.first_divergence.reason);
      divergence["expectedSummary"] = jstr(report.first_divergence.expected_summary);
      divergence["actualSummary"] = jstr(report.first_divergence.actual_summary);
      divergence["hasExpected"] = Value(report.first_divergence.has_expected);
      divergence["hasActual"] = Value(report.first_divergence.has_actual);
    }
    value["divergence"] = std::move(divergence);
  }
  value["options"] = [&] {
    Value opts = Value::object();
    if (options.mutate_step != kNoMutation) {
      opts["mutateStep"] = number(options.mutate_step);
      opts["mutateChoice"] = number(static_cast<std::uint64_t>(options.mutate_choice));
    }
    return opts;
  }();
  return value;
}

HttpResponse Workbench::handle_api(const HttpRequest& request) {
  if (!initialized_) {
    return error_response(503, "workbench is not initialized");
  }
  std::string_view path = request.path;
  if (path == "/api" || path == "/api/") {
    if (request.method != "GET") {
      return error_response(405, "method not allowed");
    }
    return json_response(overview_json());
  }
  if (path.rfind("/api/", 0) != 0) {
    return error_response(404, "unknown endpoint");
  }
  const std::vector<std::string> parts = split_path(path.substr(5));

  if (parts.size() == 1 && parts[0] == "stream") {
    if (request.method != "GET") return error_response(405, "method not allowed");
    HttpResponse response;
    response.sse = true;
    response.content_type = "text/event-stream";
    response.body = ": tempo stream connected\n\n";
    return response;
  }

  if (parts.size() == 1 && parts[0] == "overview") {
    if (request.method != "GET") return error_response(405, "method not allowed");
    return json_response(overview_json());
  }

  if (parts.size() == 1 && parts[0] == "health") {
    if (request.method != "GET") return error_response(405, "method not allowed");
    Value result = Value::object();
    result["ok"] = Value(true);
    result["version"] = jstr(kVersion);
    result["initialized"] = Value(initialized_);
    result["records"] = number(static_cast<std::uint64_t>(records_.size()));
    result["memoryUsed"] = number(static_cast<std::uint64_t>(used_bytes_));
    result["memoryBudget"] = number(static_cast<std::uint64_t>(options_.max_total_bytes));
    return json_response(result);
  }

  if (parts.size() == 1 && parts[0] == "archive") {
    if (request.method != "GET") return error_response(405, "method not allowed");
    Value result = Value::object();
    result["records"] = archive_json();
    return json_response(result);
  }

  if (parts.size() == 2 && parts[0] == "records" && parts[1] == "open") {
    if (request.method != "POST") return error_response(405, "method not allowed");
    const json::ParseOutcome body = json::parse(request.body);
    if (!body.ok || !body.value.is_object()) {
      return error_response(400, "invalid JSON body");
    }
    const Value* file = body.value.find("file");
    if (file == nullptr || !file->is_string()) {
      return error_response(400, "file is required");
    }
    RecordOutcome outcome = open_archived(file->as_string());
    Value result = Value::object();
    result["ok"] = Value(outcome.ok);
    if (!outcome.error.empty()) result["error"] = jstr(outcome.error);
    if (outcome.record) {
      for (const auto& stored : records_) {
        if (stored.id == outcome.id) {
          result["record"] = record_json(stored);
          break;
        }
      }
    }
    return json_response(result, outcome.ok ? 200 : 400);
  }

  if (parts.size() == 2 && parts[0] == "records" && parts[1] == "import") {
    if (request.method != "POST") return error_response(405, "method not allowed");
    if (request.body.size() > limits::kMaxHttpBodyBytes) {
      return error_response(413, "imported record exceeds the 256 KiB HTTP body limit");
    }
    ImportOutcome outcome = import_record(request.body);
    Value result = Value::object();
    result["ok"] = Value(outcome.ok);
    result["duplicate"] = Value(outcome.duplicate);
    if (outcome.record) {
      for (const auto& stored : records_) {
        if (stored.id == outcome.id) {
          result["record"] = record_json(stored);
          break;
        }
      }
    }
    if (!outcome.error.empty()) result["error"] = jstr(outcome.error);
    return json_response(result, outcome.ok ? 200 : 400);
  }

  if (parts.size() == 1 && parts[0] == "record") {
    if (request.method != "POST") return error_response(405, "method not allowed");
    const json::ParseOutcome body = json::parse(request.body);
    if (!body.ok || !body.value.is_object()) {
      return error_response(400, "invalid JSON body");
    }
    const Value* scenario_value = body.value.find("scenario");
    if (scenario_value == nullptr || !scenario_value->is_string()) {
      return error_response(400, "scenario is required");
    }
    const std::string scenario = scenario_value->as_string();
    if (find_scenario(scenario) == nullptr) {
      return error_response(400, "unknown scenario: " + scenario);
    }
    std::uint64_t seed = 7;
    if (const Value* seed_value = body.value.find("seed"); seed_value != nullptr) {
      if (!read_json_uint(seed_value, seed)) {
        return error_response(400, "seed must be a non-negative integer or decimal string");
      }
    }
    std::uint64_t mutate_step = kNoMutation;
    std::uint32_t mutate_choice = 0;
    if (const Value* mutate = body.value.find("mutateStep"); mutate != nullptr) {
      if (!read_json_uint(mutate, mutate_step)) {
        return error_response(400, "mutateStep must be a non-negative integer or string");
      }
      if (const Value* choice = body.value.find("mutateChoice"); choice != nullptr) {
        std::uint64_t parsed_choice = 0;
        if (!read_json_uint(choice, parsed_choice) || parsed_choice > UINT32_MAX) {
          return error_response(400, "mutateChoice must be a 32-bit non-negative integer");
        }
        mutate_choice = static_cast<std::uint32_t>(parsed_choice);
      }
    }
    RecordOutcome outcome = record_scenario(scenario, seed, mutate_step, mutate_choice);
    Value result = Value::object();
    result["ok"] = Value(outcome.ok);
    result["runOk"] = Value(outcome.summary.ok);
    result["summary"] = summary_json(outcome.summary);
    if (!outcome.error.empty()) result["error"] = jstr(outcome.error);
    if (!outcome.warning.empty()) result["warning"] = jstr(outcome.warning);
    if (outcome.record) {
      const StoredRecord* stored = nullptr;
      for (const auto& candidate : records_) {
        if (candidate.id == outcome.id) {
          stored = &candidate;
          break;
        }
      }
      if (stored != nullptr) {
        Value record = record_json(*stored);
        record["summary"] = summary_json(outcome.summary);
        result["record"] = std::move(record);
      }
    }
    return json_response(result, outcome.ok ? 200 : 400);
  }

  if (parts.size() == 1 && parts[0] == "replay") {
    if (request.method != "POST") return error_response(405, "method not allowed");
    const json::ParseOutcome body = json::parse(request.body);
    if (!body.ok || !body.value.is_object()) {
      return error_response(400, "invalid JSON body");
    }
    const Value* id_value = body.value.find("recordId");
    const std::int64_t id_number = id_value ? id_value->as_i64(-1) : -1;
    if (id_number <= 0) return error_response(400, "recordId is required");
    std::uint32_t runs = 1;
    if (const Value* runs_value = body.value.find("runs"); runs_value != nullptr) {
      const std::int64_t parsed = runs_value->as_i64(-1);
      if (!runs_value->is_number() || parsed < 1 || parsed > 1000) {
        return error_response(400, "runs must be between 1 and 1000");
      }
      runs = static_cast<std::uint32_t>(parsed);
    }
    ReplayOptions options;
    if (const Value* mutate = body.value.find("mutateStep"); mutate != nullptr) {
      const std::int64_t parsed = mutate->as_i64(-1);
      if (!mutate->is_number() || parsed < 0) {
        return error_response(400, "mutateStep must be a non-negative integer");
      }
      options.mutate_step = static_cast<std::uint64_t>(parsed);
      if (const Value* choice = body.value.find("mutateChoice"); choice != nullptr) {
        const std::int64_t parsed_choice = choice->as_i64(-1);
        if (!choice->is_number() || parsed_choice < 0) {
          return error_response(400, "mutateChoice must be a non-negative integer");
        }
        options.mutate_choice = static_cast<std::uint32_t>(parsed_choice);
      }
    }
    const Value* async_value = body.value.find("async");
    const bool async = async_value != nullptr && async_value->is_bool() && async_value->as_bool();
    if (async) {
      AsyncJobOutcome queued = enqueue_replay(static_cast<RecordId>(id_number), runs, options);
      if (!queued.ok) {
        return error_response(400, queued.error);
      }
      Value result = Value::object();
      result["ok"] = Value(true);
      result["jobId"] = number(queued.job_id);
      result["status"] = jstr("queued");
      return json_response(result, 202);
    }
    ReplayOutcome outcome = replay(static_cast<RecordId>(id_number), runs, options);
    std::shared_ptr<const Record> record = get_record(static_cast<RecordId>(id_number));
    Value result = replay_json(static_cast<RecordId>(id_number), record, outcome, runs, options);
    return json_response(result, outcome.ok ? 200 : 400);
  }

  if (!parts.empty() && parts[0] == "breakpoints") {
    if (parts.size() == 1) {
      if (request.method == "GET") {
        Value result = Value::object();
        result["breakpoints"] = breakpoints_json();
        return json_response(result);
      }
      if (request.method == "POST") {
        const json::ParseOutcome body = json::parse(request.body);
        if (!body.ok || !body.value.is_object()) {
          return error_response(400, "invalid JSON body");
        }
        Breakpoint breakpoint;
        bool any = false;
        if (const Value* type = body.value.find("type"); type != nullptr) {
          if (!type->is_string()) return error_response(400, "type must be a string");
          EventType parsed_type = EventType::Unknown;
          if (!event_type_from_name(type->as_string(), parsed_type)) {
            return error_response(400, "unknown event type");
          }
          breakpoint.has_type = true;
          breakpoint.type = parsed_type;
          any = true;
        }
        if (const Value* task = body.value.find("task"); task != nullptr) {
          const std::int64_t parsed = task->as_i64(-1);
          if (!task->is_number() || parsed < 0 || parsed > UINT32_MAX) {
            return error_response(400, "task must be a non-negative integer");
          }
          breakpoint.has_task = true;
          breakpoint.task = static_cast<TaskId>(parsed);
          any = true;
        }
        if (const Value* text = body.value.find("text"); text != nullptr) {
          if (!text->is_string() || text->as_string().empty() ||
              text->as_string().size() > 128) {
            return error_response(400, "text must be 1..128 bytes");
          }
          breakpoint.has_text = true;
          breakpoint.text = text->as_string();
          any = true;
        }
        if (!any) return error_response(400, "at least one criterion is required");
        const auto id = add_breakpoint(breakpoint);
        if (!id.has_value()) return error_response(400, "breakpoint limit reached");
        Value result = Value::object();
        result["id"] = number(*id);
        result["breakpoints"] = breakpoints_json();
        return json_response(result);
      }
      return error_response(405, "method not allowed");
    }
    if (parts.size() == 2 && request.method == "POST") {
      if (parts[1] == "clear") {
        clear_breakpoints();
        return json_response(breakpoints_json());
      }
      const json::ParseOutcome body = json::parse(request.body);
      if (!body.ok || !body.value.is_object()) {
        return error_response(400, "invalid JSON body");
      }
      const Value* id_value = body.value.find("id");
      const std::int64_t id = id_value ? id_value->as_i64(-1) : -1;
      if (id <= 0) return error_response(400, "id is required");
      if (parts[1] == "toggle") {
        const Value* enabled = body.value.find("enabled");
        if (enabled == nullptr || !enabled->is_bool()) {
          return error_response(400, "enabled must be a boolean");
        }
        if (!toggle_breakpoint(static_cast<std::uint32_t>(id), enabled->as_bool())) {
          return error_response(404, "breakpoint not found");
        }
        return json_response(breakpoints_json());
      }
      if (parts[1] == "remove") {
        if (!remove_breakpoint(static_cast<std::uint32_t>(id))) {
          return error_response(404, "breakpoint not found");
        }
        return json_response(breakpoints_json());
      }
    }
    return error_response(404, "unknown breakpoint endpoint");
  }

  if (!parts.empty() && parts[0] == "jobs") {
    if (request.method != "GET") return error_response(405, "method not allowed");
    if (parts.size() == 1) {
      return json_response(jobs_json());
    }
    if (parts.size() == 2) {
      std::uint64_t job_id = 0;
      if (!parse_u64(parts[1], job_id) || job_id == 0) {
        return error_response(400, "invalid job id");
      }
      std::optional<json::Value> status = job_status(job_id);
      if (!status.has_value()) return error_response(404, "job not found");
      return json_response(std::move(*status));
    }
    return error_response(404, "unknown job endpoint");
  }

  if (parts.size() >= 2 && parts[0] == "records") {
    std::uint64_t id_number = 0;
    if (!parse_u64(parts[1], id_number) || id_number == 0) {
      return error_response(400, "invalid record id");
    }
    const RecordId id = static_cast<RecordId>(id_number);
    std::shared_ptr<const Record> record = get_record(id);
    if (!record) return error_response(404, "record not found");
    DebugSession* session = session_for(id);
    if (session == nullptr) return error_response(500, "debug session unavailable");

    if (parts.size() == 3 && parts[2] == "events") {
      if (request.method != "GET") return error_response(405, "method not allowed");
      const auto query = parse_query(request.query);
      std::uint64_t from = 0;
      std::uint64_t limit = 200;
      if (auto it = query.find("from"); it != query.end() && !parse_u64(it->second, from)) {
        return error_response(400, "invalid from");
      }
      if (auto it = query.find("limit"); it != query.end() && !parse_u64(it->second, limit)) {
        return error_response(400, "invalid limit");
      }
      if (limit == 0) limit = 1;
      limit = std::min<std::uint64_t>(limit, limits::kMaxEventPage);
      bool has_type = false;
      EventType filter_type = EventType::Unknown;
      bool has_task = false;
      TaskId filter_task = kNoTask;
      if (auto it = query.find("type"); it != query.end() && !it->second.empty()) {
        if (!event_type_from_name(it->second, filter_type)) {
          return error_response(400, "unknown event type filter");
        }
        has_type = true;
      }
      if (auto it = query.find("task"); it != query.end() && !it->second.empty()) {
        std::uint64_t task_value = 0;
        if (!parse_u64(it->second, task_value) || task_value > UINT32_MAX) {
          return error_response(400, "invalid task filter");
        }
        filter_task = static_cast<TaskId>(task_value);
        has_task = true;
      }
      const bool filtered = has_type || has_task;
      const std::size_t total = record->events.size();
      std::size_t matched = 0;
      std::size_t skipped = 0;
      Value events = Value::array();
      for (std::size_t i = 0; i < total; ++i) {
        const Event& event = record->events[i];
        if (has_type && event.type != filter_type) continue;
        if (has_task && event.task != filter_task) continue;
        ++matched;
        if (filtered) {
          if (skipped < from) {
            ++skipped;
            continue;
          }
        } else if (i < from) {
          continue;
        }
        if (events.items().size() < limit) {
          events.push(event_json(i, event));
        }
      }
      if (!filtered && from > total) from = total;
      Value result = Value::object();
      result["total"] = number(static_cast<std::uint64_t>(filtered ? matched : total));
      result["from"] = number(from);
      result["filtered"] = Value(filtered);
      result["events"] = std::move(events);
      return json_response(result);
    }

    if (parts.size() == 3 && parts[2] == "delete") {
      if (request.method != "POST") return error_response(405, "method not allowed");
      bool delete_file = true;
      if (!request.body.empty()) {
        const json::ParseOutcome body = json::parse(request.body);
        if (body.ok && body.value.is_object()) {
          if (const Value* flag = body.value.find("deleteFile"); flag != nullptr) {
            if (!flag->is_bool()) return error_response(400, "deleteFile must be a boolean");
            delete_file = flag->as_bool();
          }
        }
      }
      std::string delete_error;
      if (!delete_record(id, delete_file, &delete_error)) {
        return error_response(delete_error == "record not found" ? 404 : 500, delete_error);
      }
      Value result = Value::object();
      result["ok"] = Value(true);
      result["deleted"] = number(id);
      result["fileDeleted"] = Value(delete_file);
      return json_response(result);
    }

    if (parts.size() == 3 && parts[2] == "download") {
      if (request.method != "GET") return error_response(405, "method not allowed");
      const SerializeOutcome serialized_record = serialize_record(*record);
      if (!serialized_record.ok) {
        return error_response(500, "record could not be serialized: " + serialized_record.error);
      }
      if (serialized_record.bytes.size() > limits::kMaxHttpResponseBytes) {
        return error_response(413, "record is too large to download through the UI");
      }
      HttpResponse response;
      response.status = 200;
      response.content_type = "application/octet-stream";
      response.body = serialized_record.bytes;
      response.extra_headers.emplace_back(
          "Content-Disposition",
          "attachment; filename=\"rec-" + record->hash_hex().substr(0, 8) + ".tmpo\"");
      return response;
    }

    if (parts.size() == 3 && parts[2] == "frame") {
      if (request.method != "GET") return error_response(405, "method not allowed");
      if (record->events.empty()) return error_response(400, "record has no events");
      const auto query = parse_query(request.query);
      std::uint64_t index_value = 0;
      if (auto it = query.find("index"); it != query.end() &&
          !parse_u64(it->second, index_value)) {
        return error_response(400, "invalid index");
      }
      const std::size_t total = record->events.size();
      if (index_value >= total) index_value = total - 1;
      const DebugFrame frame = session->frame_at(static_cast<std::size_t>(index_value));
      Value result = Value::object();
      result["index"] = number(index_value);
      result["total"] = number(static_cast<std::uint64_t>(total));
      result["event"] = event_json(static_cast<std::size_t>(index_value), frame.event);
      result["state"] = state_json(frame.state);
      result["isDivergence"] = Value(frame.is_divergence);
      if (session->divergence_index().has_value()) {
        result["divergenceIndex"] =
            number(static_cast<std::uint64_t>(*session->divergence_index()));
      }
      result["breakpointHit"] = Value(session->matches(frame.event));
      return json_response(result);
    }

    if (parts.size() == 3 && parts[2] == "timeline") {
      if (request.method != "GET") return error_response(405, "method not allowed");
      const std::vector<TaskLane> lanes = session->timeline();
      Value result = Value::object();
      Value lanes_json = Value::array();
      const std::size_t per_lane_budget =
          lanes.empty() ? 0 : std::max<std::size_t>(1, 20'000 / lanes.size());
      bool truncated = false;
      for (const auto& lane : lanes) {
        Value lane_json = Value::object();
        lane_json["task"] = number(static_cast<std::uint64_t>(lane.task));
        lane_json["name"] = jstr(lane.name);
        lane_json["count"] = number(static_cast<std::uint64_t>(lane.event_count));
        lane_json["spawnNs"] = number(lane.spawn_ns);
        lane_json["endNs"] = number(lane.end_ns);
        lane_json["exited"] = Value(lane.exited);
        lane_json["failed"] = Value(lane.failed);
        const std::size_t budget = std::min(lane.event_count, per_lane_budget);
        Value events = Value::array();
        if (lane.events.size() > budget && budget > 0) {
          truncated = true;
          const std::size_t stride = (lane.events.size() + budget - 1) / budget;
          for (std::size_t k = 0; k < lane.events.size(); k += stride) {
            const std::size_t event_index = lane.events[k];
            const Event& event = record->events[event_index];
            Value item = Value::object();
            item["i"] = number(static_cast<std::uint64_t>(event_index));
            item["type"] = jstr(event_type_name(event.type));
            item["ns"] = number(event_time(event));
            events.push(std::move(item));
          }
          lane_json["truncated"] = Value(true);
        } else {
          for (std::size_t event_index : lane.events) {
            const Event& event = record->events[event_index];
            Value item = Value::object();
            item["i"] = number(static_cast<std::uint64_t>(event_index));
            item["type"] = jstr(event_type_name(event.type));
            item["ns"] = number(event_time(event));
            events.push(std::move(item));
          }
          lane_json["truncated"] = Value(false);
        }
        lane_json["events"] = std::move(events);
        lanes_json.push(std::move(lane_json));
      }
      result["lanes"] = std::move(lanes_json);
      result["truncated"] = Value(truncated);
      return json_response(result);
    }

    if (parts.size() == 3 && parts[2] == "diff") {
      if (request.method != "GET") return error_response(405, "method not allowed");
      const auto query = parse_query(request.query);
      auto it = query.find("other");
      std::uint64_t other_id = 0;
      if (it == query.end() || !parse_u64(it->second, other_id) || other_id == 0) {
        return error_response(400, "other record id is required");
      }
      std::shared_ptr<const Record> other = get_record(static_cast<RecordId>(other_id));
      if (!other) return error_response(404, "other record not found");
      const RecordDiff diff = diff_records(*record, *other);
      Value result = Value::object();
      result["equal"] = Value(diff.equal);
      result["aEvents"] = number(static_cast<std::uint64_t>(diff.a_events));
      result["bEvents"] = number(static_cast<std::uint64_t>(diff.b_events));
      result["aHash"] = jstr(record->hash_hex());
      result["bHash"] = jstr(other->hash_hex());
      result["aScenario"] = jstr(diff.a_scenario);
      result["bScenario"] = jstr(diff.b_scenario);
      Value first = Value::object();
      first["found"] = Value(diff.first.found);
      if (diff.first.found) {
        first["index"] = number(static_cast<std::uint64_t>(diff.first.index));
        first["reason"] = jstr(diff.first.has_a && diff.first.has_b
                                   ? "event mismatch"
                                   : (diff.first.has_a ? "record B ended" : "record A ended"));
        first["summaryA"] = jstr(diff.first.summary_a);
        first["summaryB"] = jstr(diff.first.summary_b);
      }
      result["first"] = std::move(first);
      Value counts = Value::array();
      for (const auto& count : diff.counts) {
        Value item = Value::object();
        item["type"] = jstr(event_type_name(count.type));
        item["a"] = number(static_cast<std::uint64_t>(count.in_a));
        item["b"] = number(static_cast<std::uint64_t>(count.in_b));
        counts.push(std::move(item));
      }
      result["counts"] = std::move(counts);
      return json_response(result);
    }

    if (parts.size() == 4 && parts[2] == "breakpoints" && parts[3] == "next") {
      if (request.method != "GET") return error_response(405, "method not allowed");
      const auto query = parse_query(request.query);
      std::uint64_t from = 0;
      if (auto it = query.find("from"); it != query.end() && !parse_u64(it->second, from)) {
        return error_response(400, "invalid from");
      }
      bool forward = true;
      if (auto it = query.find("direction"); it != query.end()) {
        forward = it->second != "-1";
      }
      const auto hit = session->next_breakpoint(static_cast<std::size_t>(from), forward);
      Value result = Value::object();
      result["index"] = hit.has_value()
                            ? number(static_cast<std::uint64_t>(*hit))
                            : Value();
      return json_response(result);
    }
    return error_response(404, "unknown record endpoint");
  }
  return error_response(404, "unknown endpoint");
}

Workbench::RecordOutcome Workbench::record_scenario(std::string_view scenario_name,
                                                     std::uint64_t seed,
                                                     std::uint64_t mutate_step,
                                                     std::uint32_t mutate_choice) {
  RecordOutcome outcome;
  if (!initialized_) {
    outcome.error = "workbench is not initialized";
    return outcome;
  }
  const ScenarioInfo* scenario = find_scenario(scenario_name);
  if (scenario == nullptr) {
    outcome.error = "unknown scenario: " + std::string(scenario_name);
    return outcome;
  }
  std::string fixture_error;
  if (!prepare_fixtures(*scenario, &fixture_error)) {
    outcome.error = fixture_error;
    return outcome;
  }
  SystemCapabilityOptions capability_options;
  capability_options.data_root = options_.data_dir;
  SystemHost host(capability_options);
  RecordRunOptions record_options;
  record_options.seed = seed;
  record_options.mutate_step = mutate_step;
  record_options.mutate_choice = mutate_choice;
  record_options.build_id = scenario->build_id;
  RecordRunResult run = record_run(scenario->name, scenario->entry, host, record_options);
  outcome.summary = run.summary;
  if (!run.error.empty()) {
    outcome.error = run.error;
    return outcome;
  }
  outcome.run_ok = run.summary.ok;
  RecordId id = add_record(std::move(run.record), run.summary);
  if (id == 0) {
    outcome.error = "record exceeds the workbench memory budget";
    return outcome;
  }
  outcome.ok = true;
  outcome.id = id;
  for (const auto& stored : records_) {
    if (stored.id == id) {
      outcome.record = stored.record;
      outcome.warning = stored.saved ? std::string() : std::string("record was not saved to disk");
      break;
    }
  }
  broadcast_overview();
  return outcome;
}

RecordId Workbench::add_record(Record record, RunSummary summary) {
  auto shared = std::make_shared<Record>(std::move(record));
  if (shared->meta.created_unix == 0) {
    shared->meta.created_unix = static_cast<std::uint64_t>(std::time(nullptr));
  }
  const std::size_t estimate = shared->approximate_bytes();
  if (estimate > options_.max_total_bytes) {
    load_warnings_.push_back("record rejected: " + std::to_string(estimate) +
                             " bytes exceeds the " +
                             std::to_string(options_.max_total_bytes) + " byte budget");
    return 0;
  }
  while (!records_.empty() &&
         (records_.size() >= options_.max_records ||
          used_bytes_ + estimate > options_.max_total_bytes)) {
    const std::string evicted_file = records_.front().file;
    if (!evicted_file.empty()) {
      const std::string name = std::filesystem::path(evicted_file).filename().string();
      if (std::find(archive_files_.begin(), archive_files_.end(), name) ==
          archive_files_.end()) {
        archive_files_.push_back(name);
      }
    }
    sessions_.erase(records_.front().id);
    used_bytes_ -= records_.front().memory_bytes;
    records_.erase(records_.begin());
  }
  if (used_bytes_ + estimate > options_.max_total_bytes) {
    return 0;
  }
  StoredRecord stored;
  stored.id = next_record_id_++;
  stored.record = shared;
  stored.summary = std::move(summary);
  stored.created_unix = shared->meta.created_unix;
  stored.memory_bytes = estimate;
  std::string warning;
  stored.saved = save_record_file(stored, &warning);
  if (!warning.empty()) {
    load_warnings_.push_back(warning);
  }
  used_bytes_ += estimate;
  records_.push_back(stored);
  last_record_ = stored.id;
  (void)session_for(stored.id);
  return stored.id;
}

bool Workbench::save_record_file(StoredRecord& stored, std::string* warning) {
  if (!stored.record) {
    if (warning) *warning = "cannot save a null record";
    return false;
  }
  const Record& record = *stored.record;
  const std::filesystem::path records_dir =
      std::filesystem::path(options_.data_dir) / "records";
  std::error_code ec;
  std::filesystem::create_directories(records_dir, ec);
  if (ec) {
    if (warning) *warning = "cannot create records directory: " + ec.message();
    return false;
  }
  char created[24];
  std::snprintf(created, sizeof(created), "%010llu",
                static_cast<unsigned long long>(stored.created_unix));
  const std::string filename = std::string("rec-") + created + "-" +
                               record.hash_hex() + "-" +
                               slugify(record.meta.scenario) + ".tmpo";
  const std::filesystem::path path = records_dir / filename;
  if (std::filesystem::exists(path, ec)) {
    stored.file = path.string();
    return true;
  }
  std::string error;
  if (!write_record_file(record, path.string(), &error)) {
    if (warning) *warning = "save failed: " + error;
    return false;
  }
  stored.file = path.string();
  return true;
}

bool Workbench::prepare_fixtures(const ScenarioInfo& scenario, std::string* error) {
  for (const auto& [relative, content] : scenario.fixtures) {
    if (!safe_relative_path_policy(relative)) {
      if (error) *error = "unsafe fixture path";
      return false;
    }
    if (content.size() > limits::kMaxFileBytes) {
      if (error) *error = "fixture exceeds 65536 bytes";
      return false;
    }
    const std::filesystem::path full =
        std::filesystem::path(options_.data_dir) / relative;
    std::error_code ec;
    std::filesystem::create_directories(full.parent_path(), ec);
    if (ec) {
      if (error) *error = "cannot create fixture directory: " + ec.message();
      return false;
    }
    tighten_directory(full.parent_path());
    std::ofstream out(full, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (error) *error = "cannot write fixture: " + relative;
      return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) {
      if (error) *error = "fixture write failed: " + relative;
      return false;
    }
    out.close();
    tighten_file(full);
  }
  return true;
}

namespace {
Workbench::ReplayOutcome execute_replay(const std::shared_ptr<const Record>& record,
                                        const ScenarioInfo& scenario, std::uint32_t runs,
                                        ReplayOptions options) {
  Workbench::ReplayOutcome outcome;
  if (!record) {
    outcome.error = "record not found";
    return outcome;
  }
  if (!record->complete) {
    outcome.error = "cannot replay an incomplete record";
    return outcome;
  }
  if (options.expected_build_id.empty()) {
    options.expected_build_id = scenario.build_id;
  }
  const std::uint32_t capped = std::clamp<std::uint32_t>(runs, 1, limits::kMaxReplayRuns);
  const auto start = std::chrono::steady_clock::now();
  if (capped > 1) {
    outcome.batch = true;
    outcome.multi = replay_many(*record, scenario.entry, capped, options);
    outcome.ok = outcome.multi.ok;
    outcome.error = outcome.multi.error;
  } else {
    outcome.single = replay_record(*record, scenario.entry, options);
    outcome.ok = outcome.single.ok;
    outcome.error = outcome.single.error;
  }
  const auto end = std::chrono::steady_clock::now();
  outcome.elapsed_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
  return outcome;
}
}  // namespace

Workbench::ReplayOutcome Workbench::replay(RecordId id, std::uint32_t runs,
                                           ReplayOptions options) {
  ReplayOutcome outcome;
  std::shared_ptr<const Record> record = get_record(id);
  if (!record) {
    outcome.error = "record not found";
    return outcome;
  }
  const ScenarioInfo* scenario = find_scenario(record->meta.scenario);
  if (scenario == nullptr) {
    outcome.error = "scenario is not registered: " + record->meta.scenario;
    return outcome;
  }
  if (options.expected_build_id.empty()) {
    options.expected_build_id = scenario->build_id;
  }
  const std::uint32_t capped = std::clamp<std::uint32_t>(runs, 1, limits::kMaxReplayRuns);
  outcome = execute_replay(record, *scenario, capped, options);
  if (!outcome.batch) {
    if (DebugSession* session = session_for(id, false); session != nullptr) {
      if (outcome.single.first_divergence.found) {
        session->set_divergence(outcome.single.first_divergence.index,
                                outcome.single.first_divergence.reason);
      } else if (outcome.single.identical) {
        session->set_divergence(std::nullopt, std::string());
      }
    }
  }
  json::Value payload = replay_json(id, record, outcome, capped, options);
  if (broadcaster_) {
    broadcaster_("replay", payload.dump());
  }
  return outcome;
}

void Workbench::start_worker_locked() {
  if (!worker_.joinable()) {
    worker_ = std::thread([this] { worker_loop(); });
  }
}

void Workbench::worker_loop() {
  for (;;) {
    std::uint64_t job_id = 0;
    ReplayJob job;
    {
      std::unique_lock<std::mutex> lock(jobs_mutex_);
      jobs_cv_.wait(lock, [this] { return worker_stop_ || !job_queue_.empty(); });
      if (worker_stop_ && job_queue_.empty()) {
        return;
      }
      job_id = job_queue_.front();
      job_queue_.pop_front();
      auto it = jobs_.find(job_id);
      if (it == jobs_.end()) {
        continue;
      }
      it->second.status = "running";
      job = it->second;
    }
    const ScenarioInfo* scenario = find_scenario(job.record ? job.record->meta.scenario
                                                            : std::string());
    ReplayOutcome outcome;
    if (scenario == nullptr) {
      outcome.error = "scenario is not registered";
    } else {
      outcome = execute_replay(job.record, *scenario, job.runs, job.options);
    }
    {
      std::lock_guard<std::mutex> lock(jobs_mutex_);
      auto it = jobs_.find(job_id);
      if (it != jobs_.end()) {
        it->second.outcome = std::move(outcome);
        it->second.status = it->second.outcome.ok ? "done" : "error";
        if (!it->second.outcome.ok) {
          it->second.error = it->second.outcome.error.empty()
                                 ? std::string("replay failed")
                                 : it->second.outcome.error;
        }
      }
    }
  }
}

Workbench::AsyncJobOutcome Workbench::enqueue_replay(RecordId id, std::uint32_t runs,
                                                     ReplayOptions options) {
  AsyncJobOutcome result;
  std::shared_ptr<const Record> record = get_record(id);
  if (!record) {
    result.error = "record not found";
    return result;
  }
  if (!record->complete) {
    result.error = "cannot replay an incomplete record";
    return result;
  }
  const ScenarioInfo* scenario = find_scenario(record->meta.scenario);
  if (scenario == nullptr) {
    result.error = "scenario is not registered: " + record->meta.scenario;
    return result;
  }
  if (options.expected_build_id.empty()) {
    options.expected_build_id = scenario->build_id;
  }
  std::lock_guard<std::mutex> lock(jobs_mutex_);
  if (jobs_.size() >= 16) {
    for (auto it = jobs_.begin(); it != jobs_.end();) {
      if (it->second.status == "done" || it->second.status == "error") {
        it = jobs_.erase(it);
      } else {
        ++it;
      }
    }
  }
  if (jobs_.size() + job_queue_.size() >= 16) {
    result.error = "replay job queue is full";
    return result;
  }
  ReplayJob job;
  const std::uint64_t job_id = next_job_id_++;
  job.id = job_id;
  job.status = "queued";
  job.record = record;
  job.options = options;
  job.runs = std::clamp<std::uint32_t>(runs, 1, limits::kMaxReplayRuns);
  jobs_.emplace(job_id, std::move(job));
  job_queue_.push_back(job_id);
  start_worker_locked();
  jobs_cv_.notify_all();
  result.ok = true;
  result.job_id = job_id;
  return result;
}

std::optional<json::Value> Workbench::job_status(std::uint64_t job_id) {
  std::lock_guard<std::mutex> lock(jobs_mutex_);
  auto it = jobs_.find(job_id);
  if (it == jobs_.end()) {
    return std::nullopt;
  }
  const ReplayJob& job = it->second;
  Value value = Value::object();
  value["id"] = number(job_id);
  value["status"] = jstr(job.status);
  value["runs"] = number(job.runs);
  if (job.status == "done" || job.status == "error") {
    value["result"] = replay_json(job.id, job.record, job.outcome, job.runs, job.options);
    if (!job.error.empty()) {
      value["error"] = jstr(job.error);
    }
  }
  return value;
}

json::Value Workbench::jobs_json() {
  std::lock_guard<std::mutex> lock(jobs_mutex_);
  Value value = Value::array();
  for (const auto& [id, job] : jobs_) {
    Value item = Value::object();
    item["id"] = number(id);
    item["status"] = jstr(job.status);
    item["runs"] = number(job.runs);
    value.push(std::move(item));
  }
  return value;
}

Workbench::RecordOutcome Workbench::open_archived(std::string_view filename) {
  RecordOutcome outcome;
  if (!initialized_) {
    outcome.error = "workbench is not initialized";
    return outcome;
  }
  const std::string name(filename);
  if (name.empty() || name.size() > 255 || name.find('/') != std::string::npos ||
      name.find('\\') != std::string::npos || name.find("..") != std::string::npos ||
      name.find('\0') != std::string::npos ||
      std::filesystem::path(name).extension() != ".tmpo") {
    outcome.error = "invalid archive filename";
    return outcome;
  }
  const std::filesystem::path path =
      std::filesystem::path(options_.data_dir) / "records" / name;
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec) {
    outcome.error = "archive file not found";
    return outcome;
  }
  const LoadOutcome loaded = load_record_file(path.string(), LoadMode::Recover);
  if (!loaded.ok) {
    outcome.error = "invalid archive record: " + loaded.error;
    return outcome;
  }
  for (const auto& stored : records_) {
    if (stored.record && stored.record->hash == loaded.record.hash) {
      archive_files_.erase(std::remove(archive_files_.begin(), archive_files_.end(), name),
                           archive_files_.end());
      outcome.ok = true;
      outcome.id = stored.id;
      outcome.record = stored.record;
      outcome.summary.ok = loaded.record.complete;
      return outcome;
    }
  }
  const RecordId id = add_record(loaded.record, RunSummary{});
  if (id == 0) {
    outcome.error = "record exceeds the workbench memory budget";
    return outcome;
  }
  archive_files_.erase(std::remove(archive_files_.begin(), archive_files_.end(), name),
                       archive_files_.end());
  outcome.ok = true;
  outcome.id = id;
  outcome.record = get_record(id);
  broadcast_overview();
  return outcome;
}

std::shared_ptr<const Record> Workbench::get_record(RecordId id) const {
  for (const auto& stored : records_) {
    if (stored.id == id) {
      return stored.record;
    }
  }
  return nullptr;
}

std::vector<StoredRecord> Workbench::records() const {
  return records_;
}

Workbench::ImportOutcome Workbench::import_record(std::string_view bytes) {
  ImportOutcome outcome;
  if (!initialized_) {
    outcome.error = "workbench is not initialized";
    return outcome;
  }
  if (bytes.empty()) {
    outcome.error = "empty record";
    return outcome;
  }
  LoadOutcome loaded = load_record_bytes(bytes, LoadMode::Strict);
  if (!loaded.ok) {
    outcome.error = "invalid record: " + loaded.error;
    return outcome;
  }
  if (loaded.record.meta.scenario.empty()) {
    outcome.error = "record has no scenario metadata";
    return outcome;
  }
  for (const auto& stored : records_) {
    if (stored.record && stored.record->hash == loaded.record.hash) {
      outcome.ok = true;
      outcome.duplicate = true;
      outcome.id = stored.id;
      outcome.record = stored.record;
      return outcome;
    }
  }
  const RecordId id = add_record(std::move(loaded.record), RunSummary{});
  if (id == 0) {
    outcome.error = "record could not be stored";
    return outcome;
  }
  outcome.ok = true;
  outcome.id = id;
  outcome.record = get_record(id);
  broadcast_overview();
  return outcome;
}

bool Workbench::delete_record(RecordId id, bool delete_file, std::string* error) {
  auto it = std::find_if(records_.begin(), records_.end(),
                         [id](const StoredRecord& stored) { return stored.id == id; });
  if (it == records_.end()) {
    if (error) *error = "record not found";
    return false;
  }
  if (delete_file && !it->file.empty()) {
    std::error_code ec;
    std::filesystem::remove(it->file, ec);
    if (ec) {
      if (error) *error = "cannot delete record file: " + ec.message();
      return false;
    }
  }
  sessions_.erase(id);
  used_bytes_ -= it->memory_bytes;
  if (delete_file && !it->file.empty()) {
    const std::string name = std::filesystem::path(it->file).filename().string();
    archive_files_.erase(std::remove(archive_files_.begin(), archive_files_.end(), name),
                         archive_files_.end());
  }
  records_.erase(it);
  if (last_record_ == id) {
    last_record_ = records_.empty() ? 0 : records_.back().id;
  }
  broadcast_overview();
  return true;
}

DebugSession* Workbench::session_for(RecordId id, bool create) {
  auto it = sessions_.find(id);
  if (it != sessions_.end()) {
    return it->second.get();
  }
  if (!create) {
    return nullptr;
  }
  std::shared_ptr<const Record> record = get_record(id);
  if (!record) {
    return nullptr;
  }
  auto session = std::make_unique<DebugSession>(record);
  apply_breakpoints(*session);
  DebugSession* raw = session.get();
  sessions_.emplace(id, std::move(session));
  return raw;
}

void Workbench::apply_breakpoints(DebugSession& session) {
  session.clear_breakpoints();
  for (const auto& breakpoint : breakpoints_) {
    (void)session.add_breakpoint(breakpoint);
  }
}

std::optional<std::uint32_t> Workbench::add_breakpoint(Breakpoint breakpoint) {
  if (breakpoints_.size() >= limits::kMaxBreakpoints) {
    return std::nullopt;
  }
  if (breakpoint.id == 0) {
    breakpoint.id = next_breakpoint_id_++;
  } else if (breakpoint.id >= next_breakpoint_id_) {
    next_breakpoint_id_ = breakpoint.id + 1;
  }
  breakpoints_.push_back(breakpoint);
  for (auto& [id, session] : sessions_) {
    (void)id;
    (void)session->add_breakpoint(breakpoint);
  }
  return breakpoint.id;
}

bool Workbench::toggle_breakpoint(std::uint32_t id, bool enabled) {
  for (auto& breakpoint : breakpoints_) {
    if (breakpoint.id == id) {
      breakpoint.enabled = enabled;
      for (auto& [session_id, session] : sessions_) {
        (void)session_id;
        (void)session->set_breakpoint_enabled(id, enabled);
      }
      return true;
    }
  }
  return false;
}

bool Workbench::remove_breakpoint(std::uint32_t id) {
  const auto before = breakpoints_.size();
  breakpoints_.erase(
      std::remove_if(breakpoints_.begin(), breakpoints_.end(),
                     [id](const Breakpoint& bp) { return bp.id == id; }),
      breakpoints_.end());
  if (breakpoints_.size() == before) {
    return false;
  }
  for (auto& [session_id, session] : sessions_) {
    (void)session_id;
    (void)session->remove_breakpoint(id);
  }
  return true;
}

void Workbench::clear_breakpoints() {
  breakpoints_.clear();
  for (auto& [id, session] : sessions_) {
    (void)id;
    session->clear_breakpoints();
  }
}

void Workbench::broadcast_overview() {
  if (!broadcaster_) {
    return;
  }
  Value payload = Value::object();
  Value records = Value::array();
  for (const auto& stored : records_) {
    records.push(record_json(stored));
  }
  payload["records"] = std::move(records);
  payload["breakpoints"] = breakpoints_json();
  payload["lastRecord"] = number(last_record_);
  broadcaster_("overview", payload.dump());
}

}  // namespace tempo
