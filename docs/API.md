# API overview

The public headers live in `include/tempo/`; `include/tempo.h` includes all of
them. The library is C++20, header-and-static-library, with no third-party code.

Threading rule for the core: **one scheduler run is single-threaded**. Contexts,
awaitables, hosts and `Scheduler` are not thread-safe and must not be shared
between runs. The HTTP layer runs on its own single event-loop thread; see
`docs/SECURITY.md` and `docs/WORKBENCH.md`.

## Recording a run

```cpp
#include "tempo.h"

tempo::Task scenario(tempo::Context ctx) {
  ctx.watch("phase", "start");
  co_await ctx.sleep_ms(2);
  ctx.watch("phase", "done");
}

int main() {
  tempo::SystemCapabilityOptions caps;
  caps.data_root = "/tmp/tempo-data";  // existing directory
  tempo::SystemHost host(caps);

  tempo::RecordRunOptions options;
  options.seed = 7;
  tempo::RecordRunResult result =
      tempo::record_run("example", scenario, host, options);
  if (!result.ok) return 1;

  std::string error;
  tempo::write_record_file(result.record, "example.tmpo", &error);
}
```

`record_run` owns a `Scheduler` and a `RecordingHost`. It returns a `Record` even
when the scenario fails (unless capture itself hits a hard limit), because a
failed run is a reproducible fact. `ok` describes capture; `summary.ok` describes
the run. `Record::hash` is the FNV-1a 64 hash of the canonical event encoding;
`Record::approximate_bytes()` is the memory estimate the workbench budgets.

`RecordRunOptions::build_id` (and `ScenarioInfo::build_id`) stamps a source/build
fingerprint into metadata. Replay compares it when
`ReplayOptions::expected_build_id` is set and reports `build_mismatch` /
`build_warning` without refusing to run.

## Replaying a record

```cpp
tempo::LoadOutcome loaded = tempo::load_record_file("example.tmpo");
if (!loaded.ok) return 1;

tempo::ReplayReport report = tempo::replay_record(loaded.record, scenario);
if (report.identical) { /* event streams match exactly */ }
else {
  // report.first_divergence.index / reason / expected / actual
}
```

Replay answers file/env/socket calls from the record and chooses tasks from the
recorded decisions. It compares every regenerated event. The first mismatch is
reported with both events and their human summaries; after divergence, replay
continues in a safe fallback mode so it can finish rather than crash.

`replay_many(record, entry, runs)` returns hash stability across runs (max 200).

For UI use, `Workbench::enqueue_replay` runs a batch on one background worker.
`job_status`/`jobs_json` expose a bounded mutex-protected job map; the worker only
touches immutable record snapshots and registered scenario functions, never the
record store.

## Tasks and contexts

- `Task` is a move-only coroutine type returned by functions taking `Context`.
  Start it with `Scheduler::spawn_root` (once) or `Context::spawn` (children).
  `Task` bodies passed to `spawn` are stored until the task ends; captures are
  therefore safe.
- `Context` is a cheap value: scheduler pointer + task id. Pass by value.
- `co_await ctx.yield()` re-queues the task.
- `co_await ctx.sleep_ms(n)` suspends until the virtual clock reaches the
  deadline. Time only advances when no task is ready.
- `co_await ctx.join(id)` returns `true` if the target finished successfully.
- `ctx.watch(name, value)` updates inspector state and emits a `Watch` event only
  when the value changes; oversized names/values throw `LimitError`.
- `ctx.note(name, value)` records metadata without affecting reconstructed state.
- `ctx.log(message)` emits a bounded log line.
- `ctx.read_file(path)`, `write_file(path, data)`, `env_get(name)`,
  `connect/send/recv/close_socket`, and `random_u64()` are mediated capabilities.
- `ctx.cancel(id)` requests cancellation; `ctx.cancelled()` and
  `ctx.check_cancelled()` observe it.
- `ctx.sleep_until_ns(deadline)` sleeps to an absolute virtual deadline.
- `ctx.watch` supports string, integral, double and bool values; doubles are
  formatted with the classic locale, so a host calling `setlocale` cannot change
  recorded text.

Awaitables throw `CancelledError` when a cancelled task resumes. An uncaught
exception fails the task, emits `Error` + `Exit`, and wakes joiners with `false`.

## Scheduler and host

`SchedulerOptions` sets `seed`, `max_steps`, `max_tasks`, `start_ns` and
`max_virtual_ns`. The scheduler emits events through `RunHost::emit`; the host
also chooses from the ready queue and provides capabilities. `RunHost` is a
pure interface, so embedding applications can implement an in-memory host with
no OS access (see `examples/tempo_demo`). `Scheduler::task_name` and
`task_count` expose introspection.

`SystemHost` implements the real effects with a sandboxed file root, optional
environment access, loopback-only sockets and bounded timeouts. It is used by
the recorder; the replay host never touches the OS.

## Records

`RecordMeta` stores the scenario name, seed, tool version, build id, creation
time, start time and arbitrary attributes. `Record` stores events, the determinism hash and
completeness flags. `serialize_record` produces a portable container;
`write_record_file` writes atomically (`temp + rename`, `fsync`).
`load_record_file`/`load_record_bytes` parse untrusted input under strict limits.
`LoadMode::Recover` returns the valid prefix of a truncated file and marks it
incomplete; `LoadMode::Strict` requires a complete, CRC-valid file.

## Debugger

`DebugSession` wraps a `Record` and reconstructs watch state from `Watch` events.
State at any index is exact; checkpoints are built only under a 4 MiB estimate
budget, otherwise state is rebuilt from the start. Breakpoints match on event
type, task id and/or a case-insensitive substring of `s1`, `s2` or the type
name. `next_breakpoint(from, forward)` implements both directions.
`set_divergence` annotates the frame a replay diverged at.

## Diff

`diff_records(a, b)` reports equality, the common prefix, the first differing
event with summaries, per-type counts, and both hashes. It compares event
streams, not files, so formatting differences cannot mask a semantic change.

## Workbench

`WorkbenchOptions` sets the data directory, the record count cap, and a global
approximate memory budget. Records are evicted oldest-first from memory into an
`archive_files_` list when either cap is hit; files stay on disk and
`open_archived` reloads one by basename. `import_record` is idempotent by hash,
and `delete_record` can remove both memory and file. All of this is driven by the
HTTP API documented in `docs/WORKBENCH.md`.

`Workbench` is not thread-safe for record-store mutation. The background replay
worker is deliberately restricted to immutable snapshots and the job map; long
record/import operations still run on the HTTP thread.

## Formatting

`include/tempo/format.hpp` provides locale-independent general, fixed and JSON
number formatting. Persisted/recorded text never depends on the global C++
locale.

## JSON and HTTP

`tempo::json::parse` is strict, bounded (1 MiB, depth 32, bounded collections)
and rejects duplicate keys. `tempo::json::Value::dump` is deterministic (object
keys sorted, compact). `tempo::HttpServer` is a poll-based loopback HTTP/1.1
server with token/Host/Origin checks, SSE, bounded buffers and no CORS. See
`docs/WORKBENCH.md` for the endpoint table.

## Lifetime rules

- A `Scheduler` holds a reference to its host; the host must outlive the run.
- A `DebugSession` holds a `shared_ptr<const Record>`; the record stays alive.
- `Workbench` owns records and sessions. It is single-threaded; the HTTP server
  calls it from one loop thread.
- `Record` and `Event` values are freely copyable and independent of the OS.
