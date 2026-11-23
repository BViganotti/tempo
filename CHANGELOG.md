# Changelog

All notable changes to Tempo are documented here. Entries describe the commit
series that produced this repository; there are no unreleased placeholders.

## [0.2.0] - 2026-11-23

Production-hardening pass. Record format v1 is unchanged; existing records remain
readable.

### Reliability and operations

- Added a global approximate record-memory budget (`--max-memory-mb`, default
  256 MiB) with oldest-first eviction and rejection of a single oversized
  recording.
- Added an on-disk record archive for records evicted from memory, with
  `GET /api/archive` and `POST /api/records/open`; unsafe basenames are rejected.
- Long replay batches now run on a bounded background job worker (`async: true`,
  `GET /api/jobs/{id}`); the UI polls instead of freezing the SSE/HTTP loop.
- Added soak tests (500 replays, 100 record round-trips, 100 workbench
  record/delete cycles) and a 64 MiB HTTP import/download limit aligned with the
  record cap.

### Determinism and compatibility

- Locale-independent formatting for watch doubles, JSON numbers and clock text;
  a comma locale cannot corrupt recorded or persisted text.
- Records and scenarios carry a generated SHA-256 build fingerprint; replay
  reports a build mismatch in the API and UI without refusing to run.
- HTTP record creation accepts full 64-bit seeds as decimal strings; records
  expose a stable `uid` equal to the determinism hash.
- Record filenames use the full 16-hex hash to remove the previous 32-bit prefix
  collision risk.
- Fixed the synchronous replay path to reject incomplete records before scenario
  lookup (regression caught by the recovery test).

### Security and packaging

- Data, records and fixture directories are chmod'ed owner-only; fixture and
  record files are `0600`.
- Installed launcher, headers, docs, export targets and a relocatable
  `find_package(tempo)` config; `make install-smoke` builds a consumer.
- UI: archived-record loading, event type/task filters, skip link, live status,
  keyboard-focusable event rows, canvas fallback text, focus styles and
  reduced-motion CSS.
- Added a headless UI smoke script that verifies tokenized assets, recording,
  100 identical replays, mutation divergence and—where Chromium exists—a DOM
  self-test.

### Verification tooling

- Added optional coverage-guided libFuzzer harnesses
  (`-DTEMPO_LIBFUZZER=ON`, requires Clang + compiler-rt) and a
  `-DTEMPO_COVERAGE=ON` build option.
- Added an install smoke script, a UI smoke script and `make soak`.
- Re-ran release, ASan/UBSan, TSan, deterministic fuzzing, benchmarks and the
  browser/API smoke; results are in `docs/VERIFICATION.md` and
  `FINAL_REPORT.md`.

## [0.1.0] - 2026-11-23

### Scaffold

- CMake + Ninja C++20 build with warnings-as-errors, dependency-free test
  harness, MIT license, README and architecture docs.

### Deterministic runtime

- C++20 `Task` coroutines, `Context` values and a single-threaded scheduler.
- Seeded xoshiro256** scheduling draws and virtual clock; clock advances only
  when no task is ready.
- Spawn/join, cooperative cancellation (`CancelledError`, wake on cancel),
  absolute and relative sleeps, task names, limits, run summaries.
- Exact watch-variable changes emitted as events; notes and bounded logs.
- Unit, cancellation, limit and property tests for ordering and determinism.

### Virtual environment and capabilities

- Sandboxed file read/write (relative paths, no symlinks, 64 KiB cap), bounded
  environment reads, loopback-only sockets with timeouts, mediated random.
- Capability events record ok/error/payload; failures are recorded outcomes.
- Real POSIX socket echo test beside a purely in-memory test host.

### Records

- Portable v1 container: 40-byte header, metadata, deduplicated string table,
  48-byte event headers with per-event CRC, index, event-region size, body CRC
  and footer CRC.
- Strict loading and truncation recovery that preserves only a valid prefix.
- Atomic `fsync` + rename writes; deterministic serialization.
- Recording host with optional single-step schedule mutation.
- Round-trip, truncation, corruption, random-record property and mutation fuzz
  tests.

### Replay

- Replay host answers decisions and all capabilities from the record; no OS
  access on the replay path.
- Event-by-event comparison with first-divergence diagnostics and both sides.
- 100-run hash proof, batch first-failing-run reporting, mutated recordings.
- Structural record diff with first differing event and per-type counts.

### Debugger

- Exact state reconstruction from watch deltas with a 4 MiB checkpoint budget.
- Forward/reverse stepping, frames, timeline lanes and task status.
- Breakpoints on type/task/case-insensitive text; forward and backward search.
- Divergence annotation and first-divergence view.
- Tests comparing every reconstructed frame to brute force, with and without
  checkpoints.

### Workbench UI

- Poll-based HTTP/1.1 + SSE server on `127.0.0.1`, token/Host/Origin checks,
  CSP, no CORS, bounded headers/body/connections and SSE queues.
- Strict bounded JSON parser/writer.
- Record store with save/load, 16-record memory cap, import (octet-stream,
  hash-deduplicated), delete, export, health, event filtering and paged events.
- Debugger frame/state/timeline endpoints and breakpoint synchronization.
- Original embedded HTML/CSS/JS: timeline canvas, event table, inspector,
  breakpoints, replay proof, altered schedule, diff, import/export/delete.
- Launcher mints a token, injects it into asset URLs, opens the browser and runs
  without terminal prompts.

### Scenarios and demo

- `counter-race`: a real lost-update scheduling bug recorded once, replayed 100×
  identically, and diverged by altering one decision.
- `flaky-service`: fixture + environment + random + shared-state race.
- `pipeline`: three cooperative stages for the timeline.
- Embeddable example with a fully in-memory host and 100 replays.

### Hardening and verification

- ASan+UBSan and TSan builds pass the full ctest suite.
- Deterministic record/JSON/HTTP fuzzers, each also registered as a ctest smoke
  test; 20,000-iteration runs recorded in `FINAL_REPORT.md`.
- Measured benchmark of serialization, loading, hashing, JSON, file I/O,
  recording and replay.
- Fixes found by the above: footer CRC offset, string-table index alignment on
  load, JSON integer-cast UB, 64-bit event field precision, authenticated asset
  injection, SSE endpoint wiring, timeline load on auto-record, record file path
  tracking for delete, and a test-only JSON lifetime bug.

### Documentation

- Architecture, API, record format v1, event types, debugger model, scenarios,
  security, HTTP workbench, build, testing and verification docs, plus
  `LIMITATIONS.md`, `README.md` and `FINAL_REPORT.md`.

[0.1.0]: https://github.com/BViganotti/tempo
