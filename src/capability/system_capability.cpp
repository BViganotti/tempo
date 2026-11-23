#include "tempo/system.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace tempo {
namespace {

std::string errno_text(int code) {
  return std::string(std::strerror(code));
}

bool has_nul(const std::string& s) {
  return s.find('\0') != std::string::npos;
}

bool valid_env_name(const std::string& name) {
  if (name.empty() || name.size() > 255) {
    return false;
  }
  for (char c : name) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '_';
    if (!ok) {
      return false;
    }
  }
  return true;
}

int connect_loopback(const std::string& host, std::uint16_t port, std::uint32_t timeout_ms,
                     std::string* error) {
  sockaddr_storage storage{};
  socklen_t length = 0;
  int family = AF_UNSPEC;

  if (host == "127.0.0.1" || host == "localhost") {
    auto* addr = reinterpret_cast<sockaddr_in*>(&storage);
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    if (inet_pton(AF_INET, "127.0.0.1", &addr->sin_addr) != 1) {
      *error = "invalid loopback address";
      return -1;
    }
    length = sizeof(sockaddr_in);
    family = AF_INET;
  } else if (host == "::1") {
    auto* addr = reinterpret_cast<sockaddr_in6*>(&storage);
    addr->sin6_family = AF_INET6;
    addr->sin6_port = htons(port);
    if (inet_pton(AF_INET6, "::1", &addr->sin6_addr) != 1) {
      *error = "invalid loopback address";
      return -1;
    }
    length = sizeof(sockaddr_in6);
    family = AF_INET6;
  } else {
    *error = "only loopback hosts are permitted";
    return -1;
  }

  const int fd = ::socket(family, SOCK_STREAM, 0);
  if (fd < 0) {
    *error = errno_text(errno);
    return -1;
  }

  timeval timeout{};
  timeout.tv_sec = static_cast<time_t>(timeout_ms / 1000);
  timeout.tv_usec = static_cast<suseconds_t>((timeout_ms % 1000) * 1000);
  (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  (void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
  int one = 1;
  (void)::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags >= 0) {
    (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
  int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&storage), length);
  if (rc < 0 && errno != EINPROGRESS) {
    *error = errno_text(errno);
    ::close(fd);
    return -1;
  }
  if (rc < 0) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLOUT;
    const int ready = ::poll(&pfd, 1, static_cast<int>(timeout_ms));
    if (ready <= 0) {
      *error = ready == 0 ? "connect timed out" : errno_text(errno);
      ::close(fd);
      return -1;
    }
    int socket_error = 0;
    socklen_t socket_error_len = sizeof(socket_error);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_len) < 0) {
      *error = errno_text(errno);
      ::close(fd);
      return -1;
    }
    if (socket_error != 0) {
      *error = errno_text(socket_error);
      ::close(fd);
      return -1;
    }
  }
  if (flags >= 0) {
    (void)::fcntl(fd, F_SETFL, flags);
  }
  return fd;
}

}  // namespace

bool safe_relative_path_policy(const std::string& path) noexcept {
  if (path.empty() || path.size() > limits::kMaxPathBytes || has_nul(path)) {
    return false;
  }
  if (path.front() == '/') {
    return false;
  }
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t slash = path.find('/', start);
    const std::size_t end = slash == std::string::npos ? path.size() : slash;
    if (end == start) {
      return false;
    }
    const std::string_view part(path.data() + start, end - start);
    if (part == "." || part == "..") {
      return false;
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
  return true;
}

SystemHost::SystemHost(SystemCapabilityOptions options) : options_(std::move(options)) {
  if (!options_.data_root.empty()) {
    char* resolved = ::realpath(options_.data_root.c_str(), nullptr);
    if (resolved != nullptr) {
      options_.data_root = resolved;
      std::free(resolved);
      struct stat info {};
      if (::stat(options_.data_root.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
        root_ok_ = true;
      }
    }
  }
  if (options_.socket_timeout_ms == 0) {
    options_.socket_timeout_ms = 2000;
  }
  if (options_.max_open_sockets == 0) {
    options_.max_open_sockets = limits::kMaxSocketConnections;
  }
}

SystemHost::~SystemHost() {
  close_all();
}

void SystemHost::close_all() noexcept {
  for (auto& [handle, fd] : sockets_) {
    (void)handle;
    if (fd >= 0) {
      (void)::close(fd);
    }
  }
  sockets_.clear();
}

TaskId SystemHost::choose_ready(const std::vector<TaskId>& ready, std::uint64_t draw_index,
                                std::uint64_t /*step*/) {
  if (ready.empty()) {
    return kNoTask;
  }
  return ready[draw_index % ready.size()];
}

void SystemHost::emit(Event event) {
  if (sink_) {
    sink_(std::move(event));
  }
}

bool SystemHost::safe_relative_path(const std::string& path, std::string* full) const {
  if (!root_ok_ || !safe_relative_path_policy(path)) {
    return false;
  }
  std::string candidate = options_.data_root;
  candidate += '/';
  candidate += path;
  *full = std::move(candidate);
  return true;
}

CapResult SystemHost::file_read(const std::string& path) {
  CapResult result;
  std::string full;
  if (options_.data_root.empty()) {
    result.error = "no data root configured";
    return result;
  }
  if (!root_ok_) {
    result.error = "data root is not a readable directory";
    return result;
  }
  if (!safe_relative_path(path, &full)) {
    result.error = "path is not a safe relative path";
    return result;
  }
  const int fd = ::open(full.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) {
    result.error = errno_text(errno);
    return result;
  }
  struct stat info {};
  if (::fstat(fd, &info) < 0) {
    result.error = errno_text(errno);
    (void)::close(fd);
    return result;
  }
  if (!S_ISREG(info.st_mode)) {
    result.error = "not a regular file";
    (void)::close(fd);
    return result;
  }
  if (info.st_size < 0 || static_cast<std::uint64_t>(info.st_size) > limits::kMaxFileBytes) {
    result.error = "file exceeds 65536 byte capability limit";
    (void)::close(fd);
    return result;
  }
  std::string data;
  data.reserve(static_cast<std::size_t>(info.st_size));
  char buffer[8192];
  for (;;) {
    const ssize_t got = ::read(fd, buffer, sizeof(buffer));
    if (got < 0) {
      if (errno == EINTR) {
        continue;
      }
      result.error = errno_text(errno);
      (void)::close(fd);
      return result;
    }
    if (got == 0) {
      break;
    }
    if (data.size() + static_cast<std::size_t>(got) > limits::kMaxFileBytes) {
      result.error = "file exceeds 65536 byte capability limit";
      (void)::close(fd);
      return result;
    }
    data.append(buffer, static_cast<std::size_t>(got));
  }
  (void)::close(fd);
  result.ok = true;
  result.num = data.size();
  result.data = std::move(data);
  return result;
}

CapResult SystemHost::file_write(const std::string& path, const std::string& data) {
  CapResult result;
  std::string full;
  if (options_.data_root.empty()) {
    result.error = "no data root configured";
    return result;
  }
  if (!root_ok_) {
    result.error = "data root is not a writable directory";
    return result;
  }
  if (!safe_relative_path(path, &full)) {
    result.error = "path is not a safe relative path";
    return result;
  }
  if (data.size() > limits::kMaxFileBytes) {
    result.error = "write exceeds 65536 byte capability limit";
    return result;
  }
  const int fd = ::open(full.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC,
                        0600);
  if (fd < 0) {
    result.error = errno_text(errno);
    return result;
  }
  std::size_t written = 0;
  while (written < data.size()) {
    const ssize_t n = ::write(fd, data.data() + written, data.size() - written);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      result.error = errno_text(errno);
      (void)::close(fd);
      return result;
    }
    written += static_cast<std::size_t>(n);
  }
  if (::close(fd) < 0) {
    result.error = errno_text(errno);
    return result;
  }
  result.ok = true;
  result.num = written;
  return result;
}

CapResult SystemHost::env_get(const std::string& name) {
  CapResult result;
  if (!options_.allow_env) {
    result.error = "environment capability disabled";
    return result;
  }
  if (!valid_env_name(name)) {
    result.error = "invalid environment variable name";
    return result;
  }
  const char* value = std::getenv(name.c_str());
  if (value == nullptr) {
    result.error = "not set";
    return result;
  }
  const std::size_t length = std::strlen(value);
  if (length > limits::kMaxEnvBytes) {
    result.error = "environment value exceeds 4096 byte limit";
    return result;
  }
  result.ok = true;
  result.data.assign(value, length);
  result.num = length;
  return result;
}

SocketConnectResult SystemHost::socket_connect(const std::string& host, std::uint16_t port) {
  SocketConnectResult result;
  if (!options_.allow_loopback_sockets) {
    result.error = "socket capability disabled";
    return result;
  }
  if (port == 0) {
    result.error = "port must be non-zero";
    return result;
  }
  if (sockets_.size() >= options_.max_open_sockets) {
    result.error = "socket handle limit reached";
    return result;
  }
  std::string error;
  const int fd = connect_loopback(host, port, options_.socket_timeout_ms, &error);
  if (fd < 0) {
    result.error = std::move(error);
    return result;
  }
  const std::uint32_t handle = next_handle_++;
  sockets_[handle] = fd;
  result.ok = true;
  result.handle = handle;
  return result;
}

CapResult SystemHost::socket_send(std::uint32_t handle, const std::string& data) {
  CapResult result;
  auto it = sockets_.find(handle);
  if (it == sockets_.end()) {
    result.error = "unknown socket handle";
    return result;
  }
  if (data.size() > limits::kMaxSocketPayload) {
    result.error = "socket send exceeds 16384 byte limit";
    return result;
  }
  std::size_t sent = 0;
  while (sent < data.size()) {
    const ssize_t n = ::send(it->second, data.data() + sent, data.size() - sent, 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      result.error = errno_text(errno);
      return result;
    }
    if (n == 0) {
      result.error = "connection closed while sending";
      return result;
    }
    sent += static_cast<std::size_t>(n);
  }
  result.ok = true;
  result.num = sent;
  return result;
}

CapResult SystemHost::socket_recv(std::uint32_t handle, std::uint32_t max_bytes) {
  CapResult result;
  auto it = sockets_.find(handle);
  if (it == sockets_.end()) {
    result.error = "unknown socket handle";
    return result;
  }
  if (max_bytes == 0 || max_bytes > limits::kMaxSocketPayload) {
    result.error = "socket recv limit must be 1..16384";
    return result;
  }
  std::string data;
  char buffer[4096];
  while (data.size() < max_bytes) {
    const std::size_t want = std::min<std::size_t>(sizeof(buffer), max_bytes - data.size());
    const ssize_t n = ::recv(it->second, buffer, want, 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (!data.empty()) {
          break;
        }
        result.error = "socket receive timed out";
        return result;
      }
      result.error = errno_text(errno);
      return result;
    }
    if (n == 0) {
      break;
    }
    data.append(buffer, static_cast<std::size_t>(n));
  }
  result.ok = true;
  result.num = data.size();
  result.data = std::move(data);
  return result;
}

CapResult SystemHost::socket_close(std::uint32_t handle) {
  CapResult result;
  auto it = sockets_.find(handle);
  if (it == sockets_.end()) {
    result.error = "unknown socket handle";
    return result;
  }
  if (it->second >= 0) {
    (void)::close(it->second);
  }
  sockets_.erase(it);
  result.ok = true;
  return result;
}

}  // namespace tempo
