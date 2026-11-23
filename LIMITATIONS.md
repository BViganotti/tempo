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

## Capability timing

- Capability calls execute synchronously on the scheduler thread. A loopback
  socket connect/send/receive can block up to the configured timeout (2 seconds
  by default) while no other task runs.
- There is no async capability awaitable. Offloading effects to a worker thread
  would change completion ordering and needs a deterministic completion queue;
  that is not implemented.
- Replay never performs file/socket I/O; it answers from the record.
- Unmediated OS calls in scenario code are outside determinism. Tempo does not
  intercept `open`, `getenv`, `socket`, `clock_gettime` or native threads; the
  resulting event stream will usually diverge and the first mismatch is
  reported, but determinism is not guaranteed for unmediated effects.

## Files and memory

- Record file access is sandboxed to the configured data root: relative paths,
  no `..`, no symlinks, regular files only, 65,536-byte reads/writes.
- Records are still loaded **eagerly**: a file is fully parsed into events before
  it can be viewed. The on-disk event index is written and validated but the
  current reader does not stream random events from disk.
- The workbench has a global approximate memory budget (`--max-memory-mb`,
  default 256 MiB). Records beyond the in-memory cap are evicted to an archive
  and can be reloaded from the UI. A single recording larger than the budget is
  rejected rather than silently dropped. Debug checkpoints have their own 4 MiB
  estimate budget per session.
- Files can still be up to 64 MiB and a single record can expand to a multiple of
  its file size in memory. Peak memory during parse can exceed the retained
  budget transiently; import/download are bounded by the same 64 MiB HTTP cap.
- Older record files beyond the archive scan cap (512 filenames at startup) are
  not listed until files are removed or moved.

## Replay identity

- A record stores the scenario name and an optional build fingerprint, not the
  code. Replay re-executes the registered scenario with that name. If the build
  id differs it reports a warning but still runs; if the code changed without a
  build-id change it can diverge, and the first differing event is reported.
- Replay refuses records that are not `complete`. Truncated records can be
  inspected and archived but are not replayed.
- The stock launcher knows only the three built-in scenarios. Records from a
  custom embedder registry remain viewable/exportable but not replayable there.

## Hard limits

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
| HTTP headers / body | 16 KiB / 64 MiB |
| HTTP response / outbound buffer | 64 MiB / 160 MiB |
| HTTP connections | 32 |
| Records in memory | 16 (default), plus archive |
| Default workbench memory budget | 256 MiB |
| Replay jobs in flight/retained | 16 |
| Breakpoints | 128 |
| Replay batch | 200 runs |

Exceeding a limit fails cleanly with a recorded error or a rejected request; the
record writer and JSON/HTTP parsers never silently truncate. Log messages longer
than 4 KiB are truncated with an explicit `…[truncated]` marker and the original
size in the event's `d` field.

## Threading and workbench

- The scheduler and core library are single-threaded. The HTTP server has one
  event-loop thread. Long replay batches run on one background job worker; the
  worker only touches immutable record snapshots and a mutex-protected job map.
- Recording and 64 MiB import parsing still run on the HTTP loop thread. They are
  bounded but can briefly block SSE/UI requests for large scenarios or imports.
- A job’s replay divergence is returned in the job result and shown by the UI;
  it is not written back into the debug session’s frame annotation (the UI uses
  the returned divergence for its marker).

## Workbench UI and security

- The UI is a local web app served on `127.0.0.1` only. There is no TLS and no
  remote access. The per-session token is in the launcher URL; it is a bearer
  credential, not OS isolation. It rotates only when the process restarts.
- Data directories, fixtures and record files are chmod'ed owner-only, but
  records are not encrypted or signed. CRC32 detects corruption, not tampering.
- Static assets require the token. The launcher injects it into the index
  document's asset URLs.
- The UI is verified in Chromium via a headless smoke script; Firefox/Safari,
  screen-reader audits and mobile layouts are not part of the tested matrix.
- The timeline samples ticks for very large records and marks `truncated: true`;
  event browsing is paged and filterable.
- Archived records are listed by filename and size only; metadata is read when
  loaded.

## Platform, packaging and verification

- Developed and tested on macOS (arm64) with Apple Clang. POSIX APIs are used;
  Windows is not supported. Linux is likely to work but is not claimed/tested.
- The CMake package installs the library, launcher, headers, docs and a
  `find_package(tempo)` config. There is no signed `.app`, notarization,
  installer, auto-update or package-manager distribution.
- No CI/CD is checked in (per project requirement). Verification is local:
  release tests, ASan/UBSan, TSan, deterministic fuzzers, optional libFuzzer
  harnesses (requires a Clang with compiler-rt), soak loops, benchmarks, install
  smoke and headless UI smoke.
- The mutation fuzzers are deterministic, not coverage-guided in the default
  build. `TEMPO_LIBFUZZER=ON` provides coverage-guided harnesses when the
  toolchain ships libFuzzer (Apple Clang does not).
- Benchmarks are single-run, machine-specific measurements, not a statistically
  controlled performance suite.

## Fundamental non-goals

- Preemptive native execution, native reverse execution, register/stack
  reconstruction.
- Sandboxing untrusted scenario C++ code.
- Multi-user hosting, TLS termination, record encryption/signing/DRM.
- Acting as a general profiler, debugger, or version-control-adjacent tool.

These would require a different product/security architecture; they are not
claimed or partially faked.
