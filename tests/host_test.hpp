#pragma once

// Deterministic in-memory RunHost used by runtime, record, replay and debug
// tests. It never touches the OS: capabilities are served from configured maps.

#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tempo/host.hpp"

namespace tempo::test {

class TestHost final : public RunHost {
 public:
  std::vector<Event> events;
  std::map<std::string, std::string> files;
  std::map<std::string, std::string> envs;
  std::map<std::uint32_t, std::string> recv_buffers;
  std::uint32_t next_socket = 1;
  TaskId forced_choice = kNoTask;
  bool fail_choose = false;
  std::uint64_t choose_calls = 0;

  TaskId choose_ready(const std::vector<TaskId>& ready, std::uint64_t draw_index,
                      std::uint64_t /*step*/) override {
    ++choose_calls;
    if (fail_choose) {
      throw std::runtime_error("choose failed");
    }
    if (forced_choice != kNoTask) {
      return forced_choice;
    }
    if (ready.empty()) {
      return kNoTask;
    }
    return ready[draw_index % ready.size()];
  }

  CapResult file_read(const std::string& path) override {
    CapResult result;
    auto it = files.find(path);
    if (it == files.end()) {
      result.error = "no such file";
      return result;
    }
    result.ok = true;
    result.data = it->second;
    result.num = it->second.size();
    return result;
  }

  CapResult file_write(const std::string& path, const std::string& data) override {
    files[path] = data;
    CapResult result;
    result.ok = true;
    result.num = data.size();
    return result;
  }

  CapResult env_get(const std::string& name) override {
    CapResult result;
    auto it = envs.find(name);
    if (it == envs.end()) {
      result.error = "not set";
      return result;
    }
    result.ok = true;
    result.data = it->second;
    return result;
  }

  SocketConnectResult socket_connect(const std::string& host, std::uint16_t port) override {
    SocketConnectResult result;
    if (host != "127.0.0.1" || port == 0) {
      result.error = "connection refused";
      return result;
    }
    result.ok = true;
    result.handle = next_socket++;
    recv_buffers[result.handle] = "";
    return result;
  }

  CapResult socket_send(std::uint32_t handle, const std::string& data) override {
    CapResult result;
    auto it = recv_buffers.find(handle);
    if (it == recv_buffers.end()) {
      result.error = "bad handle";
      return result;
    }
    it->second += data;
    result.ok = true;
    result.num = data.size();
    return result;
  }

  CapResult socket_recv(std::uint32_t handle, std::uint32_t max_bytes) override {
    CapResult result;
    auto it = recv_buffers.find(handle);
    if (it == recv_buffers.end()) {
      result.error = "bad handle";
      return result;
    }
    const std::size_t take = std::min<std::size_t>(max_bytes, it->second.size());
    result.ok = true;
    result.data = it->second.substr(0, take);
    it->second.erase(0, take);
    result.num = take;
    return result;
  }

  CapResult socket_close(std::uint32_t handle) override {
    CapResult result;
    recv_buffers.erase(handle);
    result.ok = true;
    return result;
  }

  void emit(Event event) override { events.push_back(std::move(event)); }

  std::size_t count(EventType type) const {
    std::size_t n = 0;
    for (const auto& e : events) {
      if (e.type == type) {
        ++n;
      }
    }
    return n;
  }

  const Event* find(EventType type) const {
    for (const auto& e : events) {
      if (e.type == type) {
        return &e;
      }
    }
    return nullptr;
  }
};

}  // namespace tempo::test
