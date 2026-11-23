#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <thread>

#include "test_util.hpp"
#include "tempo/runtime.hpp"
#include "tempo/system.hpp"

using namespace tempo;

namespace {

class EchoServer {
 public:
  EchoServer() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      return;
    }
    int one = 1;
    (void)::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
      return;
    }
    socklen_t length = sizeof(addr);
    if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &length) < 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
      return;
    }
    port_ = ntohs(addr.sin_port);
    if (::listen(listen_fd_, 4) < 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
      return;
    }
    thread_ = std::thread([this] { serve_once(); });
  }

  ~EchoServer() {
    if (thread_.joinable()) {
      thread_.join();
    }
    if (listen_fd_ >= 0) {
      ::close(listen_fd_);
    }
  }

  bool ok() const { return listen_fd_ >= 0 && port_ != 0; }
  std::uint16_t port() const { return port_; }

 private:
  void serve_once() {
    pollfd pfd{};
    pfd.fd = listen_fd_;
    pfd.events = POLLIN;
    if (::poll(&pfd, 1, 5000) <= 0) {
      return;
    }
    const int client = ::accept(listen_fd_, nullptr, nullptr);
    if (client < 0) {
      return;
    }
    char buffer[64];
    const ssize_t got = ::recv(client, buffer, sizeof(buffer), 0);
    if (got > 0) {
      std::size_t sent = 0;
      while (sent < static_cast<std::size_t>(got)) {
        const ssize_t n = ::send(client, buffer + sent,
                                 static_cast<std::size_t>(got) - sent, 0);
        if (n <= 0) {
          break;
        }
        sent += static_cast<std::size_t>(n);
      }
    }
    (void)::shutdown(client, SHUT_WR);
    (void)::close(client);
  }

  int listen_fd_ = -1;
  std::uint16_t port_ = 0;
  std::thread thread_;
};

Task echo_scenario(Context ctx, std::uint16_t port) {
  const SocketConnectResult conn = ctx.connect("127.0.0.1", port);
  ctx.watch("connected", conn.ok);
  if (!conn.ok) {
    ctx.watch("connect_error", conn.error);
    co_return;
  }
  ctx.watch("handle", static_cast<std::int64_t>(conn.handle));
  const CapResult sent = ctx.send(conn.handle, "ping");
  ctx.watch("sent", static_cast<std::int64_t>(sent.num));
  const CapResult echoed = ctx.recv(conn.handle, 32);
  ctx.watch("echo", echoed.data);
  const CapResult closed = ctx.close_socket(conn.handle);
  ctx.watch("closed", closed.ok);
}

}  // namespace

TEST(loopback_socket_echoes_through_capability) {
  EchoServer server;
  CHECK(server.ok());

  std::vector<Event> events;
  SystemHost host(SystemCapabilityOptions{});
  host.set_event_sink([&events](Event event) { events.push_back(std::move(event)); });
  Scheduler scheduler(host);
  auto entry = [port = server.port()](Context ctx) { return echo_scenario(ctx, port); };
  scheduler.spawn_root("echo", entry);
  const RunSummary summary = scheduler.run();
  CHECK(summary.ok);

  bool connected = false;
  bool closed = false;
  std::string echo;
  for (const auto& event : events) {
    if (event.type == EventType::Watch) {
      if (event.s1 == "connected") {
        connected = event.s2 == "true";
      } else if (event.s1 == "echo") {
        echo = event.s2;
      } else if (event.s1 == "closed") {
        closed = event.s2 == "true";
      }
    }
  }
  CHECK(connected);
  CHECK(closed);
  CHECK_EQ(echo, std::string("ping"));
}

TEST(socket_errors_are_reported_not_fatal) {
  SystemHost host(SystemCapabilityOptions{});
  // Port 1 on loopback is almost certainly closed; either way the call must
  // return a recorded error rather than throwing.
  const SocketConnectResult conn = host.socket_connect("127.0.0.1", 1);
  if (conn.ok) {
    (void)host.socket_close(conn.handle);
  } else {
    CHECK(!conn.error.empty());
  }
  const CapResult bad = host.socket_send(9999, "x");
  CHECK(!bad.ok);
  CHECK_EQ(bad.error, std::string("unknown socket handle"));
}
