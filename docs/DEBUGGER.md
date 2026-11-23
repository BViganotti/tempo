# Debugger model

Tempo's debugger steps over a recorded event log. It does not attach to a live
process and it does not reverse native execution.

## What is reconstructed

Scenarios report inspector state explicitly:

```cpp
ctx.watch("counter", value);   // emits a Watch event only when the value changes
```

Every `Watch` event is a state delta for `(task id, name)`. `DebugSession` builds
the state after event `i` by applying all `Watch` events up to and including `i`.
Consequently:

- state at any index is exact for everything watched;
- reverse stepping is selecting an earlier index, not undoing side effects;
- unwatched locals, native stacks and file descriptors are outside the model
  (documented in `LIMITATIONS.md`).

`ctx.note(key, value)` records annotations that the UI shows but that are not
part of reconstructed inspector state.

## Checkpoints

Rebuilding from event 0 for every query is always correct but can be O(n). The
session estimates one state copy from the distinct watch keys and their maximum
value sizes. If the estimated total for checkpoints every 64 events fits in a
4 MiB budget (and there are at most 4,096 distinct keys), it stores checkpoints;
otherwise it does not. Both paths are tested to produce identical state at every
index. The estimate is conservative, so large-state records silently fall back
to scanning without using unbounded memory.

## Navigation

- `step_forward(i)` returns `i+1` unless already at the end.
- `step_back(i)` returns `i-1` unless already at 0.
- `frame_at(i)` returns the event, the exact watch state, and whether this index
  is the annotated divergence.
- The UI also offers first/last, a scrub slider, arrow keys, and "run to
  breakpoint" in both directions.

## Breakpoints

A breakpoint is a set of optional criteria; all populated criteria must match:

- event type (`Watch`, `FileRead`, `Step`, ...);
- task id (0 matches system events);
- case-insensitive substring in `s1`, `s2`, or the event type name.

Breakpoints are evaluated against recorded events only. Disabled breakpoints are
skipped. `next_breakpoint(from, forward)` searches strictly after/before `from`
and stops at the first match. The workbench keeps one global breakpoint list
(max 128) and mirrors it into every debug session.

## First divergence

A replay report contains:

- whether the streams matched (`identical`);
- the first differing index, reason, and both events with human summaries;
- the recorded and replayed hashes and event counts;
- the run summary (steps, tasks, failures, limits).

`DebugSession::set_divergence` annotates that index. The UI draws a red line in
the timeline, marks the event row, shows a side-by-side expected/actual summary,
and jumps to the frame. A later identical replay clears the annotation.

## Why compare events, not output

Two runs can produce the same final counter by accident while taking different
paths. Tempo compares every event (including scheduling decisions, capability
results, random draws, clock jumps and watch deltas), so an accidental match at
the end cannot hide a different execution.

## Limits

- Debug sessions are read-only over immutable records.
- Breakpoints and sessions are not thread-safe; the workbench drives them from
  one loop thread.
- The timeline API samples events for very large records and sets
  `truncated: true`; event browsing is independent and paged.
- Recovered (incomplete) records can be inspected but not replayed.
