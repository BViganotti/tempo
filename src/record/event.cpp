#include "tempo/event.hpp"

#include <array>
#include <cstdio>

#include "tempo/limits.hpp"

namespace tempo {
namespace {

std::string_view raw_name(EventType type) noexcept {
  switch (type) {
    case EventType::Unknown:
      return "Unknown";
    case EventType::ScenarioStart:
      return "ScenarioStart";
    case EventType::ScenarioEnd:
      return "ScenarioEnd";
    case EventType::Spawn:
      return "Spawn";
    case EventType::Step:
      return "Step";
    case EventType::Yield:
      return "Yield";
    case EventType::Sleep:
      return "Sleep";
    case EventType::Wake:
      return "Wake";
    case EventType::Exit:
      return "Exit";
    case EventType::Cancel:
      return "Cancel";
    case EventType::Error:
      return "Error";
    case EventType::Log:
      return "Log";
    case EventType::Watch:
      return "Watch";
    case EventType::Note:
      return "Note";
    case EventType::Rng:
      return "Rng";
    case EventType::ClockJump:
      return "ClockJump";
    case EventType::Join:
      return "Join";
    case EventType::FileRead:
      return "FileRead";
    case EventType::FileWrite:
      return "FileWrite";
    case EventType::EnvGet:
      return "EnvGet";
    case EventType::SocketConnect:
      return "SocketConnect";
    case EventType::SocketSend:
      return "SocketSend";
    case EventType::SocketRecv:
      return "SocketRecv";
    case EventType::SocketClose:
      return "SocketClose";
  }
  return "?";
}

std::uint64_t hash_bytes(std::uint64_t state, const void* data, std::size_t size) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < size; ++i) {
    state ^= bytes[i];
    state *= 1099511628211ull;
  }
  return state;
}

std::uint64_t hash_u64(std::uint64_t state, std::uint64_t value) noexcept {
  unsigned char buffer[8];
  for (int i = 0; i < 8; ++i) {
    buffer[i] = static_cast<unsigned char>((value >> (8 * i)) & 0xFFu);
  }
  return hash_bytes(state, buffer, sizeof(buffer));
}

std::string clip(std::string_view text, std::size_t limit) {
  if (text.size() <= limit) {
    return std::string(text);
  }
  std::string out(text.substr(0, limit));
  out += "…";
  return out;
}

}  // namespace

bool Event::operator==(const Event& other) const noexcept {
  return type == other.type && task == other.task && a == other.a && b == other.b &&
         c == other.c && d == other.d && s1 == other.s1 && s2 == other.s2 &&
         bytes == other.bytes;
}

std::string_view event_type_name(EventType type) noexcept {
  return raw_name(type);
}

bool is_capability_event(EventType type) noexcept {
  switch (type) {
    case EventType::FileRead:
    case EventType::FileWrite:
    case EventType::EnvGet:
    case EventType::SocketConnect:
    case EventType::SocketSend:
    case EventType::SocketRecv:
    case EventType::SocketClose:
      return true;
    default:
      return false;
  }
}

bool is_lifecycle_event(EventType type) noexcept {
  switch (type) {
    case EventType::ScenarioStart:
    case EventType::ScenarioEnd:
    case EventType::Spawn:
    case EventType::Step:
    case EventType::Yield:
    case EventType::Sleep:
    case EventType::Wake:
    case EventType::Exit:
    case EventType::Cancel:
    case EventType::Error:
    case EventType::Join:
      return true;
    default:
      return false;
  }
}

std::string event_summary(const Event& event) {
  std::string out;
  switch (event.type) {
    case EventType::ScenarioStart:
      out = "scenario start: " + event.s1;
      break;
    case EventType::ScenarioEnd:
      out = "scenario end";
      break;
    case EventType::Spawn:
      out = "spawn task " + std::to_string(event.b) + " \"" + event.s1 + "\"";
      if (event.a != kNoTask) {
        out += " (parent " + std::to_string(event.a) + ")";
      }
      break;
    case EventType::Step:
      out = "step " + std::to_string(event.a) + ": run task " + std::to_string(event.b);
      break;
    case EventType::Yield:
      out = "yield";
      break;
    case EventType::Sleep:
      out = "sleep until " + std::to_string(event.a) + "ns";
      break;
    case EventType::Wake:
      out = "wake";
      if (!event.s1.empty()) {
        out += " (" + event.s1 + ")";
      }
      break;
    case EventType::Exit:
      out = event.b != 0 ? "task finished" : "task failed";
      if (!event.s1.empty()) {
        out += ": " + clip(event.s1, 160);
      }
      break;
    case EventType::Cancel:
      out = "cancel requested";
      if (!event.s1.empty()) {
        out += " (" + event.s1 + ")";
      }
      break;
    case EventType::Error:
      out = "error: " + clip(event.s1, 200);
      break;
    case EventType::Log:
      out = "log: " + clip(event.s1, 200);
      break;
    case EventType::Watch:
      out = "watch " + event.s1 + " = " + clip(event.s2, 120);
      break;
    case EventType::Note:
      out = "note " + event.s1 + " = " + clip(event.s2, 160);
      break;
    case EventType::Rng:
      out = "random " + std::to_string(event.a);
      break;
    case EventType::ClockJump:
      out = "clock -> " + std::to_string(event.a) + "ns";
      break;
    case EventType::Join:
      out = "join task " + std::to_string(event.a);
      break;
    case EventType::FileRead:
      out = std::string("read ") + event.s1 + (event.a != 0 ? " ok" : " failed") +
            " (" + std::to_string(event.b) + " bytes)";
      break;
    case EventType::FileWrite:
      out = std::string("write ") + event.s1 + (event.a != 0 ? " ok" : " failed") +
            " (" + std::to_string(event.b) + " bytes)";
      break;
    case EventType::EnvGet:
      out = std::string("env ") + event.s1 + (event.a != 0 ? " ok" : " failed");
      break;
    case EventType::SocketConnect:
      out = "connect " + event.s1 + ":" + std::to_string(event.a) +
            (event.c != 0 ? " ok" : " failed");
      break;
    case EventType::SocketSend:
      out = "socket send handle " + std::to_string(event.a) + " (" +
            std::to_string(event.b) + " bytes)";
      break;
    case EventType::SocketRecv:
      out = "socket recv handle " + std::to_string(event.a) + " (" +
            std::to_string(event.bytes.size()) + " bytes)";
      break;
    case EventType::SocketClose:
      out = "socket close handle " + std::to_string(event.a) +
            (event.b != 0 ? " ok" : " failed");
      break;
    case EventType::Unknown:
      out = "unknown event";
      break;
  }
  return out;
}

std::uint64_t event_hash(std::uint64_t state, const Event& event) noexcept {
  state = hash_u64(state, static_cast<std::uint64_t>(event.type));
  state = hash_u64(state, static_cast<std::uint64_t>(event.task));
  state = hash_u64(state, event.a);
  state = hash_u64(state, event.b);
  state = hash_u64(state, event.c);
  state = hash_u64(state, event.d);
  state = hash_u64(state, event.s1.size());
  state = hash_bytes(state, event.s1.data(), event.s1.size());
  state = hash_u64(state, event.s2.size());
  state = hash_bytes(state, event.s2.data(), event.s2.size());
  state = hash_u64(state, event.bytes.size());
  state = hash_bytes(state, event.bytes.data(), event.bytes.size());
  return state;
}

std::uint64_t fnv1a64(std::uint64_t state, std::string_view bytes) noexcept {
  return hash_bytes(state, bytes.data(), bytes.size());
}

}  // namespace tempo
