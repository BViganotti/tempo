# Scenarios

A scenario is an ordinary C++20 coroutine entry point:

```cpp
tempo::Task my_scenario(tempo::Context ctx) {
  ctx.watch("phase", "run");
  const tempo::TaskId child = ctx.spawn("worker", [](tempo::Context c) -> tempo::Task {
    co_await c.sleep_ms(5);
    c.note("worker", "done");
  });
  (void)co_await ctx.join(child);
}
```

`RecordRunOptions` / `record_run` accept the entry function. The scheduler owns
the coroutine and captures every event. If the scenario throws, the task fails,
`Error` + `Exit` are recorded and joiners wake with `false`; the record remains
replayable.

## Built-in scenarios

The workbench embeds three scenarios in `src/scenario/demo.cpp`:

| Name | What it shows |
|---|---|
| `counter-race` | Two tasks perform a read-modify-write on a shared counter with a yield between read and write. The schedule loses updates, so the run's own check fails; replay reproduces that exact failure 100 times, and altering one scheduling decision changes the event stream. |
| `flaky-service` | Reads `fixtures/service.conf`, `TEMPO_SERVICE_MODE`, and one random draw, then two tasks race on shared state. Demonstrates capability replay after deleting the fixture. |
| `pipeline` | Three stages with virtual sleeps, yields and joins; produces a multi-lane timeline. |

The workbench writes the fixture `fixtures/service.conf` into its data root before
recording `flaky-service`.

## Registering scenarios

```cpp
tempo::ScenarioRegistry registry;
registry.add(tempo::ScenarioInfo{
  "my-scenario",
  "what it demonstrates",
  {{"fixtures/input.txt", "contents"}},
  my_scenario});
```

Names must be non-empty, ≤128 bytes, unique, and the entry must be callable.
`ScenarioInfo::build_id` is optional; built-ins use the SHA-256 of the scenario
sources generated at configure time. Records stamp it and replay warns when the
registered scenario’s build id differs. `ScenarioRegistry::builtins()` is
immutable and used by the stock workbench. A
custom embedder can build its own registry and drive `record_run`/`replay_record`
directly; the stock launcher only knows the built-ins. Record files from custom
scenarios remain inspectable but the stock launcher reports that the scenario is
not registered on replay.

## Writing a replayable scenario

- Await Tempo for every suspension point. Never block or spawn native threads.
- Put all external effects through `Context` capabilities.
- Report the state you want to inspect with `ctx.watch`; watchers are the
  debugger's state model. Keep names ≤64 bytes and values ≤512 bytes.
- Use `ctx.note` for results that are metadata rather than mutable state.
- Read time through `ctx.now_ms()` (virtual) and randomness through
  `ctx.random_u64()`, not OS clocks or `rand()`.
- Keep captures alive: the scheduler stores a spawned body for the task's
  lifetime, so captures passed to `ctx.spawn` are safe.
- Handle `CancelledError` if cleanup matters; otherwise let it propagate.
