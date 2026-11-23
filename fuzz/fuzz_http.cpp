// Deterministic mutation fuzzer for the HTTP request parser and serializer.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "tempo/http.hpp"
#include "tempo/limits.hpp"

namespace {

struct Rng {
  std::uint64_t state;
  explicit Rng(std::uint64_t seed) : state(seed ? seed : 0xBADF00D1234ull) {}
  std::uint64_t next() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }
  std::size_t pick(std::size_t limit) { return limit == 0 ? 0 : next() % limit; }
};

}  // namespace

int main(int argc, char** argv) {
  std::uint64_t iterations = 2000;
  std::uint64_t seed = 0xFEEDFACE;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
      iterations = std::strtoull(argv[++i], nullptr, 10);
    } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      seed = std::strtoull(argv[++i], nullptr, 10);
    } else {
      std::fprintf(stderr, "usage: fuzz_http [--iterations N] [--seed S]\n");
      return 2;
    }
  }
  if (iterations > 1'000'000) iterations = 1'000'000;

  const std::vector<std::string> seeds = {
      "GET /?token=abc HTTP/1.1\r\nHost: 127.0.0.1:1234\r\n\r\n",
      "POST /api/record HTTP/1.1\r\nHost: 127.0.0.1:9\r\nContent-Type: application/json\r\n"
      "Content-Length: 2\r\n\r\n{}",
      "GET /api/records/1/events?from=0&limit=10 HTTP/1.0\r\nHost: localhost:9\r\n\r\n",
      "POST /api/replay HTTP/1.1\r\nHost: localhost:9\r\nContent-Length: 0\r\n\r\n",
  };
  Rng rng(seed);
  std::uint64_t complete = 0;
  std::uint64_t incomplete = 0;
  std::uint64_t errors = 0;
  for (std::uint64_t iteration = 0; iteration < iterations; ++iteration) {
    std::string text = seeds[rng.pick(seeds.size())];
    const std::uint64_t mode = rng.next() % 5;
    if (mode == 0 && !text.empty()) {
      text[rng.pick(text.size())] = static_cast<char>(rng.next() & 0xFF);
    } else if (mode == 1 && !text.empty()) {
      text.resize(rng.pick(text.size()));
    } else if (mode == 2) {
      text.insert(text.begin() + static_cast<std::ptrdiff_t>(rng.pick(text.size() + 1)),
                  static_cast<char>(rng.next() & 0xFF));
    } else if (mode == 3 && !text.empty()) {
      text.erase(rng.pick(text.size()), 1);
    } else {
      text.assign(rng.pick(1024), '\0');
      for (char& c : text) c = static_cast<char>(rng.next() & 0xFF);
    }

    const tempo::HttpRequestParse parsed = tempo::parse_http_request(text);
    switch (parsed.state) {
      case tempo::HttpRequestParse::State::Complete: {
        ++complete;
        if (parsed.consumed > text.size()) {
          std::fprintf(stderr, "consumed beyond buffer\n");
          return 1;
        }
        if (parsed.request.path.empty() || parsed.request.path.front() != '/') {
          std::fprintf(stderr, "invalid path accepted\n");
          return 1;
        }
        if (parsed.request.body.size() > tempo::limits::kMaxHttpBodyBytes) {
          std::fprintf(stderr, "body exceeds limit\n");
          return 1;
        }
        tempo::HttpResponse response;
        response.status = 200;
        response.body = "ok";
        const std::string serialized = tempo::http_serialize_response(response, true);
        if (serialized.find("Content-Length: 2\r\n") == std::string::npos) {
          std::fprintf(stderr, "serializer lost body length\n");
          return 1;
        }
        break;
      }
      case tempo::HttpRequestParse::State::Incomplete:
        ++incomplete;
        break;
      case tempo::HttpRequestParse::State::Error:
        ++errors;
        if (parsed.error.empty() || parsed.error_status < 400 ||
            parsed.error_status > 599) {
          std::fprintf(stderr, "bad error metadata\n");
          return 1;
        }
        break;
    }
  }
  std::printf("fuzz_http: %llu iterations, %llu complete, %llu incomplete, %llu errors, seed=%llu\n",
              static_cast<unsigned long long>(iterations),
              static_cast<unsigned long long>(complete),
              static_cast<unsigned long long>(incomplete),
              static_cast<unsigned long long>(errors),
              static_cast<unsigned long long>(seed));
  return 0;
}
