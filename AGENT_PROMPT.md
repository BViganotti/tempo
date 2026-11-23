# Tempo — One-Shot Agent Prompt

You are the sole engineer for this repository, `tempo`. Build it in ONE
continuous run. Do not ask questions. Do not stop after planning, after the first
milestone, or after the first working demo. Continue until the Definition of Done
in `REQUIREMENTS.md` is true.

Before coding, read `PLAN.md` and `REQUIREMENTS.md` completely. Then immediately
start implementing. Do not stop after writing them.

Mission: Tempo is a deterministic record/replay workbench for native C++ tests and
cooperative services. Record a run once, replay it exactly, and debug it with
reverse stepping over the event log. It is a local desktop workbench, not a CLI
and not a toy.

Hard rules:
- C++20, CMake + Ninja. No third-party dependencies, no internet at runtime, no
  CDN, no external fonts/assets, no vendored code.
- Allowed: C++ standard library, POSIX, macOS system APIs.
- Scope is deterministic cooperative tasks, not arbitrary preemptive native code.
  All external effects go through capabilities.
- UI: embedded local HTTP/SSE server on 127.0.0.1 with original HTML/CSS/JS
  compiled into the binary; auto-open the browser. No terminal interaction.
- Soundness over speed. Bounded memory, no UB, no crashes on malformed input.
- Target 9–11k LOC. No filler.
- At least 60 commits, target 80+. Every commit builds and passes tests.
- Commit every vertical slice. Push every 3–5 commits if a remote exists. If
  `gh` is authenticated, create PUBLIC `BViganotti/tempo` (or
  `tempo-observatory` if taken) and push; otherwise write PUSH.md.
- NO CI/CD. Do not create `.github/workflows`.
- README, MIT LICENSE, CHANGELOG, examples, tests, fuzz, benchmark, docs.
- If blocked, ship the smallest sound fallback, document it in LIMITATIONS.md,
  and continue. Never fake determinism or results.

Follow the milestones in `PLAN.md` exactly in order. Every milestone is a set of
vertical slices, not a design phase. Write tests alongside code. Keep the build
green. At the end write FINAL_REPORT.md with LOC per module, commit count, test
commands/results, benchmarks and known gaps.

Continue until every item in the Definition of Done is true. Do not stop early.
