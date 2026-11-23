# Requirements

## Product
- The product is a local desktop developer workbench, not a CLI and not a terminal UI.
- The UI is a local web application served only on `127.0.0.1` with an embedded HTTP/SSE server, a random auth token, and all HTML/CSS/JS assets compiled into the binary.
- The launcher starts the server and opens the system browser. It must not require terminal interaction.
- The C++ core must also be usable as an embeddable library and testable headlessly.
- Original work. Not related to Git, version control, branching, diffs or history management.

## Language and dependencies
- C++20, CMake and Ninja.
- No third-party dependencies, vendored code, package managers or external assets.
- Allowed: C++ standard library, POSIX, and macOS system APIs.
- No OpenSSL, SQLite, zlib, Boost, Qt, SDL, ImGui, libunwind, or similar.
- No internet at runtime. No CDN. No external fonts. Pushing to GitHub is allowed.

## Quality
- Soundness over speed. Correctness, bounded memory, deterministic behavior and no UB matter more than performance.
- Modest performance is acceptable; unbounded memory, crashes on malformed input and silent truncation are not.
- Hard limits on parsed lengths, recursion, query depth, string sizes and UI request sizes.
- Every public API documented. Thread-safety and lifetime rules stated.
- Unit tests, property tests, fuzz tests and malformed-input tests.
- ASan/UBSan clean. TSan clean for multithreaded paths.
- No fake results or fabricated benchmarks.

## Delivery
- Target 9–11k LOC, excluding generated/embedded UI assets. Do not pad with boilerplate.
- At least 60 commits, target 80+. Every commit builds and passes its tests.
- Commit after every vertical slice. Push every 3–5 commits if a remote exists.
- No CI/CD. Do not create `.github/workflows` or any CI configuration.
- README, MIT LICENSE, CHANGELOG, examples, benchmark and `FINAL_REPORT.md` required.
- If blocked, ship the smallest sound fallback, document it in `LIMITATIONS.md`, and continue.
- Never pretend a feature works.

## Definition of Done
1. `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` succeeds.
2. `cmake --build build -j` succeeds with zero warnings.
3. `ctest --test-dir build --output-on-failure` passes.
4. ASan/UBSan and TSan runs pass and are documented.
5. Launcher opens the local UI; the demo scenario works.
6. README, docs, CHANGELOG, LICENSE, LIMITATIONS and FINAL_REPORT exist.
7. FINAL_REPORT lists LOC, commits, test commands/results, benchmarks and gaps.
8. Repo is committed and pushed, or PUSH.md contains exact push commands.
9. No external dependency, no CI, no CLI-only product.
