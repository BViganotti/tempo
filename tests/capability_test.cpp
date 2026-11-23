#include "host_test.hpp"
#include "test_util.hpp"
#include "tempo/runtime.hpp"

using namespace tempo;

namespace {

Task reader(Context ctx) {
  const CapResult good = ctx.read_file("data/input.txt");
  ctx.watch("good_ok", good.ok);
  ctx.watch("good_data", good.data);
  ctx.watch("good_size", static_cast<std::int64_t>(good.data.size()));

  const CapResult missing = ctx.read_file("data/missing.txt");
  ctx.watch("missing_ok", missing.ok);
  ctx.watch("missing_err", missing.error);

  const CapResult written = ctx.write_file("data/out.txt", "hello");
  ctx.watch("write_ok", written.ok);

  const CapResult mode = ctx.env_get("TEMPO_MODE");
  ctx.watch("mode", mode.data);

  const CapResult absent = ctx.env_get("TEMPO_ABSENT");
  ctx.watch("absent_ok", absent.ok);

  ctx.watch("random", static_cast<std::int64_t>(ctx.random_u64() >> 56));
  co_return;
}

Task socket_user(Context ctx) {
  const SocketConnectResult conn = ctx.connect("127.0.0.1", 7000);
  ctx.watch("connected", conn.ok);
  if (!conn.ok) {
    co_return;
  }
  const CapResult sent = ctx.send(conn.handle, "ping");
  ctx.watch("sent", static_cast<std::int64_t>(sent.num));
  const CapResult echoed = ctx.recv(conn.handle, 128);
  ctx.watch("echo", echoed.data);
  const CapResult closed = ctx.close_socket(conn.handle);
  ctx.watch("closed", closed.ok);
}

Task limit_scenario(Context ctx) {
  const std::string huge(limits::kMaxFileBytes + 1, 'x');
  const CapResult big = ctx.read_file("big.bin");
  ctx.watch("big_ok", big.ok);
  ctx.watch("big_err", big.error);

  const CapResult long_path = ctx.read_file(std::string(limits::kMaxPathBytes + 1, 'p'));
  ctx.watch("path_ok", long_path.ok);

  const CapResult big_write = ctx.write_file("out.bin", huge);
  ctx.watch("write_ok", big_write.ok);

  const CapResult big_send = ctx.send(1, huge);
  ctx.watch("send_ok", big_send.ok);

  const CapResult big_recv = ctx.recv(1, 20000);
  ctx.watch("recv_ok", big_recv.ok);
  co_return;
}

}  // namespace

TEST(records_file_env_and_random_results) {
  test::TestHost host;
  host.files["data/input.txt"] = "tempo";
  host.envs["TEMPO_MODE"] = "record";
  Scheduler scheduler(host, {.seed = 4242});
  scheduler.spawn_root("reader", reader);
  const RunSummary summary = scheduler.run();
  CHECK(summary.ok);
  CHECK_EQ(host.count(EventType::FileRead), 2u);
  CHECK_EQ(host.count(EventType::FileWrite), 1u);
  CHECK_EQ(host.count(EventType::EnvGet), 2u);
  CHECK_EQ(host.count(EventType::Rng), 1u);

  bool good = false;
  bool missing = false;
  bool mode = false;
  for (const auto& event : host.events) {
    if (event.type == EventType::Watch && event.s1 == "good_data" && event.s2 == "tempo") {
      good = true;
    }
    if (event.type == EventType::Watch && event.s1 == "missing_ok" && event.s2 == "false") {
      missing = true;
    }
    if (event.type == EventType::Watch && event.s1 == "mode" && event.s2 == "record") {
      mode = true;
    }
  }
  CHECK(good);
  CHECK(missing);
  CHECK(mode);

  const Event* read = host.find(EventType::FileRead);
  CHECK(read != nullptr);
  CHECK_EQ(read->bytes, "tempo");
  CHECK_EQ(read->b, 5u);
  CHECK_EQ(read->a, 1u);

  const Event* write = host.find(EventType::FileWrite);
  CHECK(write != nullptr);
  CHECK_EQ(write->bytes, "hello");
  CHECK(host.files["data/out.txt"] == "hello");
}

TEST(records_socket_traffic) {
  test::TestHost host;
  Scheduler scheduler(host);
  scheduler.spawn_root("socket", socket_user);
  CHECK(scheduler.run().ok);
  CHECK_EQ(host.count(EventType::SocketConnect), 1u);
  const Event* connect = host.find(EventType::SocketConnect);
  CHECK(connect != nullptr);
  CHECK_EQ(connect->c, 1u);
  CHECK_EQ(connect->a, 7000u);
  const Event* recv = host.find(EventType::SocketRecv);
  CHECK(recv != nullptr);
  CHECK_EQ(recv->bytes, "ping");
  CHECK_EQ(host.count(EventType::SocketClose), 1u);
}

TEST(oversized_capability_inputs_fail_without_calling_the_host) {
  test::TestHost host;
  host.files["big.bin"] = std::string(limits::kMaxFileBytes + 10, 'y');
  Scheduler scheduler(host);
  scheduler.spawn_root("limits", limit_scenario);
  const RunSummary summary = scheduler.run();
  CHECK(summary.ok);

  int big_ok = -1;
  int path_ok = -1;
  int write_ok = -1;
  int send_ok = -1;
  int recv_ok = -1;
  for (const auto& event : host.events) {
    if (event.type == EventType::Watch) {
      if (event.s1 == "big_ok") {
        big_ok = event.s2 == "true" ? 1 : 0;
      } else if (event.s1 == "path_ok") {
        path_ok = event.s2 == "true" ? 1 : 0;
      } else if (event.s1 == "write_ok") {
        write_ok = event.s2 == "true" ? 1 : 0;
      } else if (event.s1 == "send_ok") {
        send_ok = event.s2 == "true" ? 1 : 0;
      } else if (event.s1 == "recv_ok") {
        recv_ok = event.s2 == "true" ? 1 : 0;
      }
    }
  }
  CHECK_EQ(big_ok, 0);
  CHECK_EQ(path_ok, 0);
  CHECK_EQ(write_ok, 0);
  CHECK_EQ(send_ok, 0);
  CHECK_EQ(recv_ok, 0);
  CHECK(host.files.find("out.bin") == host.files.end());
}

TEST(env_optional_helper) {
  test::TestHost host;
  host.envs["A"] = "1";
  Scheduler scheduler(host);
  auto scenario = [](Context ctx) -> Task {
    const auto a = ctx.env("A");
    const auto b = ctx.env("B");
    ctx.watch("a", a.value_or("none"));
    ctx.watch("b", b.value_or("none"));
    co_return;
  };
  auto root = [scenario](Context ctx) -> Task {
    ctx.spawn("child", scenario);
    co_await ctx.yield();
  };
  scheduler.spawn_root("env", root);
  CHECK(scheduler.run().ok);
  bool a = false;
  bool b = false;
  for (const auto& event : host.events) {
    if (event.type == EventType::Watch && event.s1 == "a" && event.s2 == "1") {
      a = true;
    }
    if (event.type == EventType::Watch && event.s1 == "b" && event.s2 == "none") {
      b = true;
    }
  }
  CHECK(a);
  CHECK(b);
}
