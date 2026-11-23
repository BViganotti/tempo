# Event types (format v1)

Numeric ids are stable and must not be renumbered. `a`, `b`, `c`, `d` are the
64-bit numeric fields of `Event`; `s1`/`s2` are bounded strings and `bytes` is the
bounded payload.

| ID | Name | Subject task | Fields |
|---:|---|---|---|
| 1 | ScenarioStart | — | `a` seed, `b` start_ns, `d` now, `s1` scenario name |
| 2 | ScenarioEnd | — | `a` steps, `b` final now, `c` failed tasks, `s1` ok/failed |
| 3 | Spawn | child | `a` parent, `b` child, `d` now, `s1` name |
| 4 | Step | chosen | `a` step index, `b` chosen task, `c` RNG draw, `d` now, `s1` name |
| 5 | Yield | task | `a` now |
| 6 | Sleep | task | `a` deadline, `b` now, `c` duration |
| 7 | Wake | task | `a` now, or target task for a join wake; `b` now; `s1` sleep/join/cancel |
| 8 | Exit | task | `a` now, `b` success, `c` task id, `d` lifetime ns, `s1` failure |
| 9 | Cancel | target | `a` now, `s1` reason |
| 10 | Error | task/0 | `a` now, `s1` message |
| 11 | Log | task | `a` now, `d` original bytes, `s1` message (truncated with marker) |
| 12 | Watch | task | `a` now, `s1` name, `s2` value (emitted only on change) |
| 13 | Note | task | `a` now, `s1` key, `s2` value |
| 14 | Rng | task | `a` value, `d` now |
| 15 | ClockJump | — | `a` new time, `b` previous time, `d` now |
| 16 | Join | waiter | `a` target, `b` now |
| 20 | FileRead | task | `a` ok, `b` byte count, `d` now, `s1` path, `s2` error, payload contents |
| 21 | FileWrite | task | `a` ok, `b` input bytes, `d` now, `s1` path, `s2` error, payload data |
| 22 | EnvGet | task | `a` ok, `b` byte count, `d` now, `s1` name, `s2` value or error |
| 23 | SocketConnect | task | `a` port, `b` handle, `c` ok, `d` now, `s1` host, `s2` error |
| 24 | SocketSend | task | `a` handle, `b` bytes, `c` ok, `d` now, `s2` error, payload data |
| 25 | SocketRecv | task | `a` handle, `b` requested max, `c` ok, `d` now, `s2` error, payload bytes |
| 26 | SocketClose | task | `a` handle, `b` ok, `d` now, `s2` error |

Notes:

- Steps record both the scheduler RNG draw (`c`) and the chosen task (`b`). The
  draw is consumed identically on replay even when a recorded decision is
  substituted, so later random draws stay aligned.
- A `Sleep` deadline is absolute virtual nanoseconds. `ClockJump` is emitted
  only when the clock actually moves.
- Capability failures are normal recorded outcomes (`ok = 0`), not crashes.
- Unknown ids from a future writer are preserved raw and displayed as `?`.
- The UI colors events by category: lifecycle, step, capability, state
  (`Watch`/`Note`/`Rng`), and errors/divergence.
