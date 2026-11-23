# Tempo — Implementation Plan

Status: ready for one-shot implementation in this repository.

## Mission
Tempo is a deterministic record/replay workbench for native C++ tests and
cooperative services. Record a run once, replay it exactly, and debug it with
reverse stepping over the event log. It is a local desktop workbench, not a CLI
and not a toy.

Scope: deterministic cooperative tasks, not arbitrary preemptive native code.
All external effects go through capabilities so they can be recorded and replayed.

## Product shape
- Embeddable `tempo` C++ library.
- Portable record files.
- Local UI workbench: task/thread timeline, event list, state inspector,
  breakpoints and run diff.
- Demo: two tasks race on a counter; record, replay 100× identically, then alter
  one schedule decision and show the first divergence.

## Architecture
```
tempo/
  include/tempo/
    task.hpp, context.hpp, capability.hpp, recorder.hpp, replay.hpp
  src/
    runtime/     coroutine tasks, scheduler, task IDs, cancellation
    virtual/     virtual clock, deterministic RNG, sleep
    capability/  file, socket, environment, random wrappers
    record/      record model, writer/reader, string table, CRC, indexes
    replay/      scheduler replay, input replay, determinism hash, diff
    debug/       event log, reverse stepping, state reconstruction, breakpoints
    ui/          HTTP/SSE, JSON, embedded assets, launcher
  ui/            original HTML/CSS/JS assets, embedded at build time
  examples/tempo_demo/
  tests/  fuzz/  bench/
```

## LOC budget (target ~10k)
| Module | LOC |
|---|---:|
| task runtime/scheduler | 1,500 |
| virtual clock/RNG | 500 |
| capabilities | 1,300 |
| record format/recorder | 1,500 |
| replay engine + determinism | 1,500 |
| debugger model (reverse/breakpoints/diff) | 1,400 |
| UI server + assets | 1,700 |
| launcher + demo | 600 |
| tests/fuzz/bench | 1,200 |

## Milestones and commit ranges
### P0 Scaffold (commits 1–8)
CMake, test harness, README, LICENSE, Makefile, directory layout, no CI, no dependencies.

### P1 Task runtime (commits 9–20)
Coroutine/task type → scheduler → task IDs → deterministic run queue → cancellation → context API → unit tests → scheduling property tests.

### P2 Virtual environment (commits 21–30)
Virtual clock → deterministic RNG → sleep → file capability → socket capability → environment capability → capability tests.

### P3 Record format and recorder (commits 31–42)
Record model → binary writer/reader → string table → CRC → indexes → recorder integration → truncation recovery → fuzz.

### P4 Replay engine (commits 43–54)
Replay scheduler → input replay → determinism hash → 100-run equality test → mismatch diagnostics → diff of two records.

### P5 Debugger model (commits 55–66)
Event log → reverse stepping → state reconstruction → breakpoints → watch values → first-divergence view → tests.

### P6 UI (commits 67–78)
HTTP/JSON/SSE → embedded assets → timeline → event list → state inspector → breakpoints → diff view.

### P7 Demo and hardening (commits 79–95)
Flaky test demo → 100× replay proof → altered-schedule divergence → malformed record fuzz → ASan/UBSan → TSan → benchmarks → docs → FINAL_REPORT.

## Testing
- 100-run replay equality and event-hash stability.
- Property tests for scheduler determinism and record round-trip.
- Capability tests: recorded file/socket results replay without OS access.
- Reverse stepping restores inspector state.
- Fuzz record reader and query/UI parser.

## Acceptance criteria
- Replay produces identical outputs and event hashes for 100 runs.
- Reversing over the event log restores state exactly.
- Recorded file/socket results replay without touching the OS.
- Breakpoints stop at the right event/state change.
- Changing one schedule decision shows the first divergence.
- Malformed records fail cleanly.

## Risks and fallbacks
- Determinism is hard: restrict to cooperative tasks and capability-mediated I/O.
- Reverse execution: reverse over the recorded log, not arbitrary native execution.
- UI scope: local web UI with canvas is the required baseline.
