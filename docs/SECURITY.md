# Security model

Tempo runs on the user's machine and exposes a local web UI. The threat model is
a malicious local web page or another local process trying to reach the UI, plus
malformed input in records, requests or scenario configuration.

## Network exposure

- The server binds **only** `127.0.0.1`; `HttpServer::start` rejects any other
  bind address. There is no configuration to expose it on a LAN interface.
- A per-session token is required for every API request, static asset and SSE
  stream. The token is 32 lowercase hex characters minted from `std::random_device`
  by the launcher.
- The token can be sent as `?token=` or `X-Tempo-Token`. The launcher puts it in
  the initial URL and injects it into the index document's CSS/JS/favicon URLs.
- `Host` must match `127.0.0.1:<port>` or `localhost:<port>`. If `Origin` is
  present, it must match the corresponding `http://` origin. This blocks DNS
  rebinding and cross-site requests.
- No CORS headers are emitted, and `OPTIONS` is rejected with 405. Only `GET`
  and `POST` are accepted. POST bodies with content must be `application/json`
  (the server returns 415 otherwise).
- Responses set `Cache-Control: no-store`, `X-Content-Type-Options: nosniff`,
  `Referrer-Policy: no-referrer`, and a restrictive CSP:
  `default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:;
  connect-src 'self'; frame-ancestors 'none'; base-uri 'none'`.
- No external fonts, scripts, images or CDNs are referenced. All assets are
  original and compiled into the binary.
- SSE broadcasts go through a bounded queue (128 frames); each connection has a
  bounded output buffer. Slow or hostile clients are closed instead of growing
  memory without limit.

The token is visible in the browser URL and therefore in local history. Tempo
does not log it, and `Referrer-Policy: no-referrer` prevents leakage to external
sites. It is not a replacement for OS user isolation; any process that can read
the user's memory or data directory can read records.

## Input hardening

- HTTP: 16 KiB headers, 256 KiB body, 32 connections, 64 headers, origin-form
  targets only, duplicate headers rejected, obsolete folding rejected,
  `Transfer-Encoding` rejected, query ≤ 2 KiB, 15-second request deadline.
- JSON: 1 MiB input, depth 32, 100,000 array items, 2,048 object fields, 65,536-byte
  strings, duplicate keys rejected, non-finite numbers rejected.
- Records: 64 MiB file cap, 1,000,000 events, 8 KiB event strings, 64 KiB event
  payloads, per-event CRC, body CRC and footer CRC; strict mode requires a
  complete file.
- Replay: max 200 runs per batch, step/task/time limits from `SchedulerOptions`.
- Breakpoints: 128 per workbench, 128-byte text. Records: 16 in memory.
- The UI downloads records through a 4 MiB response cap; larger files can be
  copied from the data directory directly.

`HttpServer` is single-threaded. `Workbench` is documented as single-threaded
and is only called by the loop thread (or a test thread). The SSE broadcast queue
is the only cross-thread state and is mutex-protected; TSan covers the server
integration tests.

## Filesystem sandbox

`SystemHost` implements file capabilities relative to a configured `data_root`:

- relative paths only; `..`, empty components, absolute paths and NUL are
  rejected;
- symlinks are rejected with `O_NOFOLLOW` on reads and writes;
- regular files only; reads and writes capped at 65,536 bytes;
- writes create files with mode `0600`;
- there are no unlink, rename, chmod or directory-listing capabilities.

The workbench writes scenario fixtures under the same root and record files under
`<data_root>/records/` with mode `0600`. Records may contain file contents that a
scenario read; treat the data directory as sensitive.

Sockets are limited to loopback and use bounded send/receive timeouts. Replay
never opens a socket.

## Non-goals

- Tempo does not sandbox the scenario's native code. A scenario is C++ code that
  the embedding application compiled; it can call any OS API. Use a process
  sandbox if you do not trust the scenario.
- Tempo does not encrypt or sign records. CRC32 detects accidental corruption,
  not tampering.
- The virtual RNG is deterministic, not cryptographic.
