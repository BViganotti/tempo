# Tempo

Tempo is a deterministic record/replay workbench for native C++ tests and
cooperative services. Record a cooperative task run once, replay it exactly, and
debug it with forward and reverse stepping over the recorded event log.

Tempo is a local desktop workbench: a launcher starts an embedded HTTP/SSE server
on `127.0.0.1`, compiles the original UI into the binary, and opens your browser.
There is no CLI-only workflow, no network access beyond loopback, no CDN, and no
third-party dependencies.

## Status

Under active construction. See `PLAN.md`, `REQUIREMENTS.md`, `docs/` and
`FINAL_REPORT.md` for the exact state of each subsystem.

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Run the workbench:

```sh
./build/tempo_workbench
```

The launcher binds an ephemeral port on `127.0.0.1`, mints a per-session random
token, and opens the default browser. The token is required for every API and
asset request; requests from other origins are rejected.

## Why deterministic replay

Native tests and services are often "flaky": the interesting failure depends on
one scheduling decision, one file read, one environment variable or one random
draw. Tempo records those decisions and external effects as events:

- every ready-queue choice,
- sleeps and virtual-clock advances,
- capability calls (file, environment, socket, random),
- task lifecycle changes,
- scenario watch variables.

Replay re-executes the same cooperative program with the recorded choices and
capability results. If the event stream matches the recording, the run is
bit-identical. If it diverges, Tempo reports the first divergence with both
sides of the event. Because state changes are recorded as watch events, the
debugger can reconstruct scenario state at any recorded index and step backward.

## Scope and honesty

Tempo is for *cooperative* tasks: a task runs until it awaits Tempo (yield,
sleep, join, or a capability call). It is not a preemptive native debugger and
cannot record arbitrary threads or signals. All external effects must go through
the provided capabilities to be replayable. Anything outside this boundary is
documented in `LIMITATIONS.md` rather than pretended to work.

## Layout

```
include/tempo/   public embeddable library API
src/runtime/     tasks, scheduler, contexts, cancellation
src/virtual/     virtual clock and deterministic RNG
src/capability/  mediated file/env/socket/random effects
src/record/      event model, string table, CRC, portable record files
src/replay/      replay engine, determinism hashes, diff
src/debug/       event-log debugger, reverse stepping, breakpoints
src/ui/          HTTP/SSE server, JSON, embedded assets, workbench API
src/scenario/    built-in demo scenarios
ui/              original HTML/CSS/JS, embedded at build time
app/             launcher (the only executable a user runs)
tests/           unit, property, malformed-input and fuzz-smoke tests
fuzz/            mutation fuzzers
bench/           replay/parse benchmarks
```

## License

MIT. See `LICENSE`.
