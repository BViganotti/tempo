# Verification record

Environment for the recorded runs:

- macOS 26.7.3, arm64
- Apple Clang 21.0.0 (Xcode 26)
- CMake 4.2.3, Ninja 1.13.0
- Release build for ctest/bench/fuzz; Debug sanitizer builds
- Homebrew LLVM/Clang 20.1.8 for the optional libFuzzer harnesses

Every number and pass/fail below was produced by running the command on this
machine. Nothing is estimated.

## Release build and tests

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Result: build completed with zero warnings under
`-Wall -Wextra -Wpedantic -Werror -Wshadow -Wnon-virtual-dtor
-Woverloaded-virtual -Wunused -Wformat=2`; **45/45 tests passed**.

## ASan + UBSan

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTEMPO_SANITIZE=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

Result: **45/45 tests passed**. The sanitizer found two real issues during
development, both fixed before the final run:

1. `json::Value::dump` cast a large double to `int64_t` before range checking
   (UBSan).
2. A test iterated a member of a temporary parsed JSON value (ASan
   heap-use-after-free). Production code was unaffected, but the test was fixed.

The async-worker, archive, memory-budget and locale changes were all included in
this final run.

## Thread sanitizer

```sh
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTEMPO_TSAN=ON
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

Result: **45/45 tests passed**, including `async_job_test` (background replay
worker), `soak_test` and `integration_test` (HTTP accept/read/route/SSE loop).
The SSE broadcast queue and the replay job map are the only cross-thread state;
both are mutex-protected.

## Deterministic fuzzers

```sh
./build/fuzz_record --iterations 20000
./build/fuzz_json --iterations 20000
./build/fuzz_http --iterations 20000
```

Recorded output:

```
fuzz_record: 20000 iterations, 14546 recovered, 178 strict-valid, seed=335597
fuzz_json: 20000 iterations, 3851 accepted, 16149 rejected, seed=12648430
fuzz_http: 20000 iterations, 6798 complete, 8732 incomplete, 4470 errors, seed=4277009102
```

Each fuzzer is also a ctest smoke target at 500 iterations.

## Optional coverage-guided libFuzzer

Apple Clang does not ship compiler-rt's libFuzzer. With Homebrew LLVM:

```sh
cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DTEMPO_LIBFUZZER=ON \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer"
cmake --build build-fuzz --target libfuzzer_record libfuzzer_json
./build-fuzz/libfuzzer_record -max_total_time=4 -runs=1000 -max_len=4096
./build-fuzz/libfuzzer_json -max_total_time=4 -runs=1000 -max_len=4096
```

Smoke results: record harness ran 1,000 executions (54 coverage counters loaded,
no crash); JSON harness ran 1,000 executions (124 edges reached, no crash). The
default deterministic fuzzers remain the ASan-covered path; the libFuzzer target
is optional because the runtime is toolchain-dependent.

## Benchmarks

```sh
./build/tempo_bench
```

Recorded Release output:

```
record: 20000 events, 5e28fac214dc1a12
serialize x10                      113.43 ms       1763194 ops/s       92.6 MiB/s
serialized size: 1101280 bytes
strict load x10                     84.48 ms       2367516 ops/s      124.3 MiB/s
event hash x100                    215.78 ms       9268490 ops/s
JSON event page x20                 12.25 ms       1633259 ops/s      107.2 MiB/s
file save + load x5                103.41 ms       1934081 ops/s      101.6 MiB/s
record counter-race x50              1.00 ms         50232 ops/s
replay counter-race x500             7.57 ms       8383160 ops/s
counter-race replay: 127 events/run, hash 4478f29fe75c9d58
```

These numbers measure a 20,000-event synthetic record and the built-in race;
they are not universal performance claims.

## UI and install smoke

```sh
./scripts/ui_smoke.sh
./scripts/install_smoke.sh
```

`ui_smoke` starts the real launcher on an ephemeral port, verifies tokenized
assets, records a run, replays it 100 times, mutates one decision, reads a
frame, and loads the page in headless Chromium with `selftest=1&nostream=1` to
assert the DOM reached `counter-race` and rendered the timeline, filters and
archive controls. Recorded result:

```
ui_smoke: OK (record 1, 100 identical replays, mutation divergence found)
```

`install_smoke` installs to a temporary prefix and builds a separate consumer
against `tempo::tempo`:

```
install_smoke: OK (Tempo 0.2.0 (record v1))
```

Additional browser verification (Orca/Chromium at 1126×900):

- status changes to `LIVE` over SSE;
- timeline renders four lanes (`system`, `t1 counter-race`, `t2 alpha`,
  `t3 beta`);
- event table renders all 127 events;
- `Replay ×100` reports 100 runs, identical **yes**, hashes stable **yes**,
  replay/expected hash `4478f29f`, build id matches;
- altering step 1 reports “First divergence at event 7 — event mismatch” with
  recorded `step 1: run task 2` vs replayed `step 1: run task 3`;
- type filter `Watch` shows `28 shown, 28 matching of 127`;
- a `Watch` breakpoint stops at frame 9 on `watch local_read = 0`, task 2.

## Checks not automated

- Real browser compatibility beyond Chromium was not tested; the UI uses
  standard `fetch`, `EventSource` and canvas.
- Linux builds were not exercised; the code uses POSIX APIs.
- The libFuzzer harnesses require a non-Apple Clang toolchain and were run as
  short smoke campaigns, not long fuzzing sessions.
- Benchmarks are single-run, machine-specific measurements.
