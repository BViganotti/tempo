// Minimal embeddable Tempo example: record a cooperative run, save it, reload
// it, and replay it 100 times. No UI, no third-party libraries.
//
// Build: cmake --build build --target tempo_example
// Run:   ./build/tempo_example [path.tmpo]

#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "tempo.h"

namespace {

struct DemoHost final : tempo::RunHost {
  std::map<std::string, std::string> files;
  std::map<std::string, std::string> env;
  std::size_t events = 0;

  tempo::TaskId choose_ready(const std::vector<tempo::TaskId>& ready,
                             std::uint64_t draw_index, std::uint64_t) override {
    return ready.empty() ? tempo::kNoTask : ready[draw_index % ready.size()];
  }
  tempo::CapResult file_read(const std::string& path) override {
    tempo::CapResult result;
    auto it = files.find(path);
    if (it == files.end()) {
      result.error = "no such file";
      return result;
    }
    result.ok = true;
    result.data = it->second;
    result.num = it->second.size();
    return result;
  }
  tempo::CapResult file_write(const std::string& path, const std::string& data) override {
    files[path] = data;
    tempo::CapResult result;
    result.ok = true;
    result.num = data.size();
    return result;
  }
  tempo::CapResult env_get(const std::string& name) override {
    tempo::CapResult result;
    auto it = env.find(name);
    if (it == env.end()) {
      result.error = "not set";
      return result;
    }
    result.ok = true;
    result.data = it->second;
    return result;
  }
  tempo::SocketConnectResult socket_connect(const std::string&, std::uint16_t) override {
    return {};
  }
  tempo::CapResult socket_send(std::uint32_t, const std::string&) override { return {}; }
  tempo::CapResult socket_recv(std::uint32_t, std::uint32_t) override { return {}; }
  tempo::CapResult socket_close(std::uint32_t) override { return {}; }
  void emit(tempo::Event) override { ++events; }
};

struct State {
  std::int64_t counter = 0;
};

tempo::Task worker(tempo::Context ctx, std::shared_ptr<State> state) {
  for (int i = 0; i < 4; ++i) {
    const std::int64_t local = state->counter;
    ctx.watch("local", local);
    co_await ctx.yield();
    state->counter = local + 1;
    ctx.watch("counter", state->counter);
  }
}

tempo::Task scenario(tempo::Context ctx) {
  auto state = std::make_shared<State>();
  const tempo::TaskId a = ctx.spawn("alpha", [state](tempo::Context c) {
    return worker(c, state);
  });
  const tempo::TaskId b = ctx.spawn("beta", [state](tempo::Context c) {
    return worker(c, state);
  });
  const bool a_ok = co_await ctx.join(a);
  const bool b_ok = co_await ctx.join(b);
  const tempo::CapResult file = ctx.read_file("config.txt");
  ctx.watch("config", file.data);
  const tempo::CapResult mode = ctx.env_get("DEMO_MODE");
  ctx.watch("mode", mode.data);
  ctx.watch("random", static_cast<std::int64_t>(ctx.random_u64() >> 60));
  ctx.note("final", std::to_string(state->counter));
  ctx.note("joined", a_ok && b_ok ? "both" : "degraded");
  ctx.log("example scenario complete");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string path = argc > 1 ? argv[1] : "tempo-demo.tmpo";

  DemoHost host;
  host.files["config.txt"] = "mode=demo\n";
  host.env["DEMO_MODE"] = "record";
  tempo::RecordRunOptions options;
  options.seed = 7;
  tempo::RecordRunResult recorded =
      tempo::record_run("example", scenario, host, options);
  if (!recorded.ok) {
    std::fprintf(stderr, "record failed: %s\n", recorded.error.c_str());
    return 1;
  }
  std::string error;
  if (!tempo::write_record_file(recorded.record, path, &error)) {
    std::fprintf(stderr, "save failed: %s\n", error.c_str());
    return 1;
  }
  const tempo::LoadOutcome loaded = tempo::load_record_file(path);
  if (!loaded.ok) {
    std::fprintf(stderr, "load failed: %s\n", loaded.error.c_str());
    return 1;
  }

  const tempo::ReplayBatchReport batch =
      tempo::replay_many(loaded.record, scenario, 100);
  std::printf("Tempo example\n");
  std::printf("  record:  %s (%zu events, hash %s)\n", path.c_str(),
              loaded.record.events.size(), loaded.record.hash_hex().c_str());
  std::printf("  replays: %u, identical=%s, stable-hashes=%s\n", batch.runs,
              batch.all_identical ? "yes" : "no",
              batch.all_hashes_equal ? "yes" : "no");
  return batch.ok && batch.all_identical ? 0 : 1;
}
