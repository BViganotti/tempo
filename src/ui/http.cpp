#include "tempo/http.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <utility>

#include "tempo/limits.hpp"

namespace tempo {
namespace {

constexpr std::size_t kHeaderLimit = limits::kMaxHttpHeaderBytes;
constexpr std::size_t kBodyLimit = limits::kMaxHttpBodyBytes;
constexpr std::size_t kMaxHeaderCount = 64;
constexpr std::size_t kMaxTarget = 2'048;
constexpr std::size_t kMaxHeaderValue = 8'192;
constexpr std::int64_t kRequestTimeoutMs = 15'000;
constexpr std::int64_t kPingIntervalMs = 15'000;
constexpr std::size_t kSseQueueLimit = 128;

std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool is_token_char(char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return std::isalnum(u) != 0 || c == '!' || c == '#' || c == '$' || c == '%' ||
         c == '&' || c == '\'' || c == '*' || c == '+' || c == '-' || c == '.' ||
         c == '^' || c == '_' || c == '`' || c == '|' || c == '~';
}

bool has_control(std::string_view text) {
  for (unsigned char c : text) {
    if (c < 0x20 || c == 0x7F) {
      return true;
    }
  }
  return false;
}

std::string lower_copy(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

std::string_view trim(std::string_view text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
    text.remove_suffix(1);
  }
  return text;
}

bool parse_content_length(std::string_view text, std::size_t& value) {
  if (text.empty() || text.size() > 20) {
    return false;
  }
  value = 0;
  for (char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
    const std::size_t digit = static_cast<std::size_t>(c - '0');
    if (value > (SIZE_MAX - digit) / 10) {
      return false;
    }
    value = value * 10 + digit;
  }
  return true;
}

HttpResponse error_response(int status, std::string message) {
  HttpResponse response;
  response.status = status;
  response.content_type = "text/plain; charset=utf-8";
  response.body = std::move(message);
  return response;
}

bool host_matches(const std::string& host, std::uint16_t port) {
  const std::string expected_v4 = "127.0.0.1:" + std::to_string(port);
  const std::string expected_local = "localhost:" + std::to_string(port);
  return host == expected_v4 || host == expected_local;
}

}  // namespace

const std::string* HttpRequest::header(std::string_view name) const noexcept {
  auto it = headers.find(name);
  return it == headers.end() ? nullptr : &it->second;
}

std::string http_status_text(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 204:
      return "No Content";
    case 400:
      return "Bad Request";
    case 403:
      return "Forbidden";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 408:
      return "Request Timeout";
    case 411:
      return "Length Required";
    case 413:
      return "Payload Too Large";
    case 415:
      return "Unsupported Media Type";
    case 431:
      return "Request Header Fields Too Large";
    case 500:
      return "Internal Server Error";
    case 503:
      return "Service Unavailable";
    default:
      return "Unknown";
  }
}

std::string url_decode(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '%' && i + 2 < text.size()) {
      auto hex = [](char h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        return -1;
      };
      const int hi = hex(text[i + 1]);
      const int lo = hex(text[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    if (c == '+') {
      out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::map<std::string, std::string, std::less<>> parse_query(std::string_view query) {
  std::map<std::string, std::string, std::less<>> result;
  std::size_t start = 0;
  while (start <= query.size() && result.size() < 64) {
    const std::size_t amp = query.find('&', start);
    const std::size_t end = amp == std::string_view::npos ? query.size() : amp;
    const std::string_view pair = query.substr(start, end - start);
    if (!pair.empty()) {
      const std::size_t eq = pair.find('=');
      const std::string_view key_raw = eq == std::string_view::npos ? pair : pair.substr(0, eq);
      const std::string_view value_raw =
          eq == std::string_view::npos ? std::string_view() : pair.substr(eq + 1);
      if (key_raw.size() <= 256 && value_raw.size() <= 2'048) {
        result[url_decode(key_raw)] = url_decode(value_raw);
      }
    }
    if (amp == std::string_view::npos) {
      break;
    }
    start = amp + 1;
  }
  return result;
}

HttpRequestParse parse_http_request(std::string_view buffer) {
  HttpRequestParse result;
  const std::size_t header_end = buffer.find("\r\n\r\n");
  if (header_end == std::string_view::npos) {
    if (buffer.size() > kHeaderLimit) {
      result.state = HttpRequestParse::State::Error;
      result.error_status = 431;
      result.error = "request headers exceed 16 KiB";
    }
    return result;
  }
  if (header_end + 4 > kHeaderLimit) {
    result.state = HttpRequestParse::State::Error;
    result.error_status = 431;
    result.error = "request headers exceed 16 KiB";
    return result;
  }
  const std::string_view head = buffer.substr(0, header_end);
  const std::size_t line_end = head.find("\r\n");
  const std::string_view request_line =
      line_end == std::string_view::npos ? head : head.substr(0, line_end);
  const std::string_view first_space = request_line;
  const std::size_t space1 = first_space.find(' ');
  if (space1 == std::string_view::npos) {
    result.state = HttpRequestParse::State::Error;
    result.error = "malformed request line";
    return result;
  }
  const std::size_t space2 = first_space.find(' ', space1 + 1);
  if (space2 == std::string_view::npos || first_space.find(' ', space2 + 1) != std::string_view::npos) {
    result.state = HttpRequestParse::State::Error;
    result.error = "malformed request line";
    return result;
  }
  const std::string_view method = first_space.substr(0, space1);
  const std::string_view target = first_space.substr(space1 + 1, space2 - space1 - 1);
  const std::string_view version = first_space.substr(space2 + 1);
  if (method.empty() || method.size() > 16) {
    result.state = HttpRequestParse::State::Error;
    result.error = "invalid method";
    return result;
  }
  for (char c : method) {
    if (!is_token_char(c)) {
      result.state = HttpRequestParse::State::Error;
      result.error = "invalid method";
      return result;
    }
  }
  if (version != "HTTP/1.1" && version != "HTTP/1.0") {
    result.state = HttpRequestParse::State::Error;
    result.error = "unsupported HTTP version";
    return result;
  }
  if (target.empty() || target.size() > kMaxTarget || target.front() != '/' ||
      has_control(target)) {
    result.state = HttpRequestParse::State::Error;
    result.error = "invalid request target";
    return result;
  }
  result.request.method = std::string(method);
  result.request.target = std::string(target);
  result.request.version = std::string(version);
  const std::size_t question = target.find('?');
  if (question == std::string_view::npos) {
    result.request.path = std::string(target);
  } else {
    result.request.path = std::string(target.substr(0, question));
    result.request.query = std::string(target.substr(question + 1));
    if (result.request.query.size() > limits::kMaxQueryBytes) {
      result.state = HttpRequestParse::State::Error;
      result.error_status = 413;
      result.error = "query exceeds 2 KiB";
      return result;
    }
  }

  std::size_t pos = line_end == std::string_view::npos ? head.size() : line_end + 2;
  while (pos < head.size()) {
    const std::size_t end = head.find("\r\n", pos);
    const std::string_view line =
        end == std::string_view::npos ? head.substr(pos) : head.substr(pos, end - pos);
    if (line.empty()) {
      break;
    }
    if (line.front() == ' ' || line.front() == '\t') {
      result.state = HttpRequestParse::State::Error;
      result.error = "obsolete header folding is rejected";
      return result;
    }
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0) {
      result.state = HttpRequestParse::State::Error;
      result.error = "malformed header";
      return result;
    }
    const std::string_view name = line.substr(0, colon);
    const std::string_view value = trim(line.substr(colon + 1));
    if (name.size() > 64 || value.size() > kMaxHeaderValue || has_control(value)) {
      result.state = HttpRequestParse::State::Error;
      result.error = "invalid header";
      return result;
    }
    for (char c : name) {
      if (!is_token_char(c)) {
        result.state = HttpRequestParse::State::Error;
        result.error = "invalid header name";
        return result;
      }
    }
    if (result.request.headers.size() >= kMaxHeaderCount) {
      result.state = HttpRequestParse::State::Error;
      result.error_status = 431;
      result.error = "too many headers";
      return result;
    }
    const std::string lowered = lower_copy(name);
    if (result.request.headers.find(lowered) != result.request.headers.end()) {
      result.state = HttpRequestParse::State::Error;
      result.error = "duplicate header";
      return result;
    }
    result.request.headers.emplace(lowered, std::string(value));
    if (end == std::string_view::npos) {
      break;
    }
    pos = end + 2;
  }

  if (result.request.headers.find("transfer-encoding") != result.request.headers.end()) {
    result.state = HttpRequestParse::State::Error;
    result.error = "transfer-encoding is not supported";
    return result;
  }
  std::size_t content_length = 0;
  if (auto it = result.request.headers.find("content-length");
      it != result.request.headers.end()) {
    if (!parse_content_length(it->second, content_length)) {
      result.state = HttpRequestParse::State::Error;
      result.error = "invalid content-length";
      return result;
    }
    if (content_length > kBodyLimit) {
      result.state = HttpRequestParse::State::Error;
      result.error_status = 413;
      result.error = "request body exceeds 256 KiB";
      return result;
    }
  }
  const std::size_t total = header_end + 4 + content_length;
  if (buffer.size() < total) {
    return result;
  }
  result.request.body = std::string(buffer.substr(header_end + 4, content_length));
  result.state = HttpRequestParse::State::Complete;
  result.consumed = total;
  return result;
}

std::string http_serialize_response(const HttpResponse& response, bool close_connection) {
  std::string out;
  out.reserve(response.body.size() + 512);
  out += "HTTP/1.1 ";
  out += std::to_string(response.status);
  out += ' ';
  out += http_status_text(response.status);
  out += "\r\nContent-Type: ";
  out += response.content_type;
  out += "\r\nContent-Length: ";
  out += std::to_string(response.body.size());
  out += "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
  out += "Referrer-Policy: no-referrer\r\n";
  out += "Content-Security-Policy: default-src 'self'; script-src 'self'; "
         "style-src 'self'; img-src 'self' data:; connect-src 'self'; "
         "frame-ancestors 'none'; base-uri 'none'\r\n";
  out += close_connection ? "Connection: close\r\n" : "Connection: keep-alive\r\n";
  for (const auto& [name, value] : response.extra_headers) {
    out += name;
    out += ": ";
    out += value;
    out += "\r\n";
  }
  out += "\r\n";
  out += response.body;
  return out;
}

HttpServer::HttpServer(HttpServerOptions options, HttpHandler handler)
    : options_(std::move(options)), handler_(std::move(handler)) {
  if (options_.max_connections == 0) {
    options_.max_connections = limits::kMaxHttpConnections;
  }
}

HttpServer::~HttpServer() {
  stop();
  for (auto& conn : connections_) {
    if (conn && conn->fd >= 0) {
      (void)::close(conn->fd);
      conn->fd = -1;
    }
  }
  connections_.clear();
  if (listen_fd_ >= 0) {
    (void)::close(listen_fd_);
    listen_fd_ = -1;
  }
}

bool HttpServer::secure_equals(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) {
    return false;
  }
  unsigned char diff = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    diff = static_cast<unsigned char>(diff | static_cast<unsigned char>(a[i] ^ b[i]));
  }
  return diff == 0;
}

bool HttpServer::start(std::string* error) {
  if (running_) {
    if (error) *error = "server already started";
    return false;
  }
  if (options_.token.empty()) {
    if (error) *error = "server token must not be empty";
    return false;
  }
  if (options_.bind_address != "127.0.0.1") {
    if (error) *error = "Tempo only binds 127.0.0.1";
    return false;
  }
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    if (error) *error = std::string("socket: ") + std::strerror(errno);
    return false;
  }
  int one = 1;
  (void)::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
  (void)::setsockopt(listen_fd_, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(options_.port);
  if (::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
    if (error) *error = "invalid bind address";
    (void)::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    if (error) *error = std::string("bind: ") + std::strerror(errno);
    (void)::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  socklen_t length = sizeof(addr);
  if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &length) < 0) {
    if (error) *error = std::string("getsockname: ") + std::strerror(errno);
    (void)::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  port_ = ntohs(addr.sin_port);
  if (::listen(listen_fd_, 16) < 0) {
    if (error) *error = std::string("listen: ") + std::strerror(errno);
    (void)::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  const int flags = ::fcntl(listen_fd_, F_GETFL, 0);
  if (flags >= 0) {
    (void)::fcntl(listen_fd_, F_SETFL, flags | O_NONBLOCK);
  }
  running_ = true;
  stop_requested_ = false;
  started_ms_ = now_ms();
  return true;
}

void HttpServer::stop() {
  stop_requested_ = true;
  running_ = false;
}

std::string HttpServer::url() const {
  return "http://127.0.0.1:" + std::to_string(port_) + "/?token=" + options_.token;
}

void HttpServer::run(const std::function<bool()>& should_stop) {
  while (running_ && !stop_requested_) {
    if (should_stop && should_stop()) {
      break;
    }
    poll_once(200);
  }
  running_ = false;
}

void HttpServer::close_connection(Connection& conn) {
  if (conn.fd >= 0) {
    (void)::close(conn.fd);
    conn.fd = -1;
  }
  conn.dead = true;
  conn.in.clear();
  conn.out.clear();
}

void HttpServer::accept_connections() {
  for (;;) {
    const int fd = ::accept(listen_fd_, nullptr, nullptr);
    if (fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      break;
    }
    if (connections_.size() >= options_.max_connections) {
      (void)::close(fd);
      continue;
    }
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
      (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    int one = 1;
    (void)::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
    (void)::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    auto conn = std::make_unique<Connection>();
    conn->fd = fd;
    conn->deadline_ms = now_ms() + kRequestTimeoutMs;
    connections_.push_back(std::move(conn));
  }
}

void HttpServer::handle_read(Connection& conn) {
  char buffer[8192];
  for (;;) {
    const ssize_t n = ::recv(conn.fd, buffer, sizeof(buffer), 0);
    if (n > 0) {
      if (conn.in.size() + static_cast<std::size_t>(n) >
          kHeaderLimit + kBodyLimit + 4) {
        conn.out += http_serialize_response(
            error_response(413, "request exceeds server buffer limits"), true);
        conn.close_after_write = true;
        conn.in.clear();
        return;
      }
      conn.in.append(buffer, static_cast<std::size_t>(n));
      continue;
    }
    if (n == 0) {
      if (!conn.streaming && !conn.in.empty()) {
        break;
      }
      close_connection(conn);
      return;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    }
    close_connection(conn);
    return;
  }

  if (conn.streaming) {
    if (conn.in.size() > kHeaderLimit) {
      close_connection(conn);
    }
    return;
  }

  const HttpRequestParse parsed = parse_http_request(conn.in);
  if (parsed.state == HttpRequestParse::State::Incomplete) {
    if (now_ms() > conn.deadline_ms) {
      conn.out += http_serialize_response(error_response(408, "request timed out"), true);
      conn.close_after_write = true;
    }
    return;
  }
  if (parsed.state == HttpRequestParse::State::Error) {
    conn.out += http_serialize_response(
        error_response(parsed.error_status, parsed.error), true);
    conn.close_after_write = true;
    conn.in.clear();
    return;
  }
  route(conn, parsed.request);
}

void HttpServer::route(Connection& conn, const HttpRequest& request) {
  conn.in.clear();
  HttpResponse error;
  if (!authorize(request, &error)) {
    conn.out += http_serialize_response(error, true);
    conn.close_after_write = true;
    return;
  }
  HttpResponse response;
  try {
    response = handler_(request);
  } catch (const std::exception& e) {
    response = error_response(500, std::string("handler error: ") + e.what());
  } catch (...) {
    response = error_response(500, "handler error");
  }
  if (response.sse) {
    conn.streaming = true;
    conn.last_ping_ms = now_ms();
    conn.deadline_ms = now_ms() + kRequestTimeoutMs;
    std::string head;
    head += "HTTP/1.1 200 OK\r\n";
    head += "Content-Type: text/event-stream\r\n";
    head += "Cache-Control: no-store\r\n";
    head += "X-Content-Type-Options: nosniff\r\n";
    head += "Referrer-Policy: no-referrer\r\n";
    head += "Content-Security-Policy: default-src 'self'; connect-src 'self'\r\n";
    head += "Connection: keep-alive\r\n\r\n";
    head += response.body;
    conn.out += head;
  } else {
    if (response.body.size() > limits::kMaxHttpResponseBytes) {
      response = error_response(500, "response exceeds 4 MiB limit");
    }
    conn.out += http_serialize_response(response, true);
    conn.close_after_write = true;
  }
}

bool HttpServer::authorize(const HttpRequest& request, HttpResponse* error) const {
  if (request.method != "GET" && request.method != "POST") {
    *error = error_response(405, "only GET and POST are supported");
    return false;
  }
  const std::string* host = request.header("host");
  if (host == nullptr || !host_matches(*host, port_)) {
    *error = error_response(403, "invalid Host header");
    return false;
  }
  if (const std::string* origin = request.header("origin"); origin != nullptr) {
    const std::string allowed_v4 = "http://127.0.0.1:" + std::to_string(port_);
    const std::string allowed_local = "http://localhost:" + std::to_string(port_);
    if (*origin != allowed_v4 && *origin != allowed_local) {
      *error = error_response(403, "cross-origin request rejected");
      return false;
    }
  }
  std::string token;
  if (const std::string* header_token = request.header("x-tempo-token");
      header_token != nullptr) {
    token = *header_token;
  } else {
    const auto query = parse_query(request.query);
    auto it = query.find("token");
    if (it != query.end()) {
      token = it->second;
    }
  }
  if (token.empty() || !secure_equals(token, options_.token)) {
    *error = error_response(403, "missing or invalid session token");
    return false;
  }
  if (request.method == "POST" && !request.body.empty()) {
    const std::string* type = request.header("content-type");
    const bool import_endpoint = request.path == "/api/records/import";
    if (import_endpoint) {
      if (type == nullptr || type->rfind("application/octet-stream", 0) != 0) {
        *error = error_response(415, "record imports must be application/octet-stream");
        return false;
      }
    } else if (type == nullptr || type->rfind("application/json", 0) != 0) {
      *error = error_response(415, "POST bodies must be application/json");
      return false;
    }
  }
  return true;
}

void HttpServer::flush(Connection& conn) {
  while (!conn.out.empty()) {
    const ssize_t n = ::send(conn.fd, conn.out.data(), conn.out.size(), 0);
    if (n > 0) {
      conn.out.erase(0, static_cast<std::size_t>(n));
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return;
    }
    close_connection(conn);
    return;
  }
  if (conn.close_after_write && conn.out.empty()) {
    close_connection(conn);
  }
}

void HttpServer::broadcast_sse(std::string_view event, std::string_view data) {
  std::string frame = "event: ";
  bool clean = !event.empty() && event.size() <= 32;
  for (char c : event) {
    if (!is_token_char(c)) {
      clean = false;
      break;
    }
  }
  frame += clean ? std::string(event) : std::string("message");
  frame += "\ndata: ";
  for (char c : data) {
    if (c == '\r' || c == '\n') {
      frame.push_back(' ');
    } else {
      frame.push_back(c);
    }
  }
  frame += "\n\n";
  if (frame.size() > limits::kMaxHttpResponseBytes) {
    return;
  }
  std::lock_guard<std::mutex> lock(sse_mutex_);
  sse_pending_.push_back(std::move(frame));
  if (sse_pending_.size() > kSseQueueLimit) {
    sse_pending_.erase(sse_pending_.begin());
  }
}

void HttpServer::drain_sse() {
  std::vector<std::string> frames;
  {
    std::lock_guard<std::mutex> lock(sse_mutex_);
    frames.swap(sse_pending_);
  }
  const std::int64_t now = now_ms();
  for (auto& conn : connections_) {
    if (!conn || conn->dead || !conn->streaming) {
      continue;
    }
    for (const auto& frame : frames) {
      if (conn->out.size() + frame.size() > limits::kMaxHttpOutBytes) {
        close_connection(*conn);
        break;
      }
      conn->out += frame;
    }
    if (!conn->dead && now - conn->last_ping_ms >= kPingIntervalMs) {
      conn->out += ": keepalive\n\n";
      conn->last_ping_ms = now;
    }
  }
}

void HttpServer::poll_once(int timeout_ms) {
  if (!running_ || listen_fd_ < 0) {
    return;
  }
  std::vector<pollfd> fds;
  fds.reserve(connections_.size() + 1);
  fds.push_back(pollfd{listen_fd_, POLLIN, 0});
  std::vector<std::size_t> index;
  index.reserve(connections_.size());
  for (std::size_t i = 0; i < connections_.size(); ++i) {
    Connection* conn = connections_[i].get();
    if (conn == nullptr || conn->dead || conn->fd < 0) {
      continue;
    }
    short events = POLLIN;
    if (!conn->out.empty()) {
      events = static_cast<short>(events | POLLOUT);
    }
    fds.push_back(pollfd{conn->fd, events, 0});
    index.push_back(i);
  }
  const int ready = ::poll(fds.data(), fds.size(), timeout_ms);
  if (ready < 0 && errno != EINTR) {
    return;
  }
  if (ready > 0) {
    if ((fds[0].revents & POLLIN) != 0) {
      accept_connections();
    }
    for (std::size_t i = 0; i < index.size(); ++i) {
      if (fds[i + 1].revents == 0) {
        continue;
      }
      Connection* conn = connections_[index[i]].get();
      if (conn == nullptr || conn->dead) {
        continue;
      }
      if ((fds[i + 1].revents & (POLLERR | POLLNVAL | POLLHUP)) != 0) {
        if ((fds[i + 1].revents & POLLIN) == 0) {
          close_connection(*conn);
          continue;
        }
      }
      if ((fds[i + 1].revents & POLLIN) != 0) {
        handle_read(*conn);
      }
      if (!conn->dead && (fds[i + 1].revents & POLLOUT) != 0) {
        flush(*conn);
      }
    }
  }

  const std::int64_t now = now_ms();
  for (auto& conn : connections_) {
    if (!conn || conn->dead) {
      continue;
    }
    if (!conn->streaming && now > conn->deadline_ms && conn->out.empty()) {
      close_connection(*conn);
    } else if (conn->streaming && now - started_ms_ > 24ll * 3600ll * 1000ll) {
      close_connection(*conn);
    } else if (!conn->out.empty()) {
      flush(*conn);
    }
  }
  drain_sse();
  connections_.erase(
      std::remove_if(connections_.begin(), connections_.end(),
                     [](const std::unique_ptr<Connection>& conn) {
                       return conn == nullptr || (conn->dead && conn->fd < 0);
                     }),
      connections_.end());
}

}  // namespace tempo
