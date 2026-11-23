# Limitations

Tempo is deliberately narrow. These are the boundaries of what it actually does;
nothing below is a TODO disguised as a feature.

## Cooperative execution only

- A task runs until it awaits Tempo (`yield`, `sleep`, `join`, or a capability
  call). Tempo cannot preempt native code, interrupt a tight loop, or record
  arbitrary threads. A scenario that loops forever without awaiting will block
  the workbench process just like any cooperative runtime. Built-in scenarios
  always await.
- Cancellation is cooperative. `Context::cancel` wakes a sleeping/waiting task
  and marks it; the task observes cancellation at its next Tempo await or
  `check_cancelled()`. Code between awaits is not interrupted.
- Reverse stepping is over the **event log**, not native machine state. It
  restores exactly the values a scenario reported with `ctx.watch(...)`. Local
  variables that are never watched are not reconstructed, and native memory,
  stacks, file descriptors and registers are out of scope.

## Effects must go through capabilities

Replayable external effects are the ones Tempo mediates: file reads/writes,
environment reads, loopback socket traffic, virtual time and random draws. A
scenario that calls `open`, `getenv`, `socket`, `clock_gettime`, or spawns its
own thread bypasses the runtime. Tempo does not detect such bypasses directly;
the resulting event stream will usually diverge on replay and the first mismatch
is reported, but determinism is not guaranteed for unmediated effects.

File capabilities are sandboxed to the workbench data root:

- relative paths only, no `..`, no empty components, no NUL bytes, no symlinks;
- reads and writes are capped at 65,536 bytes per call;
- only regular files; no directories, FIFOs or device nodes;
- no rename, unlink, chmod, or directory listing.

Environment reads are capped at 4,096 bytes per value and can be disabled by the
host. Recorded environment values become part of the record; do not record
secrets you would not store in the data directory.

Sockets are restricted to loopback (`127.0.0.1`, `localhost`, `::1`) and have a
2-second default timeout. Replay never opens a socket.

## Replay identity depends on scenario identity

A record stores the scenario *name*, not the scenario code. Replay re-executes
the registered scenario with that name. Changing code, task names, watch values,
capability call order, or the scheduler seed stored in the record can cause a
divergence; Tempo reports the first differing event. The built-in registry is
used by the workbench, so records created by an embedder's custom registry are
viewable but not replayable in the stock launcher.

Replay refuses records that are not `complete`. Truncated records can still be
inspected (recovery mode) but are not replayed, because replaying a prefix would
pretend to prove more than was recorded.

## Hard limits

All limits are in `include/tempo/limits.hpp`. Notable ones:

| Limit | Value |
|---|---:|
| Record file / parsed body | 64 MiB |
| Events per record | 1,000,000 |
| Event string / payload | 8 KiB / 64 KiB |
| Tasks per run | 1,024 |
| Steps per run | 2,000,000 |
| Watch variables per task | 64 |
| Watch name / value | 64 / 512 bytes |
| String table | 200,000 strings, 16 MiB |
| JSON input / depth | 1 MiB / 32 |
| HTTP headers / body | 16 KiB / 256 KiB |
| HTTP connections | 32 |
| Stored records in memory | 16 (files are kept on disk) |
| Breakpoints | 128 |
| Replay batch | 200 runs |

Exceeding a limit fails cleanly with a recorded error or a rejected request; the
record writer and JSON/HTTP parsers never silently truncate. Log messages longer
than 4 KiB are truncated with an explicit `…[truncated]` marker and the original
size in the event's `d` field.

## Workbench UI

- The UI is a local web app served on `127.0.0.1` only. There is no TLS and no
  remote access. The per-session token is part of the launcher URL; it is not a
  substitute for OS user isolation.
- Static assets require the token too. The launcher injects it into the index
  document's asset URLs; opening a raw asset URL without the token returns 403.
- Downloading a record through the UI is capped at 4 MiB by the HTTP response
  limit. Larger records remain in the data directory (`records/`) and can be
  copied directly.
- The timeline endpoint samples lanes at a bounded number of points for very
  large records; it marks `truncated: true` when it does. The event list is
  paged (maximum 500 per request).
- The UI was verified with Chromium. It uses standard `fetch`, `EventSource` and
  canvas; other modern browsers should work but are not part of the tested
  matrix.

## Platform and process

- Developed and tested on macOS (arm64) with Apple Clang. POSIX APIs are used;
  Windows is not supported. Linux is likely to work but is not claimed.
- The launcher runs until SIGINT/SIGTERM. It has no service manager, tray icon,
  or native window; the browser is the desktop UI.
- Records store a wall-clock `created` timestamp for display, but replay never
  reads real time.
- Randomness is deterministic (xoshiro256**), not cryptographic. Do not use it
  for secrets.
- Record files are owner-only (`0600`); the data directory is created with the
  default umask. Tempo does not encrypt records.

## Verification boundaries

Benchmarks and sanitizer results in `FINAL_REPORT.md` were measured on the build
machine described there. They are not universal performance claims. The fuzzers
are deterministic mutation fuzzers, not coverage-guided libFuzzer campaigns;
they exercise the production parsers but cannot prove the absence of all bugs.
