#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

#include "test_util.hpp"
#include "tempo/http.hpp"
#include "tempo/limits.hpp"
#include "tempo/json.hpp"

using namespace tempo;

namespace {

struct ClientResponse {
  bool ok = false;
  std::string raw;
};

std::string make_request(const std::string& method, const std::string& target,
                         std::uint16_t port, const std::string& token,
                         const std::string& body = {},
                         const std::string& extra_headers = {}) {
  std::string request;
  request += method + " " + target + " HTTP/1.1\r\n";
  request += "Host: 127.0.0.1:" + std::to_string(port) + "\r\n";
  request += "X-Tempo-Token: " + token + "\r\n";
  if (!body.empty()) {
    request += "Content-Type: application/json\r\n";
  }
  request += extra_headers;
  if (!body.empty()) {
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  }
  request += "Connection: close\r\n\r\n";
  request += body;
  return request;
}

ClientResponse send_and_read(std::uint16_t port, const std::string& request) {
  ClientResponse result;
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return result;
  }
  timeval timeout{5, 0};
  (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  (void)::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return result;
  }
  std::size_t sent = 0;
  while (sent < request.size()) {
    const ssize_t n = ::send(fd, request.data() + sent, request.size() - sent, 0);
    if (n <= 0) {
      ::close(fd);
      return result;
    }
    sent += static_cast<std::size_t>(n);
  }
  (void)::shutdown(fd, SHUT_WR);
  char buffer[8192];
  for (;;) {
    const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
    if (n > 0) {
      result.raw.append(buffer, static_cast<std::size_t>(n));
      continue;
    }
    break;
  }
  ::close(fd);
  result.ok = true;
  return result;
}

int read_status(const std::string& raw) {
  if (raw.size() < 12 || raw.rfind("HTTP/1.1 ", 0) != 0) {
    return 0;
  }
  return std::stoi(raw.substr(9, 3));
}

class TestServer {
 public:
  TestServer()
      : options_{.port = 0, .token = "test-token-123456"} {
    handler = [](const HttpRequest& request) {
      if (request.path == "/api/ping") {
        json::Value value = json::Value::object();
        value["pong"] = json::Value(true);
        if (const std::string* n = request.header("x-test")) {
          value["header"] = json::Value(*n);
        }
        HttpResponse response;
        response.body = value.dump();
        return response;
      }
      if (request.path == "/api/stream") {
        HttpResponse response;
        response.sse = true;
        response.body = ": hello\n\n";
        return response;
      }
      if (request.path == "/api/echo" && request.method == "POST") {
        const auto parsed = json::parse(request.body);
        if (!parsed.ok) {
          HttpResponse response;
          response.status = 400;
          response.body = "{\"error\":\"bad json\"}";
          return response;
        }
        HttpResponse response;
        response.body = parsed.value.dump();
        return response;
      }
      HttpResponse response;
      response.status = 404;
      response.body = "not found";
      return response;
    };
    server = std::make_unique<HttpServer>(options_, handler);
  }

  ~TestServer() {
    stop();
  }

  bool start() {
    std::string error;
    if (!server->start(&error)) {
      return false;
    }
    thread = std::thread([this] {
      server->run([this] { return stop_requested.load(); });
    });
    return true;
  }

  void stop() {
    if (!thread.joinable()) {
      return;
    }
    stop_requested = true;
    thread.join();
  }

  std::uint16_t port() const { return server->port(); }
  const std::string& token() const { return options_.token; }

  HttpServerOptions options_;
  HttpHandler handler;
  std::unique_ptr<HttpServer> server;
  std::thread thread;
  std::atomic<bool> stop_requested{false};
};

}  // namespace

TEST(parses_a_complete_get_request) {
  const std::string raw =
      "GET /api/things?token=abc&limit=5 HTTP/1.1\r\n"
      "Host: 127.0.0.1:1234\r\n"
      "X-Tempo-Token: abc\r\n"
      "\r\n";
  const HttpRequestParse parsed = parse_http_request(raw);
  CHECK(parsed.state == HttpRequestParse::State::Complete);
  CHECK_EQ(parsed.request.method, std::string("GET"));
  CHECK_EQ(parsed.request.path, std::string("/api/things"));
  CHECK_EQ(parsed.request.query, std::string("token=abc&limit=5"));
  CHECK(parsed.request.header("host") != nullptr);
  CHECK_EQ(parsed.request.header("x-tempo-token")->size(), std::size_t(3));
  CHECK_EQ(parsed.consumed, raw.size());
}

TEST(parses_post_body_and_waits_for_completion) {
  const std::string raw =
      "POST /api/record HTTP/1.1\r\n"
      "Host: 127.0.0.1:1234\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: 7\r\n"
      "\r\n"
      "{\"a\":1}";
  const HttpRequestParse parsed = parse_http_request(raw);
  CHECK(parsed.state == HttpRequestParse::State::Complete);
  CHECK_EQ(parsed.request.body, std::string("{\"a\":1}"));
  const HttpRequestParse partial = parse_http_request(raw.substr(0, raw.size() - 1));
  CHECK(partial.state == HttpRequestParse::State::Incomplete);
}

TEST(rejects_dangerous_or_malformed_requests) {
  const std::string host = "Host: 127.0.0.1:1\r\n";
  const std::string prefix = "GET / HTTP/1.1\r\n" + host;
  {
    const auto parsed = parse_http_request(prefix + "Transfer-Encoding: chunked\r\n\r\n");
    CHECK(parsed.state == HttpRequestParse::State::Error);
  }
  {
    const auto parsed = parse_http_request(prefix + "X-A: 1\r\nX-A: 2\r\n\r\n");
    CHECK(parsed.state == HttpRequestParse::State::Error);
  }
  {
    const auto parsed = parse_http_request(prefix + " folded\r\n\r\n");
    CHECK(parsed.state == HttpRequestParse::State::Error);
  }
  {
    const auto parsed = parse_http_request("GET / HTTP/1.1\r\nHost: x\r\nContent-Length: 999999999999\r\n\r\n");
    CHECK(parsed.state == HttpRequestParse::State::Error);
    CHECK_EQ(parsed.error_status, 413);
  }
  {
    const auto parsed = parse_http_request("GET / HTTP/1.1\r\n\r\n");
    CHECK(parsed.state == HttpRequestParse::State::Complete);
  }
  {
    const auto parsed = parse_http_request("GET / HTTP/1.1\r\nHost: x\r\nBad Name: v\r\n\r\n");
    CHECK(parsed.state == HttpRequestParse::State::Error);
  }
  {
    const auto parsed = parse_http_request("GET http://evil/ HTTP/1.1\r\nHost: x\r\n\r\n");
    CHECK(parsed.state == HttpRequestParse::State::Error);
  }
}

TEST(header_limit_is_enforced) {
  std::string raw = "GET / HTTP/1.1\r\nHost: x\r\n";
  raw.append(limits::kMaxHttpHeaderBytes, 'a');
  const auto parsed = parse_http_request(raw);
  CHECK(parsed.state == HttpRequestParse::State::Error);
  CHECK_EQ(parsed.error_status, 431);
}

TEST(query_and_url_decoding) {
  const auto query = parse_query("a=1&b=hello%20world&c=x%2By&d=%2F");
  CHECK_EQ(query.at("a"), std::string("1"));
  CHECK_EQ(query.at("b"), std::string("hello world"));
  CHECK_EQ(query.at("c"), std::string("x+y"));
  CHECK_EQ(query.at("d"), std::string("/"));
  CHECK_EQ(url_decode("a+b"), std::string("a b"));
  CHECK_EQ(url_decode("%ZZ"), std::string("%ZZ"));
}

TEST(server_serves_authorized_requests) {
  TestServer server;
  CHECK(server.start());
  const std::string token = server.token();

  const auto ok = send_and_read(
      server.port(), make_request("GET", "/api/ping?token=" + token, server.port(), token));
  CHECK(ok.ok);
  CHECK_EQ(read_status(ok.raw), 200);
  CHECK(ok.raw.find("\"pong\":true") != std::string::npos);
  CHECK(ok.raw.find("Content-Security-Policy") != std::string::npos);
  CHECK(ok.raw.find("Connection: close") != std::string::npos);

  const std::string body = R"({"hello":"world"})";
  const auto echoed = send_and_read(server.port(),
                                    make_request("POST", "/api/echo?token=" + token,
                                                 server.port(), token, body));
  CHECK_EQ(read_status(echoed.raw), 200);
  CHECK(echoed.raw.find("\"hello\":\"world\"") != std::string::npos);

  const auto missing = send_and_read(
      server.port(), make_request("GET", "/api/ping", server.port(), "wrong-token"));
  CHECK_EQ(read_status(missing.raw), 403);

  const auto not_found = send_and_read(
      server.port(), make_request("GET", "/nope?token=" + token, server.port(), token));
  CHECK_EQ(read_status(not_found.raw), 404);
}

TEST(server_rejects_bad_hosts_origins_methods_and_content_types) {
  TestServer server;
  CHECK(server.start());
  const std::string token = server.token();

  std::string bad_host = make_request("GET", "/api/ping?token=" + token, server.port(), token);
  bad_host.replace(bad_host.find("Host: 127.0.0.1:"), 16, "Host: evil.example:");
  const auto host_response = send_and_read(server.port(), bad_host);
  CHECK_EQ(read_status(host_response.raw), 403);

  const auto origin_response = send_and_read(
      server.port(), make_request("GET", "/api/ping?token=" + token, server.port(), token,
                                  {}, "Origin: http://evil.example\r\n"));
  CHECK_EQ(read_status(origin_response.raw), 403);

  const auto method_response = send_and_read(
      server.port(), make_request("PUT", "/api/ping?token=" + token, server.port(), token));
  CHECK_EQ(read_status(method_response.raw), 405);

  std::string form_request =
      "POST /api/echo?token=" + token + " HTTP/1.1\r\nHost: 127.0.0.1:" +
      std::to_string(server.port()) + "\r\nX-Tempo-Token: " + token +
      "\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 3\r\n\r\na=b";
  const auto form_response = send_and_read(server.port(), form_request);
  CHECK_EQ(read_status(form_response.raw), 415);
}

TEST(server_streams_sse_messages) {
  TestServer server;
  CHECK(server.start());
  const std::string token = server.token();
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  CHECK(fd >= 0);
  timeval timeout{5, 0};
  (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(server.port());
  (void)::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  CHECK(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
  const std::string request =
      "GET /api/stream?token=" + token + " HTTP/1.1\r\nHost: 127.0.0.1:" +
      std::to_string(server.port()) + "\r\n\r\n";
  CHECK_EQ(::send(fd, request.data(), request.size(), 0),
           static_cast<ssize_t>(request.size()));

  std::string received;
  char buffer[4096];
  auto read_until = [&](const std::string& needle) {
    for (int attempt = 0; attempt < 100; ++attempt) {
      if (received.find(needle) != std::string::npos) {
        return true;
      }
      const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
      if (n <= 0) {
        return false;
      }
      received.append(buffer, static_cast<std::size_t>(n));
    }
    return false;
  };
  CHECK(read_until("text/event-stream"));
  CHECK(read_until(": hello"));
  server.server->broadcast_sse("record", "{\"id\":1}");
  CHECK(read_until("event: record\ndata: {\"id\":1}"));
  (void)::close(fd);
  server.stop();
  CHECK(received.find("200 OK") != std::string::npos);
}

TEST(status_texts_and_serialization) {
  CHECK_EQ(http_status_text(200), std::string("OK"));
  CHECK_EQ(http_status_text(404), std::string("Not Found"));
  HttpResponse response;
  response.status = 204;
  response.body = "";
  const std::string serialized = http_serialize_response(response, true);
  CHECK(serialized.rfind("HTTP/1.1 204 No Content\r\n", 0) == 0);
  CHECK(serialized.find("Content-Length: 0") != std::string::npos);
}

TEST(server_requires_a_token_to_start) {
  HttpServerOptions options;
  options.token = "";
  HttpServer server(options, [](const HttpRequest&) { return HttpResponse{}; });
  std::string error;
  CHECK(!server.start(&error));
  CHECK(error.find("token") != std::string::npos);
}
