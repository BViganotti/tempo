#include <cstdlib>
#include <string>
#include <vector>

#include "temp_dir.hpp"
#include "test_util.hpp"
#include "tempo/runtime.hpp"
#include "tempo/system.hpp"

using namespace tempo;

namespace {

Task system_scenario(Context ctx) {
  const CapResult input = ctx.read_file("input.txt");
  ctx.watch("input_ok", input.ok);
  ctx.watch("input_data", input.data);

  const CapResult written = ctx.write_file("output.txt", "written-by-tempo");
  ctx.watch("write_ok", written.ok);

  const CapResult reread = ctx.read_file("output.txt");
  ctx.watch("reread", reread.data);

  const CapResult escape = ctx.read_file("../outside.txt");
  ctx.watch("escape_ok", escape.ok);
  ctx.watch("escape_error", escape.error);

  const CapResult absolute = ctx.read_file("/etc/hostname");
  ctx.watch("absolute_ok", absolute.ok);

  const CapResult env = ctx.env_get("TEMPO_SYSTEM_TEST");
  ctx.watch("env_ok", env.ok);
  ctx.watch("env_value", env.data);

  const CapResult missing = ctx.env_get("TEMPO_SYSTEM_MISSING");
  ctx.watch("missing_env", missing.ok);

  const CapResult big = ctx.read_file("big.txt");
  ctx.watch("big_ok", big.ok);
  ctx.watch("big_error", big.error);

  const CapResult symlink = ctx.read_file("link.txt");
  ctx.watch("symlink_ok", symlink.ok);
  co_return;
}

Task env_disabled(Context ctx) {
  const CapResult env = ctx.env_get("PATH");
  ctx.watch("env_ok", env.ok);
  co_return;
}

}  // namespace

TEST(system_host_reads_and_writes_inside_sandbox) {
  test::TempDir dir;
  CHECK(!dir.path().empty());
  CHECK(dir.write("input.txt", "hello tempo"));
  CHECK(dir.write("big.txt", std::string(limits::kMaxFileBytes + 1, 'z')));
  CHECK(dir.symlink("link.txt", "input.txt"));

  std::vector<Event> events;
  SystemCapabilityOptions options;
  options.data_root = dir.path();
  SystemHost host(options);
  host.set_event_sink([&events](Event event) { events.push_back(std::move(event)); });

  Scheduler scheduler(host);
  scheduler.spawn_root("system", system_scenario);
  const RunSummary summary = scheduler.run();
  CHECK(summary.ok);

  CHECK_EQ(dir.read("output.txt"), std::string("written-by-tempo"));

  std::string escape_error;
  bool escape_ok = true;
  bool absolute_ok = true;
  bool big_ok = true;
  bool symlink_ok = true;
  std::string env_value;
  for (const auto& event : events) {
    if (event.type == EventType::Watch) {
      if (event.s1 == "escape_ok") {
        escape_ok = event.s2 == "true";
      } else if (event.s1 == "escape_error") {
        escape_error = event.s2;
      } else if (event.s1 == "absolute_ok") {
        absolute_ok = event.s2 == "true";
      } else if (event.s1 == "big_ok") {
        big_ok = event.s2 == "true";
      } else if (event.s1 == "symlink_ok") {
        symlink_ok = event.s2 == "true";
      } else if (event.s1 == "env_value") {
        env_value = event.s2;
      }
    }
  }
  CHECK(!escape_ok);
  CHECK(!absolute_ok);
  CHECK(!big_ok);
  CHECK(!symlink_ok);

  std::size_t reads = 0;
  for (const auto& event : events) {
    if (event.type == EventType::FileRead) {
      ++reads;
      if (event.s1 == "input.txt") {
        CHECK_EQ(event.bytes, std::string("hello tempo"));
        CHECK_EQ(event.a, 1u);
      }
    }
  }
  CHECK_EQ(reads, 6u);
}

TEST(system_host_env_can_be_disabled) {
  SystemCapabilityOptions options;
  options.allow_env = false;
  SystemHost host(options);
  std::vector<Event> events;
  host.set_event_sink([&events](Event event) { events.push_back(std::move(event)); });
  Scheduler scheduler(host);
  scheduler.spawn_root("env-off", env_disabled);
  CHECK(scheduler.run().ok);
  bool disabled = false;
  for (const auto& event : events) {
    if (event.type == EventType::Watch && event.s1 == "env_ok") {
      disabled = event.s2 == "false";
    }
    if (event.type == EventType::EnvGet) {
      CHECK_EQ(event.s1, "PATH");
      CHECK_EQ(event.a, 0u);
      CHECK(event.s2.find("disabled") != std::string::npos);
    }
  }
  CHECK(disabled);
}

TEST(system_host_requires_a_root) {
  SystemHost host(SystemCapabilityOptions{});
  const CapResult result = host.file_read("anything.txt");
  CHECK(!result.ok);
  CHECK(result.error.find("data root") != std::string::npos);
}

TEST(path_policy_rejects_escapes) {
  CHECK(safe_relative_path_policy("a.txt"));
  CHECK(safe_relative_path_policy("nested/a.txt"));
  CHECK(safe_relative_path_policy("a-b_c.1.txt"));
  CHECK(!safe_relative_path_policy(""));
  CHECK(!safe_relative_path_policy("/etc/passwd"));
  CHECK(!safe_relative_path_policy("../x"));
  CHECK(!safe_relative_path_policy("a/../b"));
  CHECK(!safe_relative_path_policy("a/./b"));
  CHECK(!safe_relative_path_policy("a//b"));
  CHECK(!safe_relative_path_policy("a/"));
  CHECK(!safe_relative_path_policy(std::string("a\0b", 3)));
  CHECK(!safe_relative_path_policy(std::string(limits::kMaxPathBytes + 1, 'a')));
}

TEST(system_host_rejects_unsafe_write_paths) {
  test::TempDir dir;
  SystemHost host({.data_root = dir.path()});
  const CapResult result = host.file_write("../evil.txt", "x");
  CHECK(!result.ok);
  CHECK(!dir.exists("../evil.txt"));
}

TEST(system_host_rejects_non_loopback_sockets) {
  SystemHost host(SystemCapabilityOptions{});
  const SocketConnectResult result = host.socket_connect("example.com", 80);
  CHECK(!result.ok);
  CHECK(result.error.find("loopback") != std::string::npos);
}
