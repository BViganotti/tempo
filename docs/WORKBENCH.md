# Workbench and HTTP API

The launcher starts an embedded HTTP/1.1 + SSE server on `127.0.0.1`, injects the
session token into the embedded index document, and opens the browser. There is
no terminal workflow; `Ctrl+C` stops it.

## Data directory

Default (macOS): `~/Library/Application Support/Tempo`; override with
`--data-dir`. Layout:

```
Tempo/
  records/rec-<created-unix>-<hash8>-<scenario>.tmpo
  fixtures/...            # written by built-in scenarios before recording
```

Records are saved atomically with mode `0600`. Data, records and fixture
directories are chmod'ed owner-only. The workbench keeps at most 16 records in
memory and enforces an approximate global memory budget (default 256 MiB,
`--max-memory-mb`). Records evicted by the cap or budget are listed in the
archive and can be reloaded without restarting. Record IDs are process-local and
assigned in creation order; the full record hash is exposed as a stable `uid`.
Duplicate hashes are skipped on startup.

## Endpoints

All endpoints require the session token (query `token` or `X-Tempo-Token`) and a
loopback `Host`. JSON responses use `application/json; charset=utf-8`. Errors are
`{"error":"..."}` with an HTTP 4xx/5xx status.

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | embedded `index.html` |
| GET | `/style.css`, `/app.js`, `/favicon.svg` | embedded assets |
| GET | `/api/overview` | version, scenarios, records, breakpoints, archive, memory, warnings |
| GET | `/api/health` | readiness, record count, memory usage/budget |
| GET | `/api/stream` | SSE: initial comment, `overview` and `replay` events |
| POST | `/api/record` | `{scenario, seed, mutateStep?, mutateChoice?}` |
| POST | `/api/replay` | `{recordId, runs?, mutateStep?, mutateChoice?, async?}` |
| GET | `/api/jobs` | replay job list |
| GET | `/api/jobs/{id}` | job status and eventual result |
| GET | `/api/archive` | records kept on disk outside the memory cap |
| POST | `/api/records/open` | `{file}` reload an archived record |
| GET | `/api/records/{id}/events` | `from`, `limit` (≤500) event page |
| GET | `/api/records/{id}/frame` | `index`; event, exact watch state |
| GET | `/api/records/{id}/timeline` | lane summaries plus bounded ticks |
| GET | `/api/records/{id}/download` | portable `.tmpo` (≤64 MiB via HTTP) |
| POST | `/api/records/{id}/delete` | `{deleteFile?}` remove memory and optionally file |
| GET | `/api/records/{id}/diff` | `other=<id>`; first difference + counts |
| GET | `/api/records/{id}/breakpoints/next` | `from`, `direction=1|-1` |
| GET | `/api/breakpoints` | list breakpoints |
| POST | `/api/breakpoints` | `{type?, task?, text?}`; at least one criterion |
| POST | `/api/breakpoints/toggle` | `{id, enabled}` |
| POST | `/api/breakpoints/remove` | `{id}` |
| POST | `/api/breakpoints/clear` | remove all |

`/api/record` accepts `seed` as a JSON number or a decimal string, so the full
64-bit seed range is addressable. It returns `{ok, runOk, summary, record?,
error?, warning?}`. A run that
failed still returns a complete, replayable record; `runOk` describes the run and
`ok` describes capture. `record` contains metadata, event count, hash, seed,
creation time, completeness and warning.

With `async: true`, `/api/replay` returns `202 {ok, jobId, status}` immediately
and the batch runs on the background worker. Poll `/api/jobs/{id}`:
`{id, status: queued|running|done|error, runs, result?, error?}`. The result has
the same shape as the synchronous response.

`/api/replay` returns `{ok, recordId, runs, identical, allHashesEqual,
allIdentical, replayedHash, expectedHash, recordedEvents, replayedEvents,
elapsedNs, hashes[], divergence, buildMismatch?, buildWarning?, summary?,
failedRun?, mutationApplied?, error?}`.
For a single run, `divergence` contains `found`, `index`, `reason`,
`expectedSummary`, `actualSummary` and presence flags. For batches, `failedRun`
is the 0-based first failing run or `-1`.

## SSE

`/api/stream` sends `: tempo stream connected` and then frames such as:

```
event: overview
data: {"records":[...],"breakpoints":[...],"lastRecord":1}

event: replay
data: {"recordId":1,"runs":100,"identical":true,...}
```

The server also sends `: keepalive` comments every 15 seconds. Broadcasts are
bounded (128 queued frames) and are delivered by the poll loop.

## UI walkthrough

1. **Record** — pick a scenario, full-width seed string, and optionally an
   altered scheduling step. The page auto-records `counter-race` on a fresh data
   directory. Import a `.tmpo` file (up to 64 MiB) or load an archived one.
2. **Records** — click a record to load its timeline, event window and inspector.
3. **Timeline** — one lane per task; event ticks are colored by category. Click
   anywhere to jump to the nearest index; the gold line is the current event.
4. **Step** — buttons, scrubber, arrow keys, Home/End. Every step asks the server
   for the exact watch state at that index. Event rows are keyboard-focusable;
   event type/task filters page over matching events.
5. **Breakpoints** — add by type/task/text, toggle or remove; run forward or
   backward to the next match.
6. **Replay** — `Replay` proves a single run; `Replay ×100` checks hash stability
   on the background worker and polls job status, so the page stays responsive.
   A build-id mismatch is surfaced in the result.
7. **Altered schedule** — set a step and choice, then replay. The first
   divergence is shown with both event summaries; the timeline marks it red.
8. **Run diff** — compare two records; shows the first differing event and
   per-type counts.
9. **Export .tmpo** — download the portable record container.

Everything in the page is original HTML/CSS/JavaScript compiled into the binary.
No fonts, scripts, images or network resources are fetched from the internet.

## Static asset authentication

The index document is served with the token appended to its own CSS/JS/favicon
URLs. Asset requests without a valid token return 403. The browser page therefore
never needs unauthenticated resource access.
