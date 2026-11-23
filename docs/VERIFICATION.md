# Verification record

Environment for the recorded runs:

- macOS 26.7.3, arm64
- Apple Clang 21.0.0 (Xcode 26)
- CMake 4.2.3, Ninja 1.13.0
- Release build for ctest/bench/fuzz; Debug sanitizer builds

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
-Woverloaded-virtual -Wunused -Wformat=2`; **35/35 tests passed**.

## ASan + UBSan

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTEMPO_SANITIZE=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

Result: **35/35 tests passed**. The sanitizer found two real issues during
development, both fixed before the final run:

1. `json::Value::dump` cast a large double to `int64_t` before range checking
   (UBSan).
2. A test iterated a member of a temporary parsed JSON value (ASan
   heap-use-after-free). Production code was unaffected, but the test was fixed.

## Thread sanitizer

```sh
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTEMPO_TSAN=ON
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure
```

Result: **35/35 tests passed**, including `integration_test`, which runs the
HTTP accept/read/route/SSE loop on a thread and drives it from clients. The SSE
broadcast queue is the only cross-thread state and is mutex-protected.

## Fuzzers (20,000 iterations each, default seeds)

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

## Benchmarks

```sh
./build/tempo_bench
```

Recorded output (Release):

```
record: 20000 events, 5e28fac214dc1a12
serialize x10                      110.25 ms       1814073 ops/s       95.3 MiB/s
serialized size: 1101280 bytes
strict load x10                     84.08 ms       2378629 ops/s      124.9 MiB/s
event hash x100                    213.59 ms       9363550 ops/s
JSON event page x20                 12.23 ms       1635256 ops/s      107.3 MiB/s
file save + load x5                103.95 ms       1924047 ops/s      101.0 MiB/s
record counter-race x50              0.96 ms         52056 ops/s
replay counter-race x500             7.54 ms       8418169 ops/s
counter-race replay: 127 events/run, hash 4478f29fe75c9d58
```

These numbers measure a 20,000-event synthetic record and the built-in race;
they are not universal performance claims. Replay of the 127-event demo is
microseconds; most wall time in the UI path is HTTP plus JSON, not scheduling.

## UI verification

The launcher was started headlessly:

```sh
./build/tempo_workbench --no-open --data-dir /tmp/tempo-ui
```

The printed URL was opened in a Chromium headless tab (via Orca's browser
automation) at 1126×900. Observed:

- the page auto-recorded `counter-race` on the empty data directory;
- status changed to `LIVE` over the SSE stream;
- the timeline rendered four lanes (`system`, `t1 counter-race`, `t2 alpha`,
  `t3 beta`) with event ticks;
- the event table showed all 127 events and the inspector showed frame 0 with
  no watch variables (correct for `ScenarioStart`);
- clicking `Replay ×100` produced: 100 runs, identical **yes**, hashes stable
  **yes**, replay/expected hash `4478f29f`, 1.56 ms elapsed in the page;
- setting “Alter step” to 1 and replaying produced:
  “First divergence at event 7 — event mismatch”, recorded `step 1: run task 2`
  vs replayed `step 1: run task 3`, and the UI jumped to frame 8/127;
- adding a `Watch` breakpoint and pressing `Breakpoint ▶` stopped at frame 9 on
  `watch local_read = 0`, task 2;
- the styled asset response was verified (CSS and JS carry the token injected
  into the index document).

A screenshot captured after these actions showed the dark workbench layout,
timeline, event table, inspector, and the mutation controls. The screenshot was
for verification only and is not shipped.

## Checks not automated

- Real browser compatibility beyond Chromium was not tested; the UI uses
  standard `fetch`, `EventSource` and canvas.
- Linux builds were not exercised; the code uses POSIX APIs.
- The fuzzers are deterministic mutation harnesses, not coverage-guided
  campaigns.
