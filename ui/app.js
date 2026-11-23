(() => {
  "use strict";

  const token = new URLSearchParams(location.search).get("token") || "";
  const $ = (id) => document.getElementById(id);

  const EVENT_TYPES = [
    "ScenarioStart", "ScenarioEnd", "Spawn", "Step", "Yield", "Sleep", "Wake",
    "Exit", "Cancel", "Error", "Log", "Watch", "Note", "Rng", "ClockJump",
    "Join", "FileRead", "FileWrite", "EnvGet", "SocketConnect", "SocketSend",
    "SocketRecv", "SocketClose"
  ];

  const state = {
    scenarios: [],
    records: [],
    breakpoints: [],
    current: null,
    record: null,
    events: [],
    windowFrom: 0,
    total: 0,
    index: 0,
    frame: null,
    timeline: null,
    lastReplay: null,
    divergence: null,
    diff: null,
    autoRecordTried: false,
    fetchSeq: 0
  };

  let toastTimer = null;

  function setStatus(text, mode = "") {
    const el = $("status");
    el.textContent = text;
    el.className = "status" + (mode ? " " + mode : "");
  }

  function toast(message, isError = false) {
    const el = $("toast");
    el.textContent = message;
    el.className = "toast show" + (isError ? " error" : "");
    if (toastTimer) clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { el.className = "toast"; }, 4200);
  }

  function withToken(path) {
    const url = new URL(path, location.origin);
    url.searchParams.set("token", token);
    return url.pathname + url.search;
  }

  async function api(path, options = {}) {
    const headers = Object.assign({}, options.headers || {});
    if (options.body) headers["Content-Type"] = "application/json";
    const response = await fetch(withToken(path), Object.assign({}, options, { headers }));
    const text = await response.text();
    let data = {};
    if (text) {
      try { data = JSON.parse(text); } catch { throw new Error("server returned invalid JSON"); }
    }
    if (!response.ok) throw new Error(data.error || ("HTTP " + response.status));
    return data;
  }

  function clamp(value, low, high) { return Math.max(low, Math.min(high, value)); }

  function fmtNs(ns) {
    if (typeof ns !== "number" || ns < 0) return "—";
    if (ns < 1e3) return ns + "ns";
    if (ns < 1e6) return (ns / 1e3).toFixed(1) + "µs";
    if (ns < 1e9) return (ns / 1e6).toFixed(2) + "ms";
    return (ns / 1e9).toFixed(3) + "s";
  }

  function fmtMs(ns) {
    if (typeof ns !== "number") return "—";
    return (ns / 1e6).toFixed(2) + " ms";
  }

  function shortHash(hash) { return (hash || "").slice(0, 8); }

  function typeClass(name) {
    if (name === "Step") return "step";
    if (name === "Exit" || name === "ScenarioEnd") return "exit";
    if (name === "Error" || name === "Cancel") return "err";
    if (name === "Watch" || name === "Note" || name === "Rng") return "state";
    if (["FileRead","FileWrite","EnvGet","SocketConnect","SocketSend","SocketRecv","SocketClose"].includes(name)) return "cap";
    return "";
  }

  function el(tag, cls, text) {
    const node = document.createElement(tag);
    if (cls) node.className = cls;
    if (text !== undefined && text !== null) node.textContent = String(text);
    return node;
  }

  function renderScenarioOptions() {
    const select = $("scenario");
    const previous = select.value;
    select.replaceChildren();
    for (const scenario of state.scenarios) {
      const option = el("option", "", scenario.name);
      option.value = scenario.name;
      select.appendChild(option);
    }
    if (previous && state.scenarios.some((s) => s.name === previous)) select.value = previous;
    updateScenarioNote();
  }

  function updateScenarioNote() {
    const selected = state.scenarios.find((s) => s.name === $("scenario").value);
    $("scenario-note").textContent = selected ? selected.description : "";
  }

  function renderRecords() {
    const list = $("records");
    list.replaceChildren();
    $("record-count").textContent = String(state.records.length);
    if (!state.records.length) {
      list.appendChild(el("li", "empty", "No records yet. Record a scenario to begin."));
      return;
    }
    for (const record of state.records.slice().reverse()) {
      const li = el("li", "record" + (record.id === state.current ? " active" : ""));
      li.tabIndex = 0;
      const top = el("div", "top");
      top.appendChild(el("span", "name", record.scenario));
      top.appendChild(el("span", "badge", "#" + record.id));
      li.appendChild(top);
      const meta = el("div", "meta");
      meta.appendChild(el("span", "", record.events + " events"));
      meta.appendChild(el("span", "", "seed " + record.seed));
      meta.appendChild(el("span", "", shortHash(record.hash)));
      if (record.recovered) meta.appendChild(el("span", "warn", "recovered"));
      li.appendChild(meta);
      const activate = () => selectRecord(record.id);
      li.addEventListener("click", activate);
      li.addEventListener("keydown", (event) => {
        if (event.key === "Enter" || event.key === " ") { event.preventDefault(); activate(); }
      });
      list.appendChild(li);
    }
  }

  function renderBreakpoints() {
    const list = $("breakpoints");
    list.replaceChildren();
    if (!state.breakpoints.length) {
      list.appendChild(el("li", "empty", "No breakpoints. Add one above."));
      return;
    }
    for (const breakpoint of state.breakpoints) {
      const li = el("li", "breakpoint" + (breakpoint.enabled ? "" : " off"));
      li.appendChild(el("span", "bp-dot" + (breakpoint.enabled ? "" : " off")));
      let label = "";
      if (breakpoint.type) label += breakpoint.type;
      if (breakpoint.task !== null && breakpoint.task !== undefined) label += " task " + breakpoint.task;
      if (breakpoint.text) label += ' "' + breakpoint.text + '"';
      li.appendChild(el("span", "label", label || "any event"));
      const toggle = el("button", "", breakpoint.enabled ? "off" : "on");
      toggle.title = breakpoint.enabled ? "Disable" : "Enable";
      toggle.addEventListener("click", () => toggleBreakpoint(breakpoint));
      li.appendChild(toggle);
      const remove = el("button", "", "×");
      remove.title = "Remove";
      remove.addEventListener("click", () => removeBreakpoint(breakpoint.id));
      li.appendChild(remove);
      list.appendChild(li);
    }
  }

  function renderReplay(result) {
    const box = $("replay-result");
    box.replaceChildren();
    if (!result) {
      box.appendChild(el("p", "empty", "Replay the record to prove the event stream is stable, or alter one schedule decision."));
      $("replay-note").textContent = "";
      return;
    }
    const stats = [
      ["Runs", String(result.runs), ""],
      ["Identical", result.identical ? "yes" : "no", result.identical ? "ok" : "bad"],
      ["Hashes stable", result.allHashesEqual ? "yes" : "no", result.allHashesEqual ? "ok" : "bad"],
      ["Replay hash", shortHash(result.replayedHash), "mono"],
      ["Expected hash", shortHash(result.expectedHash), "mono"],
      ["Elapsed", fmtMs(result.elapsedNs), ""],
      ["Recorded events", String(result.recordedEvents), ""],
      ["Replayed events", String(result.replayedEvents), ""],
      ["First failing run", result.failedRun >= 0 ? "#" + (result.failedRun + 1) : "—", result.failedRun >= 0 ? "bad" : ""],
      ["Build id", result.buildMismatch ? "mismatch" : "matches", result.buildMismatch ? "bad" : "ok"]
    ];
    for (const [key, value, cls] of stats) {
      const card = el("div", "stat");
      card.appendChild(el("div", "k", key));
      card.appendChild(el("div", "v " + cls, value));
      box.appendChild(card);
    }
    $("replay-note").textContent = result.error ? result.error :
      (result.buildWarning ? result.buildWarning :
        (result.allIdentical ? "100% event-identical replay" : "event stream diverged"));
    if (result.error) {
      const card = el("div", "stat");
      card.appendChild(el("div", "k", "Error"));
      card.appendChild(el("div", "v bad", result.error));
      box.appendChild(card);
    }
  }

  function renderDivergence(divergence) {
    const box = $("divergence");
    box.replaceChildren();
    if (!divergence || !divergence.found) {
      $("frame-badge").classList.add("hidden");
      return;
    }
    box.appendChild(el("div", "head", "First divergence at event " + divergence.index + " — " + divergence.reason));
    const pair = el("div", "pair");
    const sides = [
      ["recorded", divergence.expectedSummary || "(no event)"],
      ["replayed", divergence.actualSummary || "(no event)"]
    ];
    for (const [label, body] of sides) {
      const side = el("div", "side");
      side.appendChild(el("div", "label", label));
      side.appendChild(el("div", "body", body));
      pair.appendChild(side);
    }
    box.appendChild(pair);
  }

  function renderDiff(result) {
    const box = $("diff-result");
    box.replaceChildren();
    if (!result) {
      box.appendChild(el("p", "empty", "Pick another record to compare event streams."));
      return;
    }
    const summary = el("p", "hint");
    summary.textContent = result.equal
      ? "Streams are identical."
      : "Divergence at event " + result.first.index + ": " + result.first.reason;
    box.appendChild(summary);
    if (result.first.found) {
      const pair = el("div", "split2");
      for (const [label, text] of [["record A", result.first.summaryA || "(none)"], ["record B", result.first.summaryB || "(none)"]]) {
        const side = el("div", "side");
        side.appendChild(el("div", "label", label));
        side.appendChild(el("div", "body", text));
        pair.appendChild(side);
      }
      box.appendChild(pair);
    }
    if (result.counts && result.counts.length) {
      const table = el("table");
      const head = el("thead");
      const headRow = el("tr");
      for (const label of ["Event", "A", "B"]) headRow.appendChild(el("th", "", label));
      head.appendChild(headRow);
      table.appendChild(head);
      const body = el("tbody");
      for (const count of result.counts) {
        const row = el("tr");
        row.appendChild(el("td", "", count.type));
        row.appendChild(el("td", "num", count.a));
        row.appendChild(el("td", "num", count.b));
        body.appendChild(row);
      }
      table.appendChild(body);
      box.appendChild(table);
    }
  }

  function renderInspector() {
    const detail = $("event-detail");
    detail.replaceChildren();
    const frame = state.frame;
    if (!frame || !frame.event) {
      detail.appendChild(el("p", "empty", "Select an event."));
    } else {
      const event = frame.event;
      const title = el("div", "title");
      title.appendChild(el("span", "type-chip " + typeClass(event.type), event.type));
      title.appendChild(el("span", "dim mono", "#" + frame.index));
      detail.appendChild(title);
      const dl = el("dl");
      const add = (key, value) => {
        if (value === undefined || value === null || value === "") return;
        dl.appendChild(el("dt", "", key));
        dl.appendChild(el("dd", "", value));
      };
      add("summary", event.summary);
      if (event.task) add("task", event.task);
      if (event.a) add("a", event.a);
      if (event.b) add("b", event.b);
      if (event.c) add("c", event.c);
      if (event.d) add("d", event.d);
      if (event.time) add("time", fmtNs(event.time));
      if (event.s1) add("s1", event.s1);
      if (event.s2) add("s2", event.s2);
      if (event.bytesLen) add("bytes", event.bytesLen + " bytes");
      detail.appendChild(dl);
    }

    const watch = $("watch-state");
    watch.replaceChildren();
    const frameState = frame && frame.state ? frame.state : {};
    const tasks = Object.keys(frameState).sort((a, b) => Number(a) - Number(b));
    if (!tasks.length) {
      watch.appendChild(el("p", "empty", "No watch variables at this event."));
    } else {
      for (const task of tasks) {
        const block = el("div", "watch-task");
        block.appendChild(el("div", "name", "task " + task));
        for (const [key, value] of Object.entries(frameState[task])) {
          const row = el("div", "watch-row");
          row.appendChild(el("span", "k", key));
          row.appendChild(el("span", "v", value));
          block.appendChild(row);
        }
        watch.appendChild(block);
      }
    }
    const badge = $("frame-badge");
    badge.classList.toggle("hidden", !(state.divergence && state.divergence.index === state.index));
  }

  function renderEvents() {
    const body = $("event-body");
    body.replaceChildren();
    if (!state.events.length) {
      const row = el("tr");
      const cell = el("td", "", "No events loaded.");
      cell.colSpan = 5;
      row.appendChild(cell);
      body.appendChild(row);
      $("event-page").textContent = "";
      return;
    }
    for (const event of state.events) {
      const row = el("tr");
      if (event.i === state.index) row.classList.add("selected");
      if (state.divergence && event.i === state.divergence.index) row.classList.add("divergent");
      row.appendChild(el("td", "num", event.i));
      row.appendChild(el("td", "", ""));
      row.lastChild.appendChild(el("span", "type-chip " + typeClass(event.type), event.type));
      row.appendChild(el("td", "task", event.task || "—"));
      row.appendChild(el("td", "time", fmtNs(event.time)));
      row.appendChild(el("td", "summary", event.summary));
      row.addEventListener("click", () => setIndex(event.i));
      body.appendChild(row);
    }
    const end = Math.min(state.total, state.windowFrom + state.events.length);
    $("event-page").textContent = "showing " + (state.total ? state.windowFrom + 1 : 0) + "–" + end + " of " + state.total;
  }

  function drawTimeline() {
    const canvas = $("timeline");
    const wrap = canvas.parentElement;
    const width = wrap.clientWidth;
    const height = wrap.clientHeight || 220;
    if (width <= 0) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.round(width * dpr);
    canvas.height = Math.round(height * dpr);
    const ctx = canvas.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = "#080d15";
    ctx.fillRect(0, 0, width, height);

    const lanes = state.timeline && state.timeline.lanes ? state.timeline.lanes : [];
    const labelWidth = 128;
    const padRight = 16;
    const padTop = 10;
    const total = Math.max(1, (state.record ? state.record.events : 1) - 1);
    const plotWidth = Math.max(40, width - labelWidth - padRight);

    ctx.font = "11px ui-monospace, Menlo, monospace";
    if (!lanes.length) {
      ctx.fillStyle = "#5f708a";
      ctx.fillText("No timeline data.", labelWidth + 10, 30);
      return;
    }
    const laneHeight = Math.max(22, Math.min(42, (height - padTop - 16) / lanes.length));
    const colors = {
      step: "#57a6ff", life: "#7ee2a8", cap: "#6fd6d0", state: "#b392f0",
      bad: "#ff6b6b", other: "#8b9bb4"
    };

    ctx.strokeStyle = "#121b28";
    ctx.lineWidth = 1;
    for (let g = 0; g <= 10; g += 1) {
      const x = labelWidth + (plotWidth * g) / 10;
      ctx.beginPath();
      ctx.moveTo(x, padTop);
      ctx.lineTo(x, padTop + laneHeight * lanes.length);
      ctx.stroke();
    }

    lanes.forEach((lane, laneIndex) => {
      const y = padTop + laneIndex * laneHeight;
      const mid = y + laneHeight / 2;
      if (laneIndex % 2 === 0) {
        ctx.fillStyle = "rgba(255,255,255,0.018)";
        ctx.fillRect(labelWidth, y, plotWidth, laneHeight);
      }
      ctx.fillStyle = "#3a4c68";
      ctx.fillText("t" + lane.task, 8, mid + 4);
      ctx.fillStyle = lane.failed ? "#ff8b8b" : "#9fb2cc";
      const name = (lane.name || ("task " + lane.task)).slice(0, 13);
      ctx.fillText(name, 38, mid + 4);
      ctx.strokeStyle = "#182334";
      ctx.beginPath();
      ctx.moveTo(labelWidth, y + laneHeight);
      ctx.lineTo(width - padRight, y + laneHeight);
      ctx.stroke();

      const events = lane.events || [];
      for (const event of events) {
        const x = labelWidth + (event.i / total) * plotWidth;
        let cls = "other";
        if (event.type === "Step") cls = "step";
        else if (["Spawn","Exit","ScenarioStart","ScenarioEnd","Join","Sleep","Wake","Yield"].includes(event.type)) cls = "life";
        else if (["FileRead","FileWrite","EnvGet","SocketConnect","SocketSend","SocketRecv","SocketClose"].includes(event.type)) cls = "cap";
        else if (["Watch","Note","Rng"].includes(event.type)) cls = "state";
        if (event.type === "Error") cls = "bad";
        ctx.fillStyle = colors[cls];
        ctx.globalAlpha = event.type === "Step" ? 0.9 : 0.75;
        const tickHeight = Math.max(3, laneHeight * 0.32);
        ctx.fillRect(x, mid - tickHeight / 2, 1.6, tickHeight);
      }
      ctx.globalAlpha = 1;
    });

    if (state.record) {
      const x = labelWidth + (state.index / total) * plotWidth;
      ctx.strokeStyle = "#f0c674";
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.moveTo(x, padTop - 2);
      ctx.lineTo(x, padTop + laneHeight * lanes.length);
      ctx.stroke();
      ctx.fillStyle = "#f0c674";
      ctx.beginPath();
      ctx.arc(x, padTop - 2, 3, 0, Math.PI * 2);
      ctx.fill();
    }
    if (state.divergence && state.divergence.found) {
      const x = labelWidth + (state.divergence.index / total) * plotWidth;
      ctx.strokeStyle = "#ff6b6b";
      ctx.lineWidth = 1.5;
      ctx.setLineDash([4, 4]);
      ctx.beginPath();
      ctx.moveTo(x, padTop);
      ctx.lineTo(x, padTop + laneHeight * lanes.length);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = "#ff6b6b";
      ctx.fillText("divergence", Math.min(x + 6, width - 80), padTop + 4);
    }
  }

  async function loadOverview(preferredId) {
    const data = await api("/api/overview");
    state.scenarios = data.scenarios || [];
    state.records = data.records || [];
    state.breakpoints = data.breakpoints || [];
    renderScenarioOptions();
    renderRecords();
    renderBreakpoints();
    updateDiffOptions();
    if (!state.records.length) {
      if (!state.autoRecordTried) {
        state.autoRecordTried = true;
        try {
          await recordRun("counter-race", 7, null, null, true);
        } catch (error) {
          toast("Auto-record failed: " + error.message, true);
        }
      } else {
        setStatus("idle");
      }
      return;
    }
    let id = preferredId || state.current;
    if (!state.records.some((record) => record.id === id)) {
      id = data.lastRecord || state.records[state.records.length - 1].id;
    }
    if (id !== state.current || !state.record || state.timeline === null) {
      await selectRecord(id);
    } else {
      const meta = state.records.find((record) => record.id === id);
      if (meta) { state.record = meta; renderRecords(); }
    }
  }

  async function recordRun(scenario, seed, mutateStep, mutateChoice, quiet = false) {
    setStatus("recording", "busy");
    try {
      const body = { scenario, seed: String(seed) };
      if (mutateStep !== null && mutateStep !== undefined) {
        body.mutateStep = mutateStep;
        body.mutateChoice = mutateChoice || 0;
      }
      const data = await api("/api/record", { method: "POST", body: JSON.stringify(body) });
      if (!quiet) {
        const warning = data.warning ? " (" + data.warning + ")" : "";
        toast("Recorded " + data.record.scenario + " — " + data.record.events + " events" + warning);
      }
      await loadOverview(data.record.id);
      setStatus("live", "live");
    } catch (error) {
      setStatus("error", "error");
      toast("Record failed: " + error.message, true);
      throw error;
    }
  }

  async function selectRecord(id) {
    const meta = state.records.find((record) => record.id === id);
    if (!meta) return;
    state.current = id;
    state.record = meta;
    state.events = [];
    state.windowFrom = 0;
    state.total = meta.events;
    state.timeline = null;
    state.index = 0;
    state.lastReplay = null;
    state.divergence = null;
    state.frame = null;
    $("session-hash").textContent = shortHash(meta.hash);
    $("download").href = withToken("/api/records/" + id + "/download");
    $("delete-record").disabled = false;
    renderRecords();
    renderReplay(null);
    renderDivergence(null);
    renderDiff(null);
    try {
      const timeline = await api("/api/records/" + id + "/timeline");
      state.timeline = timeline;
      $("timeline-note").textContent = timeline.truncated
        ? "Timeline shows a bounded sample of events."
        : (timeline.lanes ? timeline.lanes.length : 0) + " task lanes";
    } catch (error) {
      toast("Timeline failed: " + error.message, true);
    }
    updateDiffOptions();
    await setIndex(0);
    drawTimeline();
  }

  async function loadWindow(center) {
    const limit = 300;
    const total = state.total;
    let from = clamp(center - Math.floor(limit / 2), 0, Math.max(0, total - 1));
    from = Math.max(0, Math.min(from, total - limit));
    const data = await api("/api/records/" + state.current + "/events?from=" + from + "&limit=" + limit);
    state.events = data.events || [];
    state.windowFrom = data.from || 0;
    state.total = data.total;
    renderEvents();
  }

  async function setIndex(index, options = {}) {
    if (!state.record) return;
    const total = state.total || state.record.events;
    if (total <= 0) return;
    const next = clamp(Number.isFinite(index) ? index : 0, 0, total - 1);
    state.index = next;
    const scrub = $("scrub");
    scrub.max = String(total - 1);
    scrub.value = String(next);
    $("index-label").textContent = (next + 1) + " / " + total;
    if (!state.events.length || next < state.windowFrom || next >= state.windowFrom + state.events.length) {
      await loadWindow(next);
    } else {
      renderEvents();
    }
    const seq = ++state.fetchSeq;
    try {
      const frame = await api("/api/records/" + state.current + "/frame?index=" + next);
      if (seq !== state.fetchSeq) return;
      state.frame = frame;
      renderInspector();
      drawTimeline();
    } catch (error) {
      if (seq === state.fetchSeq) toast("Frame failed: " + error.message, true);
    }
  }

  async function applyReplayResult(data, runs) {
    state.lastReplay = data;
    renderReplay(data);
    if (data.divergence && data.divergence.found) {
      state.divergence = data.divergence;
      renderDivergence(data.divergence);
      await setIndex(data.divergence.index);
      toast("Divergence at event " + data.divergence.index + ": " + data.divergence.reason);
    } else {
      state.divergence = null;
      renderDivergence(null);
      toast(data.allIdentical
        ? (runs > 1 ? runs + " replays are bit-identical." : "Replay is bit-identical.")
        : "Replay completed with a mismatch.", !data.allIdentical);
    }
    setStatus("live", "live");
  }

  async function runReplay(runs, mutateStep, mutateChoice) {
    if (!state.current) { toast("Record or select a run first.", true); return; }
    setStatus("replaying", "busy");
    try {
      const body = { recordId: state.current, runs };
      if (mutateStep !== null && mutateStep !== undefined) {
        body.mutateStep = mutateStep;
        body.mutateChoice = mutateChoice || 0;
      }
      if (runs > 1) {
        body.async = true;
        const queued = await api("/api/replay", { method: "POST", body: JSON.stringify(body) });
        if (!queued.jobId) throw new Error("server did not return a job id");
        for (let attempt = 0; attempt < 600; attempt += 1) {
          await new Promise((resolve) => setTimeout(resolve, 200));
          const job = await api("/api/jobs/" + queued.jobId);
          if (job.status === "done") {
            await applyReplayResult(job.result, runs);
            return;
          }
          if (job.status === "error") {
            throw new Error(job.error || "background replay failed");
          }
        }
        throw new Error("background replay timed out");
      }
      const data = await api("/api/replay", { method: "POST", body: JSON.stringify(body) });
      await applyReplayResult(data, runs);
    } catch (error) {
      setStatus("error", "error");
      toast("Replay failed: " + error.message, true);
    }
  }

  function updateDiffOptions() {
    const select = $("diff-other");
    const previous = select.value;
    select.replaceChildren();
    for (const record of state.records) {
      if (record.id === state.current) continue;
      const option = el("option", "", "#" + record.id + " " + record.scenario + " (" + record.events + ")");
      option.value = String(record.id);
      select.appendChild(option);
    }
    if (previous) select.value = previous;
  }

  async function loadDiff() {
    const other = Number($("diff-other").value);
    if (!state.current || !other) { toast("Select two records to compare.", true); return; }
    try {
      const data = await api("/api/records/" + state.current + "/diff?other=" + other);
      state.diff = data;
      renderDiff(data);
    } catch (error) {
      toast("Diff failed: " + error.message, true);
    }
  }

  async function addBreakpoint(formEvent) {
    formEvent.preventDefault();
    if (!state.current) { toast("Record a run first.", true); return; }
    const type = $("bp-type").value;
    const taskText = $("bp-task").value.trim();
    const text = $("bp-text").value.trim();
    const body = {};
    if (type) body.type = type;
    if (taskText) body.task = Number(taskText);
    if (text) body.text = text;
    if (!type && !taskText && !text) { toast("Add at least one breakpoint criterion.", true); return; }
    try {
      await api("/api/breakpoints", { method: "POST", body: JSON.stringify(body) });
      const data = await api("/api/breakpoints");
      state.breakpoints = data.breakpoints || [];
      renderBreakpoints();
      $("bp-text").value = "";
      $("bp-task").value = "";
    } catch (error) {
      toast("Breakpoint failed: " + error.message, true);
    }
  }

  async function toggleBreakpoint(breakpoint) {
    try {
      await api("/api/breakpoints/toggle", { method: "POST", body: JSON.stringify({ id: breakpoint.id, enabled: !breakpoint.enabled }) });
      const data = await api("/api/breakpoints");
      state.breakpoints = data.breakpoints || [];
      renderBreakpoints();
    } catch (error) {
      toast("Breakpoint failed: " + error.message, true);
    }
  }

  async function removeBreakpoint(id) {
    try {
      await api("/api/breakpoints/remove", { method: "POST", body: JSON.stringify({ id }) });
      const data = await api("/api/breakpoints");
      state.breakpoints = data.breakpoints || [];
      renderBreakpoints();
    } catch (error) {
      toast("Breakpoint failed: " + error.message, true);
    }
  }

  async function clearBreakpoints() {
    try {
      await api("/api/breakpoints/clear", { method: "POST", body: "{}" });
      const data = await api("/api/breakpoints");
      state.breakpoints = data.breakpoints || [];
      renderBreakpoints();
    } catch (error) {
      toast("Clear breakpoints failed: " + error.message, true);
    }
  }

  async function importRecord(event) {
    const file = event.target.files && event.target.files[0];
    event.target.value = "";
    if (!file) return;
    if (file.size > 64 * 1024 * 1024) {
      toast("Record import is limited to 64 MiB over HTTP.", true);
      return;
    }
    setStatus("importing", "busy");
    try {
      const bytes = await file.arrayBuffer();
      const response = await fetch(withToken("/api/records/import"), {
        method: "POST",
        headers: { "Content-Type": "application/octet-stream" },
        body: bytes
      });
      const data = await response.json();
      if (!response.ok) throw new Error(data.error || ("HTTP " + response.status));
      toast(data.duplicate ? "Record already present." : "Imported " + file.name + ".");
      await loadOverview(data.record ? data.record.id : null);
      setStatus("live", "live");
    } catch (error) {
      setStatus("error", "error");
      toast("Import failed: " + error.message, true);
    }
  }

  async function deleteCurrentRecord() {
    if (!state.current) { toast("No record selected.", true); return; }
    if (!confirm("Delete record #" + state.current + " and its .tmpo file?")) return;
    try {
      await api("/api/records/" + state.current + "/delete", {
        method: "POST",
        body: JSON.stringify({ deleteFile: true })
      });
      state.current = null;
      state.record = null;
      state.events = [];
      state.total = 0;
      state.frame = null;
      state.divergence = null;
      renderEvents();
      renderInspector();
      await loadOverview(null);
      toast("Record deleted.");
    } catch (error) {
      toast("Delete failed: " + error.message, true);
    }
  }

  async function runToBreakpoint(direction) {
    if (!state.current) return;
    try {
      const data = await api("/api/records/" + state.current + "/breakpoints/next?from=" + state.index + "&direction=" + direction);
      if (data.index === null || data.index === undefined) {
        toast("No breakpoint " + (direction > 0 ? "ahead" : "behind") + ".");
      } else {
        await setIndex(data.index);
      }
    } catch (error) {
      toast("Breakpoint run failed: " + error.message, true);
    }
  }

  function bindEvents() {
    $("scenario").addEventListener("change", updateScenarioNote);
    $("record-btn").addEventListener("click", () => {
      const scenario = $("scenario").value;
      const seedText = $("seed").value.trim();
      const seed = seedText === "" ? "7" : seedText;
      const mutateText = $("record-mutate-step").value.trim();
      const mutateStep = mutateText === "" ? null : Number(mutateText);
      const mutateChoice = Number($("record-mutate-choice").value) || 0;
      recordRun(scenario, seed, mutateStep, mutateChoice)
        .catch(() => {});
    });
    $("first").addEventListener("click", () => setIndex(0));
    $("last").addEventListener("click", () => setIndex(state.total - 1));
    $("prev").addEventListener("click", () => setIndex(state.index - 1));
    $("next").addEventListener("click", () => setIndex(state.index + 1));
    $("scrub").addEventListener("input", (event) => setIndex(Number(event.target.value)));
    $("run-bp").addEventListener("click", () => runToBreakpoint(1));
    $("run-bp-back").addEventListener("click", () => runToBreakpoint(-1));
    $("replay").addEventListener("click", () => runReplay(1));
    $("replay100").addEventListener("click", () => runReplay(100));
    $("mutate-btn").addEventListener("click", () => {
      const step = Number($("mutate-step").value);
      const choice = Number($("mutate-choice").value) || 0;
      runReplay(1, step, choice);
    });
    $("bp-form").addEventListener("submit", addBreakpoint);
    $("bp-clear").addEventListener("click", clearBreakpoints);
    $("import-btn").addEventListener("click", () => $("import-file").click());
    $("import-file").addEventListener("change", importRecord);
    $("delete-record").addEventListener("click", deleteCurrentRecord);
    $("diff-btn").addEventListener("click", loadDiff);
    $("timeline").addEventListener("click", (event) => {
      if (!state.record || !state.total) return;
      const rect = event.currentTarget.getBoundingClientRect();
      const x = event.clientX - rect.left;
      const labelWidth = 128;
      const padRight = 16;
      const plotWidth = Math.max(40, rect.width - labelWidth - padRight);
      const ratio = clamp((x - labelWidth) / plotWidth, 0, 1);
      setIndex(Math.round(ratio * (state.total - 1)));
    });
    window.addEventListener("resize", drawTimeline);
    document.addEventListener("keydown", (event) => {
      const tag = (event.target.tagName || "").toLowerCase();
      if (tag === "input" || tag === "select" || tag === "textarea") return;
      if (event.key === "ArrowLeft") { event.preventDefault(); setIndex(state.index - 1); }
      if (event.key === "ArrowRight") { event.preventDefault(); setIndex(state.index + 1); }
      if (event.key === "Home") { event.preventDefault(); setIndex(0); }
      if (event.key === "End") { event.preventDefault(); setIndex(state.total - 1); }
    });
  }

  function connectStream() {
    const source = new EventSource(withToken("/api/stream"));
    source.addEventListener("open", () => setStatus("live", "live"));
    source.addEventListener("error", () => setStatus("reconnecting", "busy"));
    source.addEventListener("overview", () => {
      api("/api/overview").then((data) => {
        state.records = data.records || [];
        state.breakpoints = data.breakpoints || [];
        renderRecords();
        renderBreakpoints();
        updateDiffOptions();
      }).catch(() => {});
    });
    source.addEventListener("replay", (event) => {
      try {
        const payload = JSON.parse(event.data);
        if (payload.recordId === state.current) {
          state.lastReplay = payload;
          renderReplay(payload);
        }
      } catch { /* ignore malformed SSE payloads */ }
    });
  }

  function init() {
    $("delete-record").disabled = true;
    const favicon = $("favicon");
    if (favicon) favicon.href = withToken("/favicon.svg");
    const typeSelect = $("bp-type");
    typeSelect.appendChild(el("option", "", "any type"));
    for (const name of EVENT_TYPES) {
      const option = el("option", "", name);
      option.value = name;
      typeSelect.appendChild(option);
    }
    bindEvents();
    setStatus("connecting", "busy");
    connectStream();
    loadOverview().then(() => {
      if (state.records.length) setStatus("live", "live");
    }).catch((error) => {
      setStatus("error", "error");
      toast("Could not load Tempo: " + error.message, true);
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
