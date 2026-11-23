#include <cstdint>
#include <string>
#include <vector>

#include "test_util.hpp"
#include "tempo/http.hpp"
#include "tempo/limits.hpp"

using namespace tempo;

namespace {

struct Rng {
  std::uint64_t state;
  explicit Rng(std::uint64_t seed) : state(seed ? seed : 0x9E3779B97F4A7C15ull) {}
  std::uint64_t next() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }
  std::size_t pick(std::size_t limit) { return limit == 0 ? 0 : next() % limit; }
};

std::string random_request(Rng& rng) {
  const bool post = (rng.next() & 1u) != 0;
  std::string method = post ? "POST" : "GET";
  std::string path = "/api/";
  static const char* const names[] = {"overview", "record", "replay", "things",
                                      "records/1/events"};
  path += names[rng.pick(5)];
  if (rng.pick(2) == 0) {
    path += "?token=abc&limit=" + std::to_string(rng.pick(100));
  }
  std::string body;
  if (post) {
    body = R"({"scenario":"counter-race","seed":)" + std::to_string(rng.pick(1000)) + "}";
  }
  std::string request;
  request += method + " " + path + " HTTP/1.1\r\n";
  request += "Host: 127.0.0.1:7777\r\n";
  request += "X-Tempo-Token: abc\r\n";
  if (!body.empty()) {
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  }
  const std::size_t extra = rng.pick(3);
  for (std::size_t i = 0; i < extra; ++i) {
    request += "X-Test-" + std::to_string(i) + ": value\r\n";
  }
  request += "\r\n";
  request += body;
  return request;
}

}  // namespace

TEST(random_valid_requests_parse_exactly) {
  Rng rng(0x48);
  for (int iteration = 0; iteration < 500; ++iteration) {
    const std::string text = random_request(rng);
    const HttpRequestParse parsed = parse_http_request(text);
    CHECK(parsed.state == HttpRequestParse::State::Complete);
    CHECK_EQ(parsed.consumed, text.size());
    CHECK(!parsed.request.method.empty());
    CHECK_EQ(parsed.request.path.front(), '/');
    CHECK(parsed.request.header("host") != nullptr);
  }
}

TEST(random_mutations_never_violate_parser_invariants) {
  Rng rng(0x91);
  for (int iteration = 0; iteration < 500; ++iteration) {
    std::string text = random_request(rng);
    switch (rng.next() % 4) {
      case 0:
        if (!text.empty()) text[rng.pick(text.size())] = static_cast<char>(rng.next());
        break;
      case 1:
        if (!text.empty()) text.resize(rng.pick(text.size()));
        break;
      case 2:
        text.insert(text.begin() + static_cast<std::ptrdiff_t>(rng.pick(text.size() + 1)),
                    static_cast<char>(rng.next()));
        break;
      default:
        if (!text.empty()) text.erase(rng.pick(text.size()), 1);
        break;
    }
    const HttpRequestParse parsed = parse_http_request(text);
    if (parsed.state == HttpRequestParse::State::Complete) {
      CHECK(parsed.consumed <= text.size());
      CHECK(!parsed.request.path.empty());
      CHECK_EQ(parsed.request.path.front(), '/');
      CHECK(parsed.request.body.size() <= limits::kMaxHttpBodyBytes);
      for (const auto& [name, value] : parsed.request.headers) {
        (void)value;
        CHECK(!name.empty());
      }
    } else if (parsed.state == HttpRequestParse::State::Error) {
      CHECK(!parsed.error.empty());
      CHECK(parsed.error_status >= 400);
      CHECK(parsed.error_status <= 599);
    }
  }
}
