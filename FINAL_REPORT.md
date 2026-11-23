# Final report

Tempo is a deterministic record/replay workbench for cooperative C++ tasks,
delivered as an embeddable C++20 library plus a local browser workbench. This
report records what was built, how it was verified, and what remains outside the
scope.

## Definition of Done

| # | Requirement | Status |
|---:|---|---|
| 1 | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` succeeds | **Yes** |
| 2 | `cmake --build build -j` succeeds with zero warnings | **Yes** (`-Werror` build) |
| 3 | `ctest --test-dir build --output-on-failure` passes | **Yes, 37/37** |
| 4 | ASan/UBSan and TSan runs pass and are documented | **Yes, 37/37 each** (`docs/VERIFICATION.md`) |
| 5 | Launcher opens the local UI; the demo works | **Yes** (browser run below) |
| 6 | README, docs, CHANGELOG, LICENSE, LIMITATIONS and FINAL_REPORT exist | **Yes** |
| 7 | This report lists LOC, commits, test commands/results, benchmarks and gaps | **Yes** |
| 8 | Repo committed and pushed, or PUSH.md with exact commands | **Yes** (remains committed and pushed to `origin`) |
| 9 | No external dependency, no CI, no CLI-only product | **Yes** |

## Environment

- macOS 26.7.3, arm64
- Apple Clang 21.0.0 (Xcode 26)
- CMake 4.2.3, Ninja 1.13.0
- Release for tests/bench/fuzz; Debug for sanitizer builds

No third-party C++ libraries, package managers, vendored code, external fonts or
CDN assets are used. There is no `.github/workflows` or other CI configuration.
Pushing to the remote is the only network operation performed by development
tooling; the product never accesses the internet.

## What was delivered

- Cooperative C++20 task runtime: deterministic scheduler, virtual clock,
  seeded RNG, spawn/join, cancellation, absolute/relative sleeps, watch state,
  notes, bounded logs, task introspection.
- Capability-mediated file, environment, loopback socket and random effects with
  a sandboxed system provider and an OS-free replay provider.
- Portable v1 record container: metadata, deduplicated strings, per-event CRC,
  body/footer CRC, event index, strict and recovery loading, atomic writes.
- Recording with optional single-decision schedule mutation; replay with
  event-by-event comparison, first-divergence diagnostics, 100-run hash proof,
  batch reporting and record diff.
- Debugger over the event log: exact watch-state reconstruction with a bounded
  checkpoint budget, reverse stepping, breakpoints (type/task/text), timeline,
  divergence annotation.
- Local-only HTTP/SSE workbench with a random token, Host/Origin validation,
  CSP, no CORS, bounded parsers and buffers; original embedded HTML/CSS/JS.
- UI: record, import, export, delete, timeline canvas, event list, inspector,
  breakpoints, replay proof, altered-schedule replay, first-divergence view, run
  diff.
- Three built-in demo scenarios, an embeddable example with an in-memory host,
  three mutation fuzzers, and measured benchmarks.

## Tests

Commands:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Result: **37/37 tests passed**, build with zero warnings under
`-Wall -Wextra -Wpedantic -Werror -Wshadow -Wnon-virtual-dtor
-Woverloaded-virtual -Wunused -Wformat=2`.

Sanitizers:

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTEMPO_SANITIZE=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure   # 37/37

cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTEMPO_TSAN=ON
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure   # 37/37
```

ASan found a test-only lifetime bug; UBSan found a real out-of-range cast in the
JSON writer. Both were fixed and the full suites were rerun clean.

## Fuzzing

Deterministic mutation fuzzers over the production parsers (also ctest smoke
targets at 500 iterations):

```sh
./build/fuzz_record --iterations 20000
./build/fuzz_json   --iterations 20000
./build/fuzz_http   --iterations 20000
```

Recorded output:

```
fuzz_record: 20000 iterations, 14546 recovered, 178 strict-valid, seed=335597
fuzz_json: 20000 iterations, 3851 accepted, 16149 rejected, seed=12648430
fuzz_http: 20000 iterations, 6798 complete, 8732 incomplete, 4470 errors, seed=4277009102
```

These are not coverage-guided libFuzzer campaigns; they are seedable mutation
harnesses with invariant checks and no crash on malformed input.

## Benchmarks

`./build/tempo_bench` on the environment above:

| Benchmark | Result |
|---|---|
| Serialize 20k events ×10 | 110.25 ms, 1,814,073 ops/s, 95.3 MiB/s |
| Strict load ×10 | 84.08 ms, 2,378,629 ops/s, 124.9 MiB/s |
| Determinism hash ×100 | 213.59 ms, 9,363,550 events/s |
| JSON 1k-event page ×20 | 12.23 ms, 1,635,256 events/s, 107.3 MiB/s |
| File save + load ×5 | 103.95 ms, 1,924,047 events/s, 101.0 MiB/s |
| Record counter-race ×50 | 0.96 ms, 52,056 runs/s |
| Replay counter-race ×500 | 7.54 ms, 8,418,169 events/s |

Serialized 20k-event record: 1,101,280 bytes, hash `5e28fac214dc1a12`. The
counter race record has 127 events and hash `4478f29fe75c9d58`. These numbers are
machine-specific, not universal claims.

## Demo proof

- `counter-race` performs a read-modify-write with a yield between read and
  write. The recorded schedule loses updates, so the scenario's own check
  reports failure; replay reproduces the same 127-event stream and hash 100
  times.
- In the browser verification run, `Replay ×100` reported 100 runs, identical
  **yes**, hashes stable **yes**.
- Altering step 1 produced: “First divergence at event 7 — event mismatch”,
  recorded `step 1: run task 2` versus replayed `step 1: run task 3`.
- A `Watch` breakpoint stopped forward navigation at frame 9 on
  `watch local_read = 0` for task 2.
- The launcher auto-recorded on a cold data directory, served tokenized CSS/JS,
  connected SSE (status `LIVE`) and rendered four timeline lanes.

Verification details and exact outputs are in `docs/VERIFICATION.md` and
`docs/TROUBLESHOOTING.md`.

## LOC

Generated embedded assets are excluded. Production code includes the public
headers, library, launcher, example, fuzzers and benchmark.

| Area | LOC |
|---|---:|
| Public headers (`include/tempo`, umbrella) | 1,643 |
| Runtime/scheduler | 1,108 |
| Virtual clock/RNG | 111 |
| Capabilities | 472 |
| Record format/recorder | 1,268 |
| Replay/diff | 406 |
| Debugger | 297 |
| UI server/JSON/workbench | 2,915 |
| Scenarios | 187 |
| Launcher | 167 |
| Shared version source | 9 |
| **Production total** | **8,583** |
| Example/fuzz/bench tools | 748 |
| **Production + tools** | **9,331** |
| Tests | 5,639 |
| Embedded UI assets (excluded) | 1,302 |

Production + tools (9,331) is inside the 9–11k target. The test suite is larger
than the original 1.2k budget because it contains property, fuzz, sanitizer,
integration and end-to-end workbench coverage; it was not padded with filler.

## Commits

- **62 commits at delivery** (this report is the 62nd). The stated target of 80+
  was not reached; the 60-commit minimum is met. Commits are vertical slices:
  runtime, virtual environment, capabilities, record format, replay, debugger,
  UI, scenarios, hardening, docs and tests. Every commit was built and tested
  during development.
- Remote: <https://github.com/BViganotti/tempo> (public), pushed on `master`.

## Known gaps

These are honest and also in `LIMITATIONS.md`:

- Cooperative tasks only; no preemption, native thread recording, or reverse
  native execution. Unwatched locals are not reconstructed.
- Unmediated OS calls in scenario code are outside determinism; divergences are
  usually reported but not guaranteed to be caught at the call site.
- Replay requires the scenario code/name to be registered; the stock launcher
  only knows the three built-ins, so records from custom embedder registries are
  viewable but not replayable there.
- Records are unencrypted and CRC-protected, not signed. The token is in the
  browser URL; there is no TLS (loopback only).
- Import through HTTP is limited to 256 KiB and UI download to 4 MiB. Larger
  records are files under `<data-dir>/records/`.
- Timeline ticks are sampled for very large records; the event list is paged.
- Tested on macOS arm64 with Chromium; Linux and other browsers are not claimed.
- The fuzzers are deterministic mutation harnesses, not coverage-guided.
- Commit count is 62, below the aspirational 80+; no low-value commits were added
  to inflate the count.

## Reproduction

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/tempo_bench
./build/fuzz_record --iterations 20000
./build/tempo_example /tmp/tempo-example.tmpo
./build/tempo_workbench          # opens the browser
```

Licensed MIT. See `README.md`, `docs/`, `CHANGELOG.md`, `LIMITATIONS.md` and
`SECURITY.md`.
