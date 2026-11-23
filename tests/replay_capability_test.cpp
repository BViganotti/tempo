#include "host_test.hpp"
#include "test_util.hpp"
#include "tempo/replay.hpp"
#include "tempo/record_run.hpp"

using namespace tempo;

namespace {

Task read_a(Context ctx) {
  const CapResult result = ctx.read_file("a.txt");
  ctx.watch("data", result.data);
  ctx.watch("ok", result.ok);
  co_return;
}

Task read_b(Context ctx) {
  const CapResult result = ctx.read_file("b.txt");
  ctx.watch("data", result.data);
  ctx.watch("ok", result.ok);
  co_return;
}

Task env_a(Context ctx) {
  const CapResult result = ctx.env_get("TEMPO_A");
  ctx.watch("value", result.data);
  co_return;
}

Task env_b(Context ctx) {
  const CapResult result = ctx.env_get("TEMPO_B");
  ctx.watch("value", result.data);
  co_return;
}

Task write_a(Context ctx) {
  const CapResult result = ctx.write_file("a.txt", "payload");
  ctx.watch("ok", result.ok);
  co_return;
}

Task write_b(Context ctx) {
  const CapResult result = ctx.write_file("b.txt", "payload");
  ctx.watch("ok", result.ok);
  co_return;
}

}  // namespace

TEST(different_file_call_diverges_at_the_read_event) {
  test::TestHost caps;
  caps.files["a.txt"] = "alpha";
  caps.files["b.txt"] = "beta";
  const RecordRunResult recorded = record_run("files", read_a, caps);
  CHECK(recorded.ok);

  const ReplayReport replay = replay_record(recorded.record, read_b);
  CHECK(replay.ok);
  CHECK(!replay.identical);
  CHECK(replay.first_divergence.found);
  CHECK(replay.first_divergence.has_expected);
  CHECK(replay.first_divergence.has_actual);
  CHECK_EQ(replay.first_divergence.expected.type, EventType::FileRead);
  CHECK_EQ(replay.first_divergence.actual.type, EventType::FileRead);
  CHECK_EQ(replay.first_divergence.expected.s1, std::string("a.txt"));
  CHECK_EQ(replay.first_divergence.actual.s1, std::string("b.txt"));
  CHECK(replay.first_divergence.reason.find("mismatch") != std::string::npos);
}

TEST(different_environment_name_diverges) {
  test::TestHost caps;
  caps.envs["TEMPO_A"] = "one";
  caps.envs["TEMPO_B"] = "two";
  const RecordRunResult recorded = record_run("env", env_a, caps);
  CHECK(recorded.ok);
  const ReplayReport replay = replay_record(recorded.record, env_b);
  CHECK(!replay.identical);
  CHECK_EQ(replay.first_divergence.expected.type, EventType::EnvGet);
  CHECK_EQ(replay.first_divergence.expected.s1, std::string("TEMPO_A"));
  CHECK_EQ(replay.first_divergence.actual.s1, std::string("TEMPO_B"));
}

TEST(different_write_path_diverges) {
  test::TestHost caps;
  const RecordRunResult recorded = record_run("writes", write_a, caps);
  CHECK(recorded.ok);
  const ReplayReport replay = replay_record(recorded.record, write_b);
  CHECK(!replay.identical);
  CHECK_EQ(replay.first_divergence.expected.type, EventType::FileWrite);
  CHECK_EQ(replay.first_divergence.expected.s1, std::string("a.txt"));
  CHECK_EQ(replay.first_divergence.actual.s1, std::string("b.txt"));
}

TEST(replay_of_the_same_capability_sequence_stays_identical) {
  test::TestHost caps;
  caps.files["a.txt"] = "alpha";
  const RecordRunResult recorded = record_run("files", read_a, caps);
  CHECK(recorded.ok);
  const ReplayReport replay = replay_record(recorded.record, read_a);
  CHECK(replay.ok);
  CHECK(replay.identical);
}
