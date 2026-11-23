#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace tempo {

/// Parsed HTTP/1.x request. Header names are lowercase. Query is the raw text
/// after '?' (not including it). `target` is the origin-form target.
struct HttpRequest {
  std::string method;
  std::string target;
  std::string path;
  std::string query;
  std::string version;
  std::map<std::string, std::string, std::less<>> headers;
  std::string body;

  const std::string* header(std::string_view name) const noexcept;
};

struct HttpResponse {
  int status = 200;
  std::string content_type = "application/json; charset=utf-8";
  std::string body;
  bool sse = false;
  std::vector<std::pair<std::string, std::string>> extra_headers;
};

struct HttpRequestParse {
  enum class State { Incomplete, Complete, Error };
  State state = State::Incomplete;
  HttpRequest request;
  int error_status = 400;
  std::string error;
  std::size_t consumed = 0;
};

/// Parse a request from a possibly-incomplete buffer. Enforces header/body
/// limits (16 KiB headers, 256 KiB body), rejects duplicate headers, obs-fold,
/// transfer-encoding, control characters and non-origin-form targets.
HttpRequestParse parse_http_request(std::string_view buffer);

std::string http_status_text(int status);
std::string url_decode(std::string_view text);
std::map<std::string, std::string, std::less<>> parse_query(std::string_view query);
std::string http_serialize_response(const HttpResponse& response, bool close_connection);

struct HttpServerOptions {
  std::string bind_address = "127.0.0.1";
  std::uint16_t port = 0;  // 0 = ephemeral
  /// Required. Minted per launch and checked on every request.
  std::string token;
  std::size_t max_connections = 32;
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

/// Minimal poll-based HTTP/1.1 + SSE server bound to loopback.
///
/// Security: requires the per-session token (query `token` or `x-tempo-token`),
/// validates the Host/Origin headers, sends no CORS headers, rejects
/// transfer-encoding, and bounds every buffer. Normal responses close the
/// connection; SSE connections stay open and receive bounded broadcasts.
///
/// Thread-safety: one event-loop thread owns all connections. `broadcast_sse`
/// may be called from any thread; it only touches a mutex-protected queue.
class HttpServer {
 public:
  HttpServer(HttpServerOptions options, HttpHandler handler);
  ~HttpServer();

  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  bool start(std::string* error);
  void stop();
  bool is_running() const noexcept { return running_; }

  /// Run until stop() or `should_stop()` returns true.
  void run(const std::function<bool()>& should_stop = {});

  /// One poll iteration; used by run() and by tests.
  void poll_once(int timeout_ms);

  std::uint16_t port() const noexcept { return port_; }
  std::string url() const;

  /// Queue one SSE frame for every connected stream. Bounded to 128 frames.
  void broadcast_sse(std::string_view event, std::string_view data);

 private:
  struct Connection {
    int fd = -1;
    std::string in;
    std::string out;
    bool streaming = false;
    bool close_after_write = false;
    bool dead = false;
    std::int64_t deadline_ms = 0;
    std::int64_t last_ping_ms = 0;
  };

  void accept_connections();
  void handle_read(Connection& conn);
  void flush(Connection& conn);
  void route(Connection& conn, const HttpRequest& request);
  bool authorize(const HttpRequest& request, HttpResponse* error) const;
  void close_connection(Connection& conn);
  void drain_sse();
  static bool secure_equals(std::string_view a, std::string_view b) noexcept;

  HttpServerOptions options_;
  HttpHandler handler_;
  int listen_fd_ = -1;
  std::uint16_t port_ = 0;
  bool running_ = false;
  std::atomic<bool> stop_requested_{false};
  std::vector<std::unique_ptr<Connection>> connections_;
  std::mutex sse_mutex_;
  std::vector<std::string> sse_pending_;
  std::int64_t started_ms_ = 0;
};

}  // namespace tempo
