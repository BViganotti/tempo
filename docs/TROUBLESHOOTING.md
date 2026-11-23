# Troubleshooting

## The browser shows 403 Forbidden

Every request needs the current session token. The token is in the URL printed by
the launcher (`/?token=...`). Opening `http://127.0.0.1:<port>/` without it is
rejected by design. Restarting the launcher mints a new token, so old bookmarks
stop working; use the newly printed URL. The `Host` header must also be
`127.0.0.1:<port>` or `localhost:<port>`; a hosts-file alias or reverse proxy
gets 403.

## The browser did not open

Pass `--no-open`, copy the printed URL, and open it manually. On macOS the
launcher invokes `/usr/bin/open`; if that fails (for example in a restricted
session) the server still runs and the URL remains valid.

## Port already in use

Use `--port 0` (ephemeral) or pick another port. Tempo only binds
`127.0.0.1`; it cannot be configured to listen on other addresses.

## No records appear / recording fails

- The data directory must be writable. Default is
  `~/Library/Application Support/Tempo`; pass `--data-dir` to choose another.
- The page auto-records `counter-race` on a cold directory. If that failed, the
  status pill shows an error and a toast describes the cause.
- Scenario fixtures are written under the data root before recording. A
  read-only directory fails with a clear error.

## Import is rejected

- The HTTP body limit for imports is 64 MiB, aligned with the record file cap.
  Copy larger `.tmpo` files directly into `<data-dir>/records/` and reload the
  workbench; they will appear in the archive if they do not fit memory.
- Imports must be strict, complete, CRC-valid records. Truncated files can be
  inspected only when loaded from the records directory in recovery mode.
- Sending `Content-Type: application/json` to `/api/records/import` returns 415;
  imports use `application/octet-stream`.
- Importing the same record twice is idempotent and reports `duplicate: true`.

## A long replay freezes the page

Batch replays (`Replay ×100`) normally run on the background job worker. If the
worker is unavailable, the request is rejected; single replays are synchronous.
A job queue holds at most 16 queued/retained jobs; let existing jobs finish or
restart the workbench. Recording and imports remain synchronous and can briefly
block the UI for very large inputs.

## Replay warns about a build mismatch

The record was written by a different scenario build. The replay still runs so
you can inspect the divergence; use the matching checkout or re-record if you
need a trusted comparison.

## Replay diverges immediately

Reproduce with the same scenario code and seed stored in the record. Changing
task names, watch values, capability order or scheduling changes the event
stream. Read the first-divergence card: it shows the recorded and replayed
events at the exact index. If the record names a scenario that is not registered
by the stock launcher (custom embedder registry), replay is refused rather than
guessed.

## Replaying an old record after changing the demo

Built-in demo code is not versioned alongside records. A record made by an older
build may diverge or refuse if its scenario is gone. Records store the tool
version in metadata; they do not migrate scenario code.

## SSE keeps reconnecting

The badge shows `RECONNECTING` when the event stream drops. Reload the page with
the current URL. SSE is optional: all visible data is also fetched on load, and
the API works without a stream.

## The timeline says “No timeline data”

Switch to another record and back, or reload with the token URL. Timeline data is
fetched when a record is selected; on a cold directory the auto-recording flow
selects it automatically in current builds. Check that the record has not been
evicted from the 16-record memory cap (the file may still exist in `records/`).

## The UI shows fewer than 16 records after restart

The workbench loads up to 16 records within its memory budget. Records that do
not fit are listed under the Records panel’s archive selector; choose one and
press **Load**. Files remain under `<data-dir>/records/`. If the archive selector
is empty, the file was not a valid `.tmpo` or was beyond the 512-file startup
scan cap.

## A record download says 413

Downloads are capped at 64 MiB (the record file cap). A larger file remains under
`<data-dir>/records/`; copy it directly.

## Sanitizer build fails or reports an error

Use the documented commands (`make asan`, `make tsan`) or configure with
`-DTEMPO_SANITIZE=ON` / `-DTEMPO_TSAN=ON`. TSan on macOS requires a matching
Xcode runtime; mismatched toolchains can fail to link. The recorded results in
`docs/VERIFICATION.md` used Apple Clang 21 with Xcode 26.

## Sleeps appear instant

Time is virtual. The clock jumps only when no task is ready, and it jumps
directly to the next deadline. This is expected; wall-clock duration is not part
of replay determinism.

## Files a scenario wrote are missing during replay

Replay intentionally does not perform file writes or reads. It answers from the
record. Create/modify effects only exist during recording.

## Nothing works on Linux/Windows

Tempo is developed and tested on macOS with POSIX APIs. Windows is unsupported.
Linux may work but is not claimed; report exact errors if you try.
