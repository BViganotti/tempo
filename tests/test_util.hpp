#pragma once

// Minimal, dependency-free test harness used by every Tempo test binary.
// Each test file includes this header exactly once; it provides main().

#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace tempo::test {

struct Case {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}

inline int& failures() {
  static int f = 0;
  return f;
}

inline std::string& current_case() {
  static std::string c;
  return c;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) {
    registry().push_back({name, std::move(fn)});
  }
};

template <class T>
std::string show(const T& value) {
  if constexpr (std::is_enum_v<T>) {
    return std::to_string(static_cast<long long>(value));
  } else if constexpr (requires(std::ostream& os, const T& v) { os << v; }) {
    std::ostringstream os;
    os << value;
    return os.str();
  } else {
    return "<value>";
  }
}

inline void fail(const char* what, const char* file, int line, const std::string& detail) {
  ++failures();
  std::fprintf(stderr, "FAIL %s\n  at %s:%d\n  %s\n", current_case().c_str(), file, line,
               what);
  if (!detail.empty()) {
    std::fprintf(stderr, "  %s\n", detail.c_str());
  }
}

inline bool check(bool ok, const char* what, const char* file, int line) {
  if (!ok) {
    fail(what, file, line, {});
  }
  return ok;
}

template <class A, class B>
bool check_eq(const A& a, const B& b, const char* ea, const char* eb, const char* file,
              int line) {
  if (a == b) {
    return true;
  }
  fail((std::string(ea) + " == " + eb).c_str(), file, line,
       "left: " + show(a) + "  right: " + show(b));
  return false;
}

inline bool check_near(double a, double b, double eps, const char* ea, const char* eb,
                       const char* file, int line) {
  if (std::fabs(a - b) <= eps) {
    return true;
  }
  fail((std::string(ea) + " ~= " + eb).c_str(), file, line,
       "left: " + show(a) + "  right: " + show(b));
  return false;
}

inline int run_all() {
  int run = 0;
  for (auto& c : registry()) {
    current_case() = c.name;
    std::printf("[ RUN  ] %s\n", c.name.c_str());
    int before = failures();
    try {
      c.fn();
    } catch (const std::exception& e) {
      fail("uncaught exception", __FILE__, __LINE__, e.what());
    } catch (...) {
      fail("uncaught non-standard exception", __FILE__, __LINE__, {});
    }
    ++run;
    if (failures() == before) {
      std::printf("[  OK  ] %s\n", c.name.c_str());
    } else {
      std::printf("[ FAIL ] %s\n", c.name.c_str());
    }
  }
  std::printf("\n%d test cases, %d failures\n", run, failures());
  return failures() == 0 ? 0 : 1;
}

}  // namespace tempo::test

#define TEST(name)                                                     \
  static void tempo_test_##name();                                     \
  static ::tempo::test::Registrar tempo_reg_##name(#name, tempo_test_##name); \
  static void tempo_test_##name()

#define CHECK(cond) ::tempo::test::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) \
  ::tempo::test::check_eq((a), (b), #a, #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, eps) \
  ::tempo::test::check_near((a), (b), (eps), #a, #b, __FILE__, __LINE__)
#define CHECK_THROWS(expr)                                                     \
  do {                                                                         \
    bool tempo_threw = false;                                                   \
    try {                                                                       \
      (void)(expr);                                                            \
    } catch (...) {                                                            \
      tempo_threw = true;                                                       \
    }                                                                           \
    CHECK(tempo_threw);                                                         \
  } while (0)

int main() {
  return ::tempo::test::run_all();
}
