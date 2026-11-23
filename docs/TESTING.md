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
| Virtual environment | `rng_test`, `clock_test`, `format_test`, `locale_test` |
| Runtime | `task_test`, `scheduler_test`, `cancellation_test`, `limits_test`, `property_test`, `soak_test` |
| Capabilities | `capability_test`, `system_capability_test`, `socket_capability_test` |
| Record format | `crc32_test`, `string_table_test`, `event_test`, `record_roundtrip_test`, `record_recovery_test`, `record_property_test`, `record_fuzz_test`, `record_run_test` |
| Replay | `replay_test`, `replay_determinism` (100 runs), `replay_capability_test`, `mutation_test`, `diff_test`, `diff_property_test`, `build_id_test` |
| Debugger | `debug_test`, `breakpoint_test` |
| UI | `json_test`, `json_property_test`, `http_test`, `http_property_test`, `workbench_test`, `workbench_memory_test`, `workbench_archive_test`, `workbench_recovery_test`, `async_job_test`, `fs_permissions_test`, `integration_test` |
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
- malformed JSON/HTTP input fails without crashing;
- locale changes cannot alter watch or JSON number text;
- long replay batches complete on the job worker while polling works;
- archived records reload by basename, and path traversal is rejected;
- soak loops exercise 500 replays, 100 serializations and 100 record/delete
  cycles;
- installed packages are consumable through `find_package(tempo)`.

The suite is **45 ctest targets** in the default build (three of them fuzz smoke
tests). The UI smoke and install smoke are intentionally separate scripts because
they start processes or install files.

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
HTTP server integration test and the background replay worker. Coverage builds
use `-DTEMPO_COVERAGE=ON`.

## Fuzzers

```sh
./build/fuzz_record --iterations 20000
./build/fuzz_json --iterations 20000
./build/fuzz_http --iterations 20000
```

The default fuzzers are deterministic (seedable) mutation harnesses over the
production parsers and are also ctest smoke tests at 500 iterations. Optional
coverage-guided harnesses live in `fuzz/libfuzzer_*.cpp` and build with:

```sh
cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DTEMPO_LIBFUZZER=ON \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
cmake --build build-fuzz --target libfuzzer_record libfuzzer_json
./build-fuzz/libfuzzer_record -max_total_time=30 -max_len=4096
```

Apple Clang lacks the compiler-rt libFuzzer runtime; CMake reports that clearly.
The deterministic fuzzers need no special toolchain and run under ASan.

## End-to-end smoke scripts

```sh
./scripts/ui_smoke.sh       # launcher, token assets, API, headless DOM
./scripts/install_smoke.sh  # install prefix + find_package consumer
./scripts/ui_smoke.sh       # optional Chromium self-test probe
```

`ui_smoke.sh` starts the real launcher on an ephemeral port, verifies tokenized
assets, records, replays 100 times, mutates one decision, checks a frame, and—if
Chromium is installed—loads the page with `selftest=1&nostream=1` and asserts the
DOM reached the demo record. `install_smoke.sh` installs to a temp prefix and
builds a separate consumer against `tempo::tempo`.

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
