# Testing and verification

Tempo has no third-party test framework. `tests/test_util.hpp` provides a small
deterministic harness, and every test binary links the real library.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The suite covers:

| Area | Tests |
|---|---|
| Version/build | `version_test` |
| Virtual environment | `rng_test`, `clock_test` |
| Runtime | `task_test`, `scheduler_test`, `cancellation_test`, `limits_test`, `property_test` |
| Capabilities | `capability_test`, `system_capability_test`, `socket_capability_test` |
| Record format | `crc32_test`, `string_table_test`, `event_test`, `record_roundtrip_test`, `record_recovery_test`, `record_fuzz_test`, `record_run_test` |
| Replay | `replay_test`, `replay_determinism` (100 runs), `mutation_test`, `diff_test` |
| Debugger | `debug_test`, `breakpoint_test` |
| UI | `json_test`, `http_test`, `workbench_test`, `integration_test` |
| Scenarios | `scenario_test` |
| Fuzz smoke | `fuzz_record_smoke`, `fuzz_json_smoke`, `fuzz_http_smoke` |

Notable properties enforced by tests:

- recording the same seed twice produces identical events and hashes;
- 100 replays are event-identical and hashes are stable;
- replay answers file, environment and socket capabilities without touching the
  OS (fixtures and servers are deleted before replay);
- reverse stepping reconstructs the same watch state as a brute-force scan at
  every index, with and without checkpoints;
- truncated records recover only a valid event prefix; strict mode rejects them;
- mutated records never crash the parser and, when valid, re-serialize stably;
- the HTTP server enforces token, Host, Origin, methods, content types and
  buffer limits, and streams SSE through the real workbench;
- malformed JSON/HTTP input fails without crashing.

## Sanitizers

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTEMPO_SANITIZE=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure

cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTEMPO_TSAN=ON
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

ASan+UBSan and TSan results are recorded in `FINAL_REPORT.md`. TSan covers the
HTTP server integration test, which runs the accept/read/route/SSE loop on a
thread and drives it from clients.

## Fuzzers

```sh
./build/fuzz_record --iterations 20000
./build/fuzz_json --iterations 20000
./build/fuzz_http --iterations 20000
```

The fuzzers are deterministic (seedable) mutation harnesses over the production
parsers, not coverage-guided libFuzzer. Each is also run as a ctest smoke test at
500 iterations. Sanitizer builds turn memory errors into test failures.

## Benchmarks

```sh
./build/tempo_bench
```

All numbers are measured at runtime (serialize/load/hash/JSON/file I/O, 50 record
runs, 500 replays). No numbers are embedded in the source. Results for the build
machine are in `FINAL_REPORT.md`.

## Example

```sh
./build/tempo_example /tmp/tempo-example.tmpo
```

Records a scenario through an in-memory host, saves a portable record, reloads it
and verifies 100 identical replays.
