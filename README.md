# Tempo

Tempo is a deterministic record/replay workbench for native C++ tests and
cooperative services. Record a cooperative task run once, replay it exactly, and
debug it with forward and reverse stepping over the recorded event log.

Tempo is a local desktop workbench, not a CLI: the launcher starts an embedded
HTTP/SSE server on `127.0.0.1`, serves an original UI compiled into the binary,
and opens your browser. No third-party code, no CDN, no external assets, no
internet access at runtime.

## What it demonstrates

- A deterministic cooperative scheduler with task IDs, joins, cancellation,
  virtual sleeps and a seeded RNG.
- Every external effect mediated by capabilities: file, environment, loopback
  socket, random. Replay answers from the record and never touches the OS.
- A portable, versioned binary record format with a string table, per-event CRC,
  body/footer CRC, an event index and truncation recovery.
- Exact replay: 100 replays of the built-in counter race produce the same event
  hash; a changed program or altered scheduling decision reports the first
  diverging event with both sides.
- A debugger over the event log: state reconstruction from watch deltas,
  breakpoints by type/task/text, reverse stepping, timeline and run diff.
- A hardened local UI: random session token, Host/Origin checks, CSP, no CORS,
  bounded parsers, SSE, background replay jobs, event filters, accessible
  keyboard navigation and archived-record loading.
- Operational hardening: a global record memory budget with eviction/archive,
  owner-only data files, locale-independent persisted formatting, scenario
  build fingerprints, full-width seeds, 64 MiB HTTP import/download, soak tests
  and an end-to-end UI smoke script.

The built-in demo is a real scheduling bug, not a scripted animation: two tasks
read a shared counter, yield, then write back a stale value. The recorded run's
own check fails; replay reproduces that failure 100 times; altering one
scheduling decision changes the event stream and the first divergence is shown.

## Build

Requires CMake ≥ 3.20, Ninja and a C++20 compiler (tested on macOS arm64).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Warnings are errors. There is no CI configuration and no network dependency.

## Run

```sh
./build/tempo_workbench
```

The launcher prints the URL, mints a per-session token, starts the server and
opens the default browser. Use `--no-open` for headless checks and
`--data-dir PATH` to choose where records and fixtures live. Press `Ctrl+C` to
stop; there are no prompts.

The UI (all original HTML/CSS/JS, embedded at build time) shows:

- record controls (scenario, seed, optional altered scheduling step),
- a record list and breakpoint editor,
- a multi-lane canvas timeline with event categories and the divergence marker,
- a paged event table, frame inspector and reconstructed watch state,
- replay proof, altered-schedule replay, first-divergence view, run diff, and
  portable record export.

## Embed as a library

```cpp
#include "tempo.h"

tempo::Task scenario(tempo::Context ctx) {
  ctx.watch("phase", "start");
  co_await ctx.sleep_ms(2);
  ctx.watch("phase", "done");
}

tempo::SystemCapabilityOptions caps;
caps.data_root = "/tmp/tempo-data";
tempo::SystemHost host(caps);
tempo::RecordRunResult recorded =
    tempo::record_run("example", scenario, host, {.seed = 7});

tempo::ReplayReport replay = tempo::replay_record(recorded.record, scenario);
```

`examples/tempo_demo` records through a fully in-memory host, writes a `.tmpo`
file, reloads it and checks 100 identical replays.

## Layout

```
include/tempo/   public embeddable API
src/runtime/     tasks, scheduler, contexts, cancellation
src/virtual/     virtual clock and deterministic RNG
src/capability/  sandboxed file/env/loopback socket provider
src/record/      event model, string table, CRC, record writer/reader
src/replay/      replay engine, determinism hashes, record diff
src/debug/       event-log debugger, reverse stepping, breakpoints
src/ui/          HTTP/SSE server, JSON, workbench API, embedded assets
src/scenario/    built-in demo scenarios
ui/              original HTML/CSS/JS, embedded at build time
app/             launcher
tests/           unit, property, recovery, fuzz-smoke and integration tests
fuzz/            deterministic mutation fuzzers
bench/           measured micro-benchmarks
docs/            architecture, API, format, debugger, security, testing
```

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [API overview](docs/API.md)
- [Record format v1](docs/RECORD_FORMAT.md)
- [Event types](docs/EVENT_TYPES.md)
- [Workbench and HTTP API](docs/WORKBENCH.md)
- [Debugger model](docs/DEBUGGER.md)
- [Scenarios](docs/SCENARIOS.md)
- [Security model](docs/SECURITY.md)
- [Testing and sanitizers](docs/TESTING.md)
- [Verification results](docs/VERIFICATION.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Building and installing](docs/BUILD.md)
- [Limitations](LIMITATIONS.md)

## Verification

The repository contains 45 ctest targets plus ASan/UBSan and TSan builds,
deterministic mutation fuzzers for records/JSON/HTTP, optional libFuzzer
coverage-guided harnesses, an install smoke test, a headless UI smoke test and
measured benchmarks.
`FINAL_REPORT.md` records LOC, commit count, test commands/results, benchmark
numbers and known gaps. `CHANGELOG.md` tracks the work.

## Scope and honesty

Tempo records **cooperative** tasks and capability-mediated effects, not
arbitrary preemptive native execution. Raw OS calls, unwatched locals and
unmediated threads are outside the replay/debug model; this is documented rather
than hidden. Capability calls still execute synchronously on the scheduler
thread, and records are loaded eagerly; both are bounded and documented in
`LIMITATIONS.md`. TLS/signing and untrusted-scenario sandboxing remain explicit
non-goals for a loopback developer tool. If a feature is not implemented, it is
listed there.

## License

MIT. See [LICENSE](LICENSE).
