# Final report

Tempo 0.2.0 is a deterministic record/replay workbench for cooperative C++ tasks:
an embeddable C++20 library plus a loopback browser workbench. This report records
what was built, what was verified, and what remains outside the product’s scope.

## Definition of Done

| # | Requirement | Status |
|---:|---|---|
| 1 | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` succeeds | **Yes** (clean `build-final` run) |
| 2 | `cmake --build build -j` succeeds with zero warnings | **Yes** (0 `warning:` lines under `-Werror`) |
| 3 | `ctest --test-dir build --output-on-failure` passes | **Yes, 45/45** |
| 4 | ASan/UBSan and TSan runs pass and are documented | **Yes, 45/45 each** (`docs/VERIFICATION.md`) |
| 5 | Launcher opens the local UI; the demo works | **Yes** (HTTP/API + headless Chromium + Orca visual runs) |
| 6 | README, docs, CHANGELOG, LICENSE, LIMITATIONS and FINAL_REPORT exist | **Yes** |
| 7 | This report lists LOC, commits, test commands/results, benchmarks and gaps | **Yes** |
| 8 | Repo is committed and pushed, or PUSH.md contains exact push commands | **Yes** (public `BViganotti/tempo`, `master`) |
| 9 | No external dependency, no CI, no CLI-only product | **Yes** |

Environment: macOS 26.7.3 arm64, Apple Clang 21.0.0, CMake 4.2.3, Ninja 1.13.0.
No third-party C++ code, package manager, vendored source, CDN, external font or
runtime internet access. No `.github/workflows` or other CI configuration exists.

## What was delivered

### Deterministic core

- C++20 cooperative tasks, deterministic scheduler, virtual clock, seeded
  xoshiro256** RNG, task names/ids, joins, cooperative cancellation,
  absolute/relative sleeps, watch state, notes and bounded logs.
- Capability-mediated file, environment, loopback socket and random effects with
  a sandboxed system provider and an OS-free replay provider.
- Portable v1 record format: metadata, deduplicated string table, per-event CRC,
  body/footer CRC, on-disk index, strict loading, truncation recovery, atomic
  durable writes.
- Recording with optional one-decision schedule mutation; replay with
  event-by-event comparison, first-divergence diagnostics, 100-run hash proof,
  batch reporting and structural record diff.
- Debugger over the event log: exact watch-state reconstruction with a bounded
  checkpoint estimate, forward/reverse stepping, type/task/text breakpoints,
  timeline, divergence annotation.

### Workbench and operations

- Loopback HTTP/SSE server with random per-session token, Host/Origin checks,
  CSP, no CORS, bounded parsers/buffers, SSE keepalive and shutdown.
- Original embedded HTML/CSS/JS: timeline canvas, paged/filterable events,
  inspector, keyboard-accessible rows, breakpoints, replay proof, altered
  schedule, first divergence, run diff, import/export/delete, archived records.
- **Global memory budget** (`--max-memory-mb`, default 256 MiB) with oldest-first
  memory eviction, safe on-disk archive and reload by basename; oversized single
  records are rejected instead of silently dropped.
- **Background replay jobs**: long batches run on one bounded worker; the UI polls
  `/api/jobs/{id}` and remains responsive. Job map and SSE queues are locked.
- **Build fingerprints**: generated SHA-256 of built-in scenario sources stamps
  records; replay reports build-id mismatches without refusing to run.
- **Locale independence**: watch doubles, JSON numbers and clock text use
  explicit classic-locale formatting.
- Full-width 64-bit seeds, stable record `uid`, full-hash filenames, 64 MiB
  record I/O limits, owner-only directories/files, safe archive loading.
- Installable package: launcher, library, headers, docs, export targets and a
  relocatable `find_package(tempo)` config.

### Verification tools

- 45 ctest targets plus ASan/UBSan, TSan, soak loops, a UI smoke script, an
  install smoke script, deterministic record/JSON/HTTP fuzzers and optional
  coverage-guided libFuzzer harnesses.
- All primary engineering gaps identified in the prior review are addressed in
  code and tests; remaining boundaries are architectural non-goals or explicitly
  documented limits (see below).

## Test commands and results

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

**45/45 passed**, zero build warnings.

```sh
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTEMPO_SANITIZE=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure   # 45/45

cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTEMPO_TSAN=ON
cmake --build build-tsan -j
ctest --test-dir build-tsan --output-on-failure   # 45/45
```

TSan includes the background replay worker, archive/memory paths, HTTP/SSE
integration and soak tests. ASan/UBSan include locale, permissions, async jobs
and large-import tests.

## Fuzzing

Deterministic mutation fuzzers, 20,000 iterations each:

```
fuzz_record: 20000 iterations, 14546 recovered, 178 strict-valid
fuzz_json: 20000 iterations, 3851 accepted, 16149 rejected
fuzz_http: 20000 iterations, 6798 complete, 8732 incomplete, 4470 errors
```

Optional coverage-guided libFuzzer harnesses (Homebrew LLVM 20.1.8, 1,000-run
smokes): record harness 54 coverage counters, JSON harness 124 edges, no crash.
Apple Clang lacks compiler-rt libFuzzer; CMake reports that clearly instead of a
link error.

## Benchmarks

`./build/tempo_bench` (Release, machine-specific, single run):

| Benchmark | Result |
|---|---|
| Serialize 20k events ×10 | 113.43 ms, 1,763,194 ops/s, 92.6 MiB/s |
| Strict load ×10 | 84.48 ms, 2,367,516 ops/s, 124.3 MiB/s |
| Determinism hash ×100 | 215.78 ms, 9,268,490 events/s |
| JSON 1k-event page ×20 | 12.25 ms, 1,633,259 events/s, 107.2 MiB/s |
| File save + load ×5 | 103.41 ms, 1,934,081 events/s, 101.6 MiB/s |
| Record counter-race ×50 | 1.00 ms, 50,232 runs/s |
| Replay counter-race ×500 | 7.57 ms, 8,383,160 events/s |

20k-event record: 1,101,280 bytes, hash `5e28fac214dc1a12`. Counter-race record:
127 events, hash `4478f29fe75c9d58`. A later clean build measured within normal
run-to-run variance (e.g. 88–93 MiB/s serialize, 123–124 MiB/s load). No benchmark
number is hard-coded or fabricated.

## UI / install verification

```sh
./scripts/ui_smoke.sh
./scripts/install_smoke.sh
```

Results:

```
ui_smoke: OK (record 1, 100 identical replays, mutation divergence found)
install_smoke: OK (Tempo 0.2.0 (record v1))
```

The UI smoke starts the real launcher, checks tokenized assets, records,
replays 100 times, mutates one schedule decision, reads a frame, and (if Chromium
is installed) loads the page with `selftest=1&nostream=1` and asserts the DOM
reached `counter-race` and rendered the timeline, filters and archive controls.

Browser checks (Chromium/Orca at 1126×900):

- `LIVE` SSE status, four timeline lanes, 127-event table;
- `Replay ×100`: 100 runs, identical yes, hashes stable yes, build id matches;
- step-1 mutation: first divergence at event 7, recorded `task 2` vs replayed
  `task 3`;
- `Watch` filter: `28 shown, 28 matching of 127`;
- breakpoint at frame 9 on `watch local_read = 0`.

## LOC

Excludes generated/embedded UI assets.

| Area | LOC |
|---|---:|
| Public headers (`include/tempo`, umbrella) | 1,705 |
| Runtime/scheduler | 1,107 |
| Virtual clock/RNG | 107 |
| Capabilities | 472 |
| Record format/recorder | 1,284 |
| Replay/diff | 416 |
| Debugger | 297 |
| UI server/JSON/workbench | 3,375 |
| Scenarios | 191 |
| Launcher | 184 |
| Shared version source | 9 |
| **Production core** | **9,172** |
| Example/fuzz/bench tools | 785 |
| **Production + tools** | **9,957** |
| Tests | 6,298 |
| Embedded UI assets (excluded) | 1,481 |
| Shell verification scripts | 139 |

Production + tools is inside the requested 9–11k target. Tests exceed the original
1.2k estimate because they include property, fuzz, sanitizer, soak, async,
archive, packaging and full HTTP integration coverage; there is no filler.

## Commits

- **97 commits at delivery** (this report is the 97th). The 60 minimum and 80+
  target are met.
- Remote: <https://github.com/BViganotti/tempo> (public, `master`).
- Each commit was built/tested during development; the final clean build and all
  sanitizer suites are green.

## Remaining gaps (honest)

These are **not** claimed as fixed:

**Architectural / product scope**
- Cooperative execution only. No preemption, native thread recording, or native
  reverse execution; a scenario looping without awaiting still blocks.
- Capability calls are synchronous on the scheduler thread; a slow loopback
  socket call can stall other tasks up to its timeout. There is no async
  capability awaitable.
- Records are loaded eagerly; the on-disk index is not used for streaming random
  access. Large files/records are bounded by limits, memory budget and archive,
  not streamed.
- Recording and large import parsing still run on the HTTP loop thread (replay
  batches were moved off it). Long recordings can briefly block the UI.
- Untrusted scenario C++ is not sandboxed. TLS, record encryption/signing,
  token rotation and multi-user hosting are non-goals for a loopback tool.
- Linux is untested; Windows is unsupported. No signed/notarized distribution or
  auto-update.

**Security / UX**
- The session token lives in the browser URL and rotates only on restart. No TLS.
- Records are CRC-protected, not cryptographically signed.
- Browser verification is Chromium-only; no formal accessibility audit or mobile
  matrix.
- The timeline samples ticks for very large records; the UI has no virtualization
  beyond paged events.
- Async job divergence is returned in the job result and UI but is not written
  into debug-session frame annotations.

**Verification**
- No CI by requirement; verification is local. libFuzzer runs were short smoke
  campaigns, not long campaigns. Benchmarks are single-run, not a statistical
  performance suite.

The fixable engineering gaps from the review (memory limits/eviction, blocking
batch replay, locale sensitivity, build compatibility, seed range, import/download
caps, filename collisions, archive access, permissions, packaging, filters,
accessibility basics, soak/UI/install automation, coverage-guided fuzzing option)
were implemented and tested. The remaining list is either fundamental to the
cooperative design or requires changing the product’s security/deployment model.

## Reproduction

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
make ui-smoke
make install-smoke
./build/tempo_bench
./build/fuzz_record --iterations 20000
./build/tempo_example /tmp/tempo-example.tmpo
./build/tempo_workbench          # opens the browser
```

MIT licensed. See `README.md`, `docs/`, `CHANGELOG.md`, `SECURITY.md` and
`LIMITATIONS.md`.
