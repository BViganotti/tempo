# Architecture

Tempo is split into an embeddable core and a local workbench shell.

```
scenario (your cooperative C++ code)
        │  Tempo Task / Context API
        ▼
  scheduler ── virtual clock ── deterministic RNG
        │            │                 │
        │            └──── event log ───┘
        ▼
     RunHost  ── recorder (system capabilities)  ── .tmpo record
        │
        └──────── replay host (recorded values + decisions)
```

## Layers

1. **Runtime** (`src/runtime`): C++20 coroutine tasks, a deterministic ready
   queue, task IDs and parent links, cooperative cancellation, joins, watch
   variables. A task only advances when the scheduler resumes it; there is no
   preemption and no hidden threading.
2. **Virtual environment** (`src/virtual`): a monotonic virtual clock advanced
   only when the ready queue is empty, plus a seeded xoshiro256** RNG. Both are
   recorded so a replay can prove it saw the same numbers.
3. **Capabilities** (`src/capability`): all external effects (file, environment,
   loopback socket, random, log) flow through a `RunHost`. Recording executes the
   real effect and stores the result; replay returns the stored result and never
   touches the OS.
4. **Records** (`src/record`): a bounded, versioned binary container with a
   string table, per-event CRC32, section CRCs and a truncation recovery mode.
5. **Replay** (`src/replay`): re-executes the scenario against a replay host,
   compares every regenerated event with the record, reports the first
   divergence, and computes a stable determinism hash over the event stream.
6. **Debugger** (`src/debug`): an index into the event log with state
   checkpoints. Watch events are the state deltas, so state at any index is
   reconstructed exactly from the nearest checkpoint; reverse stepping is just
   selecting an earlier index. Breakpoints match on event type, task and text.
7. **UI** (`src/ui`): a poll-based HTTP/1.1 server on `127.0.0.1`, a JSON parser
   and writer with hard limits, SSE for updates, and embedded assets. Every
   request must present the per-session token and a loopback Host header.
8. **Launcher** (`app/main.cpp`): starts the server, opens the browser, and runs
   until interrupted. It has no interactive terminal workflow.

## Determinism boundary

The event log covers what Tempo controls: scheduling, virtual time, RNG draws
and capability calls. A scenario that reads a clock, opens a raw socket, or
spawns a raw thread bypasses the boundary and is not replayable; this is
documented in `LIMITATIONS.md`.

## Threading

The core scheduler is single-threaded by contract. The HTTP server uses one
event-loop thread and no shared mutable state beyond the workbench facade, which
is only touched by that thread. SSE clients receive immutable snapshots. This is
verified with TSan.

## Bounded resources

Every parser has explicit limits (`include/tempo/limits.hpp`): record size,
event count, string length, string table size, JSON depth, HTTP request size,
task count, watch variables, socket payloads and replay steps. Exceeding a limit
fails cleanly with an error; nothing is silently truncated except where a limit
explicitly defines truncation for display.
