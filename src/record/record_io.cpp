#include "tempo/record.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

#include "tempo/crc32.hpp"
#include "tempo/limits.hpp"
#include "tempo/string_table.hpp"
#include "tempo/version.hpp"

namespace tempo {
namespace {

constexpr std::uint32_t kMagic = 0x4F504D54u;      // "TMPO"
constexpr std::uint32_t kFooterMagic = 0x54444E45u;  // "ENDT"
constexpr std::uint16_t kFormatVersion = 1;
constexpr std::size_t kHeaderSize = 40;
constexpr std::size_t kTrailerSize = 24;  // body CRC + footer
constexpr std::uint16_t kFlagComplete = 1;

// ---------------------------------------------------------------------------
// Byte-level helpers (explicit little-endian, no packing assumptions)
// ---------------------------------------------------------------------------

struct Writer {
  std::string out;

  void u8(std::uint8_t value) { out.push_back(static_cast<char>(value)); }
  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value & 0xFFu));
    u8(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
  }
  void u32(std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
      u8(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
    }
  }
  void u64(std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
      u8(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu));
    }
  }
  void raw(std::string_view value) { out.append(value); }
};

struct Reader {
  const unsigned char* data = nullptr;
  std::size_t size = 0;
  std::size_t pos = 0;

  std::size_t remaining() const noexcept { return size - pos; }
  bool need(std::size_t count) const noexcept { return count <= remaining(); }

  bool u8(std::uint8_t& value) {
    if (!need(1)) {
      return false;
    }
    value = data[pos++];
    return true;
  }
  bool u16(std::uint16_t& value) {
    if (!need(2)) {
      return false;
    }
    value = static_cast<std::uint16_t>(data[pos] | (data[pos + 1] << 8));
    pos += 2;
    return true;
  }
  bool u32(std::uint32_t& value) {
    if (!need(4)) {
      return false;
    }
    value = 0;
    for (int i = 0; i < 4; ++i) {
      value |= static_cast<std::uint32_t>(data[pos + i]) << (8 * i);
    }
    pos += 4;
    return true;
  }
  bool u64(std::uint64_t& value) {
    if (!need(8)) {
      return false;
    }
    value = 0;
    for (int i = 0; i < 8; ++i) {
      value |= static_cast<std::uint64_t>(data[pos + i]) << (8 * i);
    }
    pos += 8;
    return true;
  }
  bool raw(std::string& value, std::size_t count) {
    if (!need(count)) {
      return false;
    }
    value.assign(reinterpret_cast<const char*>(data + pos), count);
    pos += count;
    return true;
  }
  bool take(std::size_t count, std::string_view& view) {
    if (!need(count)) {
      return false;
    }
    view = std::string_view(reinterpret_cast<const char*>(data + pos), count);
    pos += count;
    return true;
  }
};

std::optional<std::uint64_t> parse_u64(std::string_view text) {
  if (text.empty() || text.size() > 20) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (char c : text) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
    const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
    if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
      return std::nullopt;
    }
    value = value * 10 + digit;
  }
  return value;
}

std::string encode_meta(const RecordMeta& meta) {
  if (meta.scenario.size() > 4'096) {
    throw LimitError("scenario name exceeds metadata limit");
  }
  if (meta.tool_version.size() > 128) {
    throw LimitError("tool version exceeds metadata limit");
  }
  Writer writer;
  std::vector<std::pair<std::string, std::string>> fields;
  fields.emplace_back("scenario", meta.scenario);
  fields.emplace_back("tool", meta.tool_version.empty() ? std::string(tempo::kVersion)
                                                       : meta.tool_version);
  fields.emplace_back("seed", std::to_string(meta.seed));
  fields.emplace_back("created", std::to_string(meta.created_unix));
  fields.emplace_back("start_ns", std::to_string(meta.start_ns));
  for (const auto& [key, value] : meta.attributes) {
    fields.emplace_back(key, value);
  }
  if (fields.size() > 64) {
    throw LimitError("too many metadata fields");
  }
  writer.u32(static_cast<std::uint32_t>(fields.size()));
  for (const auto& [key, value] : fields) {
    if (key.size() > 64 || value.size() > 4'096) {
      throw LimitError("metadata field exceeds limit");
    }
    writer.u32(static_cast<std::uint32_t>(key.size()));
    writer.raw(key);
    writer.u32(static_cast<std::uint32_t>(value.size()));
    writer.raw(value);
  }
  if (writer.out.size() > limits::kMaxMetadataBytes) {
    throw LimitError("metadata exceeds 64 KiB limit");
  }
  return std::move(writer.out);
}

bool decode_meta(std::string_view blob, RecordMeta* meta, std::string* error) {
  Reader reader{reinterpret_cast<const unsigned char*>(blob.data()), blob.size()};
  std::uint32_t count = 0;
  if (!reader.u32(count) || count > 64) {
    *error = "malformed metadata header";
    return false;
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    std::uint32_t key_len = 0;
    if (!reader.u32(key_len) || key_len > 64) {
      *error = "malformed metadata field";
      return false;
    }
    std::string key;
    if (!reader.raw(key, key_len)) {
      *error = "truncated metadata field";
      return false;
    }
    std::uint32_t value_len = 0;
    if (!reader.u32(value_len) || value_len > 4'096) {
      *error = "malformed metadata field";
      return false;
    }
    std::string value;
    if (!reader.raw(value, value_len)) {
      *error = "truncated metadata field";
      return false;
    }
    if (key == "scenario") {
      meta->scenario = std::move(value);
    } else if (key == "tool") {
      meta->tool_version = std::move(value);
    } else if (key == "seed") {
      const auto parsed = parse_u64(value);
      if (!parsed) {
        *error = "invalid seed metadata";
        return false;
      }
      meta->seed = *parsed;
    } else if (key == "created") {
      const auto parsed = parse_u64(value);
      if (!parsed) {
        *error = "invalid created metadata";
        return false;
      }
      meta->created_unix = *parsed;
    } else if (key == "start_ns") {
      const auto parsed = parse_u64(value);
      if (!parsed || *parsed == 0) {
        *error = "invalid start_ns metadata";
        return false;
      }
      meta->start_ns = *parsed;
    } else {
      if (meta->attributes.size() >= 48) {
        *error = "too many metadata attributes";
        return false;
      }
      meta->attributes.emplace(std::move(key), std::move(value));
    }
  }
  return true;
}

struct ParseState {
  bool fatal = false;
  bool truncated = false;
  std::string warning;
  std::string error;
  std::uint32_t declared_events = 0;
  bool body_crc_ok = false;
};

bool note_truncation(ParseState& state, std::string message) {
  state.truncated = true;
  if (state.warning.empty()) {
    state.warning = std::move(message);
  }
  return false;
}

}  // namespace

SerializeOutcome serialize_record(const Record& record) {
  SerializeOutcome outcome;
  try {
    if (record.events.size() > limits::kMaxEvents) {
      throw LimitError("event count exceeds record limit");
    }
    Writer body;
    const std::string meta = encode_meta(record.meta);
    body.u32(static_cast<std::uint32_t>(meta.size()));
    body.raw(meta);

    StringTable table;
    std::vector<std::uint32_t> s1_refs;
    std::vector<std::uint32_t> s2_refs;
    s1_refs.reserve(record.events.size());
    s2_refs.reserve(record.events.size());
    for (const auto& event : record.events) {
      if (event.s1.size() > limits::kMaxEventString ||
          event.s2.size() > limits::kMaxEventString) {
        throw LimitError("event string exceeds 8 KiB limit");
      }
      if (event.bytes.size() > limits::kMaxEventPayload) {
        throw LimitError("event payload exceeds 64 KiB limit");
      }
      s1_refs.push_back(table.intern(event.s1));
      s2_refs.push_back(table.intern(event.s2));
    }
    body.u32(table.size());
    for (const auto& value : table.values()) {
      body.u32(static_cast<std::uint32_t>(value.size()));
      body.raw(value);
    }

    body.u32(static_cast<std::uint32_t>(record.events.size()));
    const std::size_t events_start = body.out.size();
    std::vector<std::uint32_t> index;
    for (std::size_t i = 0; i < record.events.size(); ++i) {
      if (i % 64 == 0) {
        const std::size_t offset = body.out.size() - events_start;
        if (offset > std::numeric_limits<std::uint32_t>::max()) {
          throw LimitError("event offset exceeds index range");
        }
        index.push_back(static_cast<std::uint32_t>(offset));
      }
      const Event& event = record.events[i];
      const std::size_t event_start = body.out.size();
      body.u8(static_cast<std::uint8_t>(event.type));
      body.u8(0);
      body.u16(event.task);
      body.u64(event.a);
      body.u64(event.b);
      body.u64(event.c);
      body.u64(event.d);
      body.u32(s1_refs[i]);
      body.u32(s2_refs[i]);
      body.u32(static_cast<std::uint32_t>(event.bytes.size()));
      body.raw(event.bytes);
      const std::string_view serialized(body.out.data() + event_start,
                                        body.out.size() - event_start);
      body.u32(crc32(serialized));
    }
    const std::size_t event_bytes = body.out.size() - events_start;
    body.u32(static_cast<std::uint32_t>(index.size()));
    for (std::uint32_t offset : index) {
      body.u32(offset);
    }
    body.u64(event_bytes);

    if (body.out.size() > limits::kMaxRecordBytes - kHeaderSize - kTrailerSize) {
      throw LimitError("record exceeds 64 MiB limit");
    }
    const std::uint32_t body_crc = crc32(body.out);

    Writer header;
    header.u32(kMagic);
    header.u16(kFormatVersion);
    header.u16(kFlagComplete);
    header.u32(crc32(std::string_view(header.out.data(), 8)));
    header.u64(kHeaderSize);
    header.u64(body.out.size());
    header.u32(body_crc);
    header.u32(0);
    header.u32(crc32(std::string_view(header.out.data(), 36)));

    Writer footer;
    footer.u32(kFooterMagic);
    footer.u64(record.events.size());
    footer.u32(body_crc);
    footer.u32(crc32(footer.out));

    std::string result;
    result.reserve(kHeaderSize + body.out.size() + kTrailerSize);
    result.append(header.out);
    result.append(body.out);
    Writer trailer;
    trailer.u32(body_crc);
    trailer.raw(footer.out);
    result.append(trailer.out);
    outcome.ok = true;
    outcome.bytes = std::move(result);
    return outcome;
  } catch (const LimitError& e) {
    outcome.error = e.what();
    return outcome;
  } catch (const std::bad_alloc&) {
    outcome.error = "out of memory while serializing record";
    return outcome;
  }
}

LoadOutcome load_record_bytes(std::string_view bytes, LoadMode mode) {
  LoadOutcome outcome;
  Record& record = outcome.record;
  if (bytes.size() < kHeaderSize) {
    outcome.record.complete = false;
    outcome.error = "record is shorter than its header";
    return outcome;
  }
  if (bytes.size() > limits::kMaxRecordBytes) {
    outcome.error = "record exceeds 64 MiB limit";
    return outcome;
  }

  Reader reader{reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()};
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t flags = 0;
  std::uint32_t header_crc = 0;
  std::uint64_t body_offset = 0;
  std::uint64_t body_size = 0;
  std::uint32_t body_crc = 0;
  std::uint32_t reserved = 0;
  std::uint32_t header_crc2 = 0;
  if (!reader.u32(magic) || !reader.u16(version) || !reader.u16(flags) ||
      !reader.u32(header_crc) || !reader.u64(body_offset) || !reader.u64(body_size) ||
      !reader.u32(body_crc) || !reader.u32(reserved) || !reader.u32(header_crc2)) {
    outcome.error = "truncated header";
    return outcome;
  }
  (void)reserved;
  if (magic != kMagic) {
    outcome.error = "not a Tempo record (bad magic)";
    return outcome;
  }
  if (version != kFormatVersion) {
    outcome.error = "unsupported record version " + std::to_string(version);
    return outcome;
  }
  if (crc32(std::string_view(bytes.data(), 8)) != header_crc) {
    outcome.error = "header checksum mismatch";
    return outcome;
  }
  if (crc32(std::string_view(bytes.data(), 36)) != header_crc2) {
    outcome.error = "header checksum mismatch";
    return outcome;
  }
  if (body_offset != kHeaderSize) {
    outcome.error = "invalid body offset";
    return outcome;
  }
  if (body_size > limits::kMaxRecordBytes) {
    outcome.error = "body size exceeds record limit";
    return outcome;
  }

  ParseState state;
  const std::size_t available_body =
      std::min<std::size_t>(static_cast<std::size_t>(body_size), bytes.size() - kHeaderSize);
  const std::size_t body_end = kHeaderSize + available_body;
  Reader body{reinterpret_cast<const unsigned char*>(bytes.data()) + kHeaderSize,
              available_body};

  try {
    // ---- meta -------------------------------------------------------------
    std::uint32_t meta_len = 0;
    std::string_view meta_blob;
    if (!body.u32(meta_len) || meta_len > limits::kMaxMetadataBytes ||
        !body.take(meta_len, meta_blob)) {
      note_truncation(state, "truncated metadata section");
    } else if (!decode_meta(meta_blob, &record.meta, &state.error)) {
      state.fatal = true;
    }

    // ---- string table -----------------------------------------------------
    if (!state.fatal && !state.truncated) {
      std::uint32_t string_count = 0;
      if (!body.u32(string_count)) {
        note_truncation(state, "truncated string table size");
      } else if (string_count > limits::kMaxStringCount) {
        state.fatal = true;
        state.error = "invalid string table size";
      } else {
        StringTable table;
        table.clear_all();
        for (std::uint32_t i = 0; i < string_count; ++i) {
          std::uint32_t length = 0;
          std::string value;
          if (!body.u32(length) || length > limits::kMaxStringBytes ||
              !body.raw(value, length)) {
            note_truncation(state, "truncated string table");
            break;
          }
          try {
            table.add_loaded(std::move(value));
          } catch (const LimitError& e) {
            state.fatal = true;
            state.error = e.what();
            break;
          }
        }
        if (!state.fatal && !state.truncated) {
          // ---- events -----------------------------------------------------
          std::uint32_t event_count = 0;
          if (!body.u32(event_count)) {
            note_truncation(state, "truncated event count");
          } else if (event_count > limits::kMaxEvents) {
            state.fatal = true;
            state.error = "invalid event count";
          } else {
            state.declared_events = event_count;
            record.events.reserve(std::min<std::uint32_t>(event_count, 4096));
            const std::size_t events_start = body.pos;
            for (std::uint32_t i = 0; i < event_count; ++i) {
              if (body.remaining() < 48) {
                note_truncation(state, "truncated event stream");
                break;
              }
              const std::size_t event_start = body.pos;
              std::uint8_t type = 0;
              std::uint8_t event_flags = 0;
              std::uint16_t task = 0;
              std::uint64_t a = 0;
              std::uint64_t b = 0;
              std::uint64_t c = 0;
              std::uint64_t d = 0;
              std::uint32_t s1 = 0;
              std::uint32_t s2 = 0;
              std::uint32_t payload_len = 0;
              if (!body.u8(type) || !body.u8(event_flags) || !body.u16(task) ||
                  !body.u64(a) || !body.u64(b) || !body.u64(c) || !body.u64(d) ||
                  !body.u32(s1) || !body.u32(s2) || !body.u32(payload_len)) {
                note_truncation(state, "truncated event header");
                break;
              }
              (void)event_flags;
              if (payload_len > limits::kMaxEventPayload) {
                state.fatal = true;
                state.error = "event payload exceeds limit";
                break;
              }
              if (body.remaining() < static_cast<std::size_t>(payload_len) + 4) {
                note_truncation(state, "truncated event payload");
                break;
              }
              std::string payload;
              if (!body.raw(payload, payload_len)) {
                note_truncation(state, "truncated event payload");
                break;
              }
              std::uint32_t stored_crc = 0;
              if (!body.u32(stored_crc)) {
                note_truncation(state, "truncated event checksum");
                break;
              }
              const std::size_t crc_end = body.pos - 4;
              const std::uint32_t actual_crc = crc32(std::string_view(
                  bytes.data() + kHeaderSize + event_start, crc_end - event_start));
              if (actual_crc != stored_crc) {
                note_truncation(state, "event checksum mismatch");
                break;
              }
              if (s1 >= table.size() || s2 >= table.size()) {
                note_truncation(state, "string reference out of range");
                break;
              }
              Event event;
              event.type = static_cast<EventType>(type);
              event.task = task;
              event.a = a;
              event.b = b;
              event.c = c;
              event.d = d;
              event.s1 = table.at(s1);
              event.s2 = table.at(s2);
              event.bytes = std::move(payload);
              record.events.push_back(std::move(event));
            }
            const std::size_t event_bytes = body.pos - events_start;
            if (record.events.size() != event_count) {
              note_truncation(state, "record ends inside the event stream");
            }

            // ---- index ----------------------------------------------------
            if (!state.truncated) {
              std::uint32_t index_count = 0;
              if (!body.u32(index_count) || index_count > limits::kMaxEvents) {
                note_truncation(state, "truncated event index");
              } else {
                for (std::uint32_t i = 0; i < index_count; ++i) {
                  std::uint32_t offset = 0;
                  if (!body.u32(offset)) {
                    note_truncation(state, "truncated event index");
                    break;
                  }
                  (void)offset;
                }
              }
            }
            if (!state.truncated) {
              std::uint64_t region = 0;
              if (!body.u64(region)) {
                note_truncation(state, "truncated event region size");
              } else if (region != event_bytes) {
                note_truncation(state, "event region size mismatch");
              }
            }
            if (!state.truncated && body.remaining() != 0) {
              note_truncation(state, "trailing bytes in record body");
            }
          }
        }
      }
    }
  } catch (const std::bad_alloc&) {
    outcome.error = "out of memory while parsing record";
    return outcome;
  } catch (const LimitError& e) {
    outcome.error = e.what();
    return outcome;
  }

  if (state.fatal) {
    record.complete = false;
    outcome.error = state.error.empty() ? "malformed record" : state.error;
    return outcome;
  }

  bool body_crc_ok = false;
  if (available_body == body_size) {
    body_crc_ok = crc32(std::string_view(bytes.data() + kHeaderSize, body_size)) == body_crc;
  }
  state.body_crc_ok = body_crc_ok;

  bool footer_ok = false;
  bool exact_size = bytes.size() == kHeaderSize + body_size + kTrailerSize;
  if (body_crc_ok && exact_size && body_size <= bytes.size() - kHeaderSize - kTrailerSize) {
    Reader trailer{reinterpret_cast<const unsigned char*>(bytes.data()) + body_end,
                   bytes.size() - body_end};
    std::uint32_t trailer_crc = 0;
    std::uint32_t footer_magic = 0;
    std::uint64_t footer_count = 0;
    std::uint32_t footer_body_crc = 0;
    std::uint32_t footer_crc = 0;
    const std::size_t footer_start = trailer.pos;
    if (trailer.u32(trailer_crc) && trailer.u32(footer_magic) &&
        trailer.u64(footer_count) && trailer.u32(footer_body_crc) &&
        trailer.u32(footer_crc)) {
      const std::string_view footer_bytes(
          reinterpret_cast<const char*>(trailer.data + footer_start + 4), 16);
      if (footer_magic == kFooterMagic && trailer_crc == body_crc &&
          footer_body_crc == body_crc && footer_crc == crc32(footer_bytes) &&
          footer_count == record.events.size()) {
        footer_ok = true;
      }
    }
  }

  const bool complete = footer_ok && (flags & kFlagComplete) != 0 && !state.truncated &&
                        state.body_crc_ok && record.events.size() == state.declared_events &&
                        exact_size;
  record.complete = complete;
  record.recovered = !complete;
  if (!complete) {
    if (!state.warning.empty()) {
      record.warning = state.warning;
    } else if (available_body < body_size) {
      record.warning = "record is truncated";
    } else if (!state.body_crc_ok) {
      record.warning = "body checksum mismatch";
    } else if (!footer_ok) {
      record.warning = "missing or invalid record footer";
    } else {
      record.warning = "record is incomplete";
    }
  }
  record.recompute_hash();

  if (mode == LoadMode::Strict) {
    if (!complete) {
      outcome.error = record.warning.empty() ? "record is incomplete" : record.warning;
      return outcome;
    }
    if (record.events.size() > limits::kMaxEvents) {
      outcome.error = "event count exceeds limit";
      return outcome;
    }
  }
  outcome.ok = true;
  return outcome;
}

LoadOutcome load_record_file(const std::string& path, LoadMode mode) {
  LoadOutcome outcome;
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    outcome.error = std::string("cannot open record: ") + std::strerror(errno);
    return outcome;
  }
  struct stat info {};
  if (::fstat(fd, &info) < 0) {
    outcome.error = std::string("cannot stat record: ") + std::strerror(errno);
    (void)::close(fd);
    return outcome;
  }
  if (info.st_size < 0 || static_cast<std::uint64_t>(info.st_size) > limits::kMaxRecordBytes) {
    outcome.error = "record file exceeds 64 MiB limit";
    (void)::close(fd);
    return outcome;
  }
  std::string bytes;
  bytes.resize(static_cast<std::size_t>(info.st_size));
  std::size_t read_bytes = 0;
  while (read_bytes < bytes.size()) {
    const ssize_t n = ::read(fd, bytes.data() + read_bytes, bytes.size() - read_bytes);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      outcome.error = std::string("cannot read record: ") + std::strerror(errno);
      (void)::close(fd);
      return outcome;
    }
    if (n == 0) {
      break;
    }
    read_bytes += static_cast<std::size_t>(n);
  }
  (void)::close(fd);
  bytes.resize(read_bytes);
  return load_record_bytes(bytes, mode);
}

bool write_record_file(const Record& record, const std::string& path, std::string* error) {
  SerializeOutcome serialized = serialize_record(record);
  if (!serialized.ok) {
    if (error != nullptr) {
      *error = serialized.error;
    }
    return false;
  }
  const std::string temp_path = path + ".tmp." + std::to_string(::getpid());
  const int fd = ::open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) {
    if (error != nullptr) {
      *error = std::string("cannot create record: ") + std::strerror(errno);
    }
    return false;
  }
  std::size_t written = 0;
  while (written < serialized.bytes.size()) {
    const ssize_t n =
        ::write(fd, serialized.bytes.data() + written, serialized.bytes.size() - written);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (error != nullptr) {
        *error = std::string("cannot write record: ") + std::strerror(errno);
      }
      (void)::close(fd);
      (void)::unlink(temp_path.c_str());
      return false;
    }
    written += static_cast<std::size_t>(n);
  }
  if (::fsync(fd) < 0) {
    if (error != nullptr) {
      *error = std::string("cannot sync record: ") + std::strerror(errno);
    }
    (void)::close(fd);
    (void)::unlink(temp_path.c_str());
    return false;
  }
  (void)::close(fd);
  if (::rename(temp_path.c_str(), path.c_str()) < 0) {
    if (error != nullptr) {
      *error = std::string("cannot rename record: ") + std::strerror(errno);
    }
    (void)::unlink(temp_path.c_str());
    return false;
  }
  return true;
}

}  // namespace tempo
