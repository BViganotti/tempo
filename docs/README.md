# Documentation index

Start with the [README](../README.md) for the product and quickstart.

| Document | What it covers |
|---|---|
| [ARCHITECTURE.md](ARCHITECTURE.md) | Layer diagram, determinism boundary, threading, bounded resources |
| [API.md](API.md) | Public C++ API, lifetimes, recording/replay examples |
| [RECORD_FORMAT.md](RECORD_FORMAT.md) | Byte-level v1 container, CRCs, recovery |
| [EVENT_TYPES.md](EVENT_TYPES.md) | Stable numeric event ids and field meanings |
| [DEBUGGER.md](DEBUGGER.md) | Watch-state reconstruction, checkpoints, breakpoints, divergence |
| [SCENARIOS.md](SCENARIOS.md) | Built-ins, writing replayable scenarios, registry |
| [WORKBENCH.md](WORKBENCH.md) | HTTP/SSE endpoints, data layout, UI walkthrough |
| [SECURITY.md](SECURITY.md) | Loopback threat model, token/Host/Origin checks, sandbox |
| [BUILD.md](BUILD.md) | Prerequisites, options, launcher flags, asset embedding |
| [TESTING.md](TESTING.md) | Test matrix, fuzzers, sanitizer commands |
| [VERIFICATION.md](VERIFICATION.md) | Recorded release/sanitizer/fuzz/bench/browser results |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | Common failures and exact remedies |
| [../LIMITATIONS.md](../LIMITATIONS.md) | Honest scope boundaries |
| [../FINAL_REPORT.md](../FINAL_REPORT.md) | LOC, commits, test/benchmark results, gaps |

The UI assets under `ui/` are original and embedded at build time; there are no
external documentation assets to fetch.
