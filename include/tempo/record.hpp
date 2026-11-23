#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "tempo/event.hpp"

namespace tempo {

/// Metadata stored in every record. `attributes` preserves unknown fields so a
/// newer writer's record can still be re-saved by an older tool.
struct RecordMeta {
  std::string scenario;
  std::string tool_version;
  std::uint64_t seed = 1;
  std::uint64_t created_unix = 0;
  std::uint64_t start_ns = 1'000'000;
  std::map<std::string, std::string> attributes;

  bool operator==(const RecordMeta& other) const noexcept {
    return scenario == other.scenario && tool_version == other.tool_version &&
           seed == other.seed && created_unix == other.created_unix &&
           start_ns == other.start_ns && attributes == other.attributes;
  }
};

/// An immutable-in-practice sequence of events plus metadata.
///
/// `hash` is the FNV-1a 64 determinism hash over the canonical event encoding.
/// It is recomputed whenever events are loaded or appended by the recorder.
struct Record {
  RecordMeta meta;
  std::vector<Event> events;
  std::uint64_t hash = 0;
  bool complete = true;
  bool recovered = false;
  std::string warning;

  std::uint64_t compute_hash() const noexcept;
  std::uint64_t recompute_hash() noexcept;
  /// Conservative heap estimate for the event graph, used by the workbench to
  /// enforce a global memory budget. Counts container capacity and strings.
  std::size_t approximate_bytes() const noexcept;
  /// 16 lowercase hex characters of `hash`.
  std::string hash_hex() const;
  std::uint32_t event_count() const noexcept {
    return static_cast<std::uint32_t>(events.size());
  }
};

struct SerializeOutcome {
  bool ok = false;
  std::string bytes;
  std::string error;
};

enum class LoadMode {
  /// Require a complete, CRC-valid file with a footer.
  Strict,
  /// Parse the valid prefix of a truncated file; never invent events.
  Recover,
};

struct LoadOutcome {
  bool ok = false;
  Record record;
  std::string error;
};

/// Serialize to the portable v1 record container. Fails (never truncates) if a
/// hard limit is exceeded.
SerializeOutcome serialize_record(const Record& record);

/// Serialize and atomically write via a temporary file + rename.
bool write_record_file(const Record& record, const std::string& path, std::string* error);

/// Parse a record from memory. `bytes` is treated as untrusted input.
LoadOutcome load_record_bytes(std::string_view bytes, LoadMode mode = LoadMode::Strict);

/// Read and parse a record file. Enforces the 64 MiB file size limit.
LoadOutcome load_record_file(const std::string& path, LoadMode mode = LoadMode::Strict);

}  // namespace tempo
