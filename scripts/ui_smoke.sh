#!/bin/sh
# End-to-end smoke: launcher, tokenized assets, record, replay x100, mutation,
# breakpoint navigation, and an optional headless Chromium DOM check.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD=${BUILD:-$ROOT/build}
BIN="$BUILD/tempo_workbench"
if [ ! -x "$BIN" ]; then
  echo "ui_smoke: build tempo_workbench first ($BIN missing)" >&2
  exit 1
fi

DATA=$(mktemp -d "${TMPDIR:-/tmp}/tempo-ui-smoke.XXXXXX")
LOG="$DATA/launcher.log"
cleanup() {
  if [ -n "${SERVER_PID:-}" ]; then kill "$SERVER_PID" 2>/dev/null || true; fi
  rm -rf "$DATA"
}
trap cleanup EXIT INT TERM

"$BIN" --no-open --data-dir "$DATA" --port 0 >"$LOG" 2>&1 &
SERVER_PID=$!
URL=""
i=0
while [ "$i" -lt 100 ]; do
  URL=$(grep -o 'http://127.0.0.1:[0-9]*/?token=[0-9a-f]*' "$LOG" 2>/dev/null | head -1 || true)
  [ -n "$URL" ] && break
  i=$((i + 1))
  sleep 0.05
done
if [ -z "$URL" ]; then
  echo "ui_smoke: launcher did not print a URL" >&2
  cat "$LOG" >&2
  exit 1
fi
TOKEN=${URL##*token=}
BASE=$(printf '%s' "$URL" | sed -E 's#(http://127.0.0.1:[0-9]+)/.*#\1#')

fail() { echo "ui_smoke: $*" >&2; exit 1; }

INDEX=$(curl -fsS "$URL") || fail "index fetch failed"
printf '%s' "$INDEX" | grep -q 'Tempo' || fail "index missing title"
printf '%s' "$INDEX" | grep -q "style.css?token=$TOKEN" || fail "css token not injected"
printf '%s' "$INDEX" | grep -q "app.js?token=$TOKEN" || fail "js token not injected"
curl -fsS "$BASE/style.css?token=$TOKEN" >/dev/null || fail "css fetch failed"
curl -fsS "$BASE/app.js?token=$TOKEN" >/dev/null || fail "js fetch failed"

RECORD=$(curl -fsS -X POST "$BASE/api/record?token=$TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"scenario":"counter-race","seed":7}') || fail "record failed"
printf '%s' "$RECORD" | grep -q '"ok":true' || fail "record response not ok"
printf '%s' "$RECORD" | grep -q '"events":' || fail "record response has no events"
ID=$(printf '%s' "$RECORD" | sed -n 's/.*"record":{[^}]*"id":\([0-9]*\).*/\1/p' | head -1)
[ -n "$ID" ] || fail "record id missing"

REPLAY=$(curl -fsS -X POST "$BASE/api/replay?token=$TOKEN" \
  -H 'Content-Type: application/json' -d "{\"recordId\":$ID,\"runs\":100}") \
  || fail "replay failed"
printf '%s' "$REPLAY" | grep -q '"identical":true' || fail "100-run replay diverged"
printf '%s' "$REPLAY" | grep -q '"allHashesEqual":true' || fail "hashes not stable"
printf '%s' "$REPLAY" | grep -q '"runs":100' || fail "run count wrong"

MUTATED=$(curl -fsS -X POST "$BASE/api/replay?token=$TOKEN" \
  -H 'Content-Type: application/json' \
  -d "{\"recordId\":$ID,\"mutateStep\":1}") || fail "mutation replay failed"
printf '%s' "$MUTATED" | grep -q '"found":true' || fail "mutation did not diverge"

FRAME=$(curl -fsS "$BASE/api/records/$ID/frame?index=0&token=$TOKEN") || fail "frame failed"
printf '%s' "$FRAME" | grep -q '"state":' || fail "frame has no state"

if [ -x "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" ]; then
  CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
  DOM="$DATA/dom.html"
  "$CHROME" --headless=new --disable-gpu --no-first-run --no-default-browser-check \
    --user-data-dir="$DATA/chrome" --virtual-time-budget=8000 --dump-dom \
    "$URL&nostream=1&selftest=1" >"$DOM" 2>/dev/null &
  CHROME_PID=$!
  i=0
  while [ "$i" -lt 60 ]; do
    if ! kill -0 "$CHROME_PID" 2>/dev/null; then break; fi
    i=$((i + 1))
    sleep 0.2
  done
  kill "$CHROME_PID" 2>/dev/null || true
  wait "$CHROME_PID" 2>/dev/null || true
  if [ -s "$DOM" ]; then
    grep -q 'data-tempo-selftest="counter-race/' "$DOM" ||
      fail "DOM self-test did not reach the demo record"
    grep -q 'Timeline' "$DOM" || fail "DOM did not render the timeline panel"
    grep -q 'id="filter-type"' "$DOM" || fail "DOM missing event filters"
    grep -q 'id="archive-select"' "$DOM" || fail "DOM missing archive controls"
  else
    echo "ui_smoke: chromium produced no DOM (non-fatal)" >&2
  fi
else
  echo "ui_smoke: chromium not installed; skipped DOM check" >&2
fi

echo "ui_smoke: OK (record $ID, 100 identical replays, mutation divergence found)"
