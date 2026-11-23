#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "temp_dir.hpp"
#include "test_util.hpp"
#include "tempo/http.hpp"
#include "tempo/workbench.hpp"

using namespace tempo;

namespace {

std::string build_request(std::uint16_t port, const std::string& token,
                          const std::string& method, const std::string& target,
                          const std::string& body = {}) {
  std::string request;
  request += method + " " + target;
  request += target.find('?') == std::string::npos ? "?token=" + token : "&token=" + token;
  request += " HTTP/1.1\r\n";
  request += "Host: 127.0.0.1:" + std::to_string(port) + "\r\n";
  request += "X-Tempo-Token: " + token + "\r\n";
  if (!body.empty()) {
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  }
  request += "Connection: close\r\n\r\n";
  request += body;
  return request;
}

std::string http_exchange(std::uint16_t port, const std::string& request) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return {};
  timeval timeout{5, 0};
  (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  (void)::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    (void)::close(fd);
    return {};
  }
  std::size_t sent = 0;
  while (sent < request.size()) {
    const ssize_t n = ::send(fd, request.data() + sent, request.size() - sent, 0);
    if (n <= 0) {
      (void)::close(fd);
      return {};
    }
    sent += static_cast<std::size_t>(n);
  }
  (void)::shutdown(fd, SHUT_WR);
  std::string response;
  char buffer[8192];
  for (;;) {
    const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
    if (n <= 0) break;
    response.append(buffer, static_cast<std::size_t>(n));
  }
  (void)::close(fd);
  return response;
}

int status_of(const std::string& response) {
  if (response.size() < 12) return 0;
  return std::stoi(response.substr(9, 3));
}

}  // namespace

TEST(workbench_serves_ui_api_and_sse_over_the_real_server) {
  test::TempDir dir;
  Workbench workbench(WorkbenchOptions{.data_dir = dir.path()});
  std::string error;
  CHECK(workbench.initialize(&error));

  for (int attempt = 0; attempt < 10; ++attempt) {
    if (!workbench.records().empty()) break;
  }

  HttpServerOptions options;
  options.token = "integration-token-0123456789";
  HttpServer server(options, [&workbench](const HttpRequest& request) {
    return workbench.handle(request);
  });
  workbench.set_broadcaster([&server](std::string_view event, std::string_view data) {
    server.broadcast_sse(event, data);
  });
  CHECK(server.start(&error));
  std::atomic<bool> stop{false};
  std::thread thread([&server, &stop] {
    server.run([&stop] { return stop.load(); });
  });
  const std::uint16_t port = server.port();
  const std::string token = options.token;

  const std::string index =
      http_exchange(port, build_request(port, token, "GET", "/"));
  CHECK_EQ(status_of(index), 200);
  CHECK(index.find("Tempo") != std::string::npos);
  CHECK(index.find("app.js") != std::string::npos);

  const std::string overview =
      http_exchange(port, build_request(port, token, "GET", "/api/overview"));
  CHECK_EQ(status_of(overview), 200);
  CHECK(overview.find("counter-race") != std::string::npos);

  const std::string recorded = http_exchange(
      port, build_request(port, token, "POST", "/api/record",
                          R"({"scenario":"counter-race","seed":7})"));
  CHECK_EQ(status_of(recorded), 200);
  CHECK(recorded.find("\"ok\":true") != std::string::npos);

  const std::string events = http_exchange(
      port, build_request(port, token, "GET", "/api/records/1/events?from=0&limit=5"));
  CHECK_EQ(status_of(events), 200);
  CHECK(events.find("\"events\"") != std::string::npos);

  const std::string unauthorized = http_exchange(
      port, build_request(port, "wrong-token", "GET", "/api/overview"));
  CHECK_EQ(status_of(unauthorized), 403);

  // Connect an SSE client and verify a real broadcast from the workbench.
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  CHECK(fd >= 0);
  timeval timeout{5, 0};
  (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  (void)::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
  const std::string stream_request =
      "GET /api/stream?token=" + token + " HTTP/1.1\r\nHost: 127.0.0.1:" +
      std::to_string(port) + "\r\n\r\n";
  CHECK_EQ(::send(fd, stream_request.data(), stream_request.size(), 0),
           static_cast<ssize_t>(stream_request.size()));
  std::string received;
  char buffer[4096];
  auto read_until = [&](const std::string& needle) {
    for (int attempt = 0; attempt < 100; ++attempt) {
      if (received.find(needle) != std::string::npos) return true;
      const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
      if (n <= 0) return false;
      received.append(buffer, static_cast<std::size_t>(n));
    }
    return false;
  };
  CHECK(read_until("text/event-stream"));
  (void)http_exchange(port, build_request(port, token, "POST", "/api/record",
                                     R"({"scenario":"pipeline","seed":2})"));
  CHECK(read_until("event: overview"));
  (void)::close(fd);

  stop = true;
  thread.join();
  server.stop();
}

TEST(integration_rejects_bad_host_and_methods) {
  test::TempDir dir;
  Workbench workbench(WorkbenchOptions{.data_dir = dir.path()});
  std::string error;
  CHECK(workbench.initialize(&error));
  HttpServerOptions options;
  options.token = "host-test-token";
  HttpServer server(options, [&workbench](const HttpRequest& request) {
    return workbench.handle(request);
  });
  CHECK(server.start(&error));
  std::atomic<bool> stop{false};
  std::thread thread([&server, &stop] {
    server.run([&stop] { return stop.load(); });
  });
  const std::uint16_t port = server.port();
  const std::string token = options.token;

  std::string bad_host = build_request(port, token, "GET", "/api/overview");
  bad_host.replace(bad_host.find("Host: 127.0.0.1:"), 16, "Host: evil.example:");
  CHECK_EQ(status_of(http_exchange(port, bad_host)), 403);

  const std::string put = http_exchange(
      port, build_request(port, token, "PUT", "/api/overview"));
  CHECK_EQ(status_of(put), 405);

  stop = true;
  thread.join();
  server.stop();
}
