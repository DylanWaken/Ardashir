"use strict";

const state = {
  session: null,
  filename: "",
  names: new Map(),
  threads: new Map(),
  viewStart: 0,
  viewEnd: 1,
  threadFilter: "",
  nameFilter: "",
  hitRegions: [],
  dragging: false,
  dragX: 0,
  dragStart: 0,
  dragEnd: 1,
};

const elements = {
  form: document.querySelector("#upload-form"),
  file: document.querySelector("#trace-file"),
  empty: document.querySelector("#empty-state"),
  workspace: document.querySelector("#workspace"),
  error: document.querySelector("#error-banner"),
  filename: document.querySelector("#filename"),
  summary: document.querySelector("#summary"),
  threadFilter: document.querySelector("#thread-filter"),
  nameFilter: document.querySelector("#name-filter"),
  reset: document.querySelector("#reset-view"),
  timeWindow: document.querySelector("#time-window"),
  timeline: document.querySelector("#timeline"),
  timelineWrap: document.querySelector("#timeline-wrap"),
  counters: document.querySelector("#counters"),
  counterEmpty: document.querySelector("#counter-empty"),
  aggregates: document.querySelector("#aggregate-body"),
  tooltip: document.querySelector("#tooltip"),
};

function formatDuration(ns) {
  const value = Math.abs(ns);
  if (value >= 1e9) return `${(ns / 1e9).toFixed(3)} s`;
  if (value >= 1e6) return `${(ns / 1e6).toFixed(3)} ms`;
  if (value >= 1e3) return `${(ns / 1e3).toFixed(3)} µs`;
  return `${ns.toFixed(0)} ns`;
}

function formatNumber(value) {
  return new Intl.NumberFormat(undefined, { maximumFractionDigits: 3 }).format(value);
}

function colorFor(id, alpha = 1) {
  const hue = (Number(id) * 47 + 198) % 360;
  return `hsla(${hue}, 48%, 57%, ${alpha})`;
}

function setError(message) {
  elements.error.textContent = message || "";
  elements.error.hidden = !message;
}

function setCanvasSize(canvas, cssHeight) {
  const ratio = window.devicePixelRatio || 1;
  const width = Math.max(1, canvas.clientWidth);
  canvas.style.height = `${cssHeight}px`;
  canvas.width = Math.round(width * ratio);
  canvas.height = Math.round(cssHeight * ratio);
  const context = canvas.getContext("2d");
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  return { context, width, height: cssHeight };
}

function loadSession(payload) {
  state.session = payload.session;
  state.filename = payload.filename || "Capture";
  state.names = new Map(state.session.names.map((item) => [item.id, item.label]));
  state.threads = new Map(state.session.threads.map((item) => [item.id, item.label]));
  state.viewStart = state.session.start_ns;
  state.viewEnd = Math.max(state.session.end_ns, state.session.start_ns + 1);
  state.threadFilter = "";
  state.nameFilter = "";

  elements.filename.textContent = state.filename;
  elements.threadFilter.replaceChildren(new Option("All threads", ""));
  for (const thread of state.session.threads) {
    elements.threadFilter.append(new Option(thread.label, String(thread.id)));
  }
  elements.threadFilter.value = "";
  elements.nameFilter.value = "";
  elements.empty.hidden = true;
  elements.workspace.hidden = false;
  setError("");
  renderSummary();
  renderAll();
}

function renderSummary() {
  const summary = state.session.summary;
  const stats = [
    ["Duration", formatDuration(state.session.duration_ns)],
    ["Threads", summary.thread_count],
    ["Scopes", summary.scope_count],
    ["Counters", summary.counter_count],
    ["Markers", summary.marker_count],
    ["Names", summary.name_count],
  ];
  elements.summary.replaceChildren();
  for (const [label, value] of stats) {
    const card = document.createElement("div");
    card.className = "stat";
    const labelNode = document.createElement("div");
    labelNode.className = "stat-label";
    labelNode.textContent = label;
    const valueNode = document.createElement("div");
    valueNode.className = "stat-value";
    valueNode.textContent = String(value);
    card.append(labelNode, valueNode);
    elements.summary.append(card);
  }
}

function nameMatches(nameId) {
  return !state.nameFilter ||
    (state.names.get(nameId) || "").toLocaleLowerCase().includes(state.nameFilter);
}

function threadMatches(threadId) {
  return !state.threadFilter || String(threadId) === state.threadFilter;
}

function visibleThreads() {
  return state.session.threads.filter((thread) => threadMatches(thread.id));
}

function scopeDepths() {
  const byId = new Map(state.session.scopes.map((scope) => [scope.scope_id, scope]));
  const memo = new Map();
  function depth(scope) {
    if (memo.has(scope.scope_id)) return memo.get(scope.scope_id);
    const parent = byId.get(scope.parent_scope_id);
    const result = parent ? depth(parent) + 1 : 0;
    memo.set(scope.scope_id, result);
    return result;
  }
  for (const scope of state.session.scopes) depth(scope);
  return memo;
}

function xForTime(timestamp, left, width) {
  return left + ((timestamp - state.viewStart) / (state.viewEnd - state.viewStart)) * width;
}

function drawTimeGrid(context, left, width, height) {
  context.strokeStyle = "#262d34";
  context.fillStyle = "#7f8994";
  context.font = "10px ui-monospace, Consolas, monospace";
  context.textAlign = "center";
  for (let index = 0; index <= 8; index += 1) {
    const x = left + (width * index) / 8;
    const timestamp = state.viewStart + ((state.viewEnd - state.viewStart) * index) / 8;
    context.beginPath();
    context.moveTo(x, 22);
    context.lineTo(x, height);
    context.stroke();
    context.fillText(formatDuration(timestamp - state.session.start_ns), x, 14);
  }
}

function renderTimeline() {
  const threads = visibleThreads();
  const depths = scopeDepths();
  const scopes = state.session.scopes.filter(
    (scope) => threadMatches(scope.thread_id) && nameMatches(scope.name_id)
  );
  const maxDepth = new Map(threads.map((thread) => [thread.id, 0]));
  for (const scope of scopes) {
    maxDepth.set(scope.thread_id, Math.max(maxDepth.get(scope.thread_id) || 0, depths.get(scope.scope_id)));
  }

  const rowHeight = 24;
  const headerHeight = 28;
  const laneHeights = threads.map((thread) => Math.max(48, (maxDepth.get(thread.id) + 1) * rowHeight + 16));
  const totalHeight = Math.max(240, headerHeight + laneHeights.reduce((sum, height) => sum + height, 0));
  const { context, width, height } = setCanvasSize(elements.timeline, totalHeight);
  const left = Math.min(150, Math.max(90, width * 0.18));
  const plotWidth = Math.max(1, width - left - 12);
  state.hitRegions = [];
  context.clearRect(0, 0, width, height);
  context.fillStyle = "#101317";
  context.fillRect(0, 0, width, height);
  drawTimeGrid(context, left, plotWidth, height);

  let laneTop = headerHeight;
  for (let threadIndex = 0; threadIndex < threads.length; threadIndex += 1) {
    const thread = threads[threadIndex];
    const laneHeight = laneHeights[threadIndex];
    context.fillStyle = threadIndex % 2 ? "#12161a" : "#15191e";
    context.fillRect(0, laneTop, width, laneHeight);
    context.strokeStyle = "#2b3239";
    context.beginPath();
    context.moveTo(0, laneTop);
    context.lineTo(width, laneTop);
    context.stroke();
    context.fillStyle = "#bac1c8";
    context.font = "11px system-ui, sans-serif";
    context.textAlign = "left";
    context.fillText(thread.label, 10, laneTop + 18);

    for (const scope of scopes.filter((item) => item.thread_id === thread.id)) {
      if (scope.end_ns < state.viewStart || scope.start_ns > state.viewEnd) continue;
      const x1 = Math.max(left, xForTime(scope.start_ns, left, plotWidth));
      const x2 = Math.min(left + plotWidth, xForTime(scope.end_ns, left, plotWidth));
      const y = laneTop + 7 + depths.get(scope.scope_id) * rowHeight;
      const rectWidth = Math.max(1, x2 - x1);
      context.fillStyle = colorFor(scope.name_id, 0.82);
      context.fillRect(x1, y, rectWidth, 18);
      context.strokeStyle = colorFor(scope.name_id);
      context.strokeRect(x1 + .5, y + .5, Math.max(0, rectWidth - 1), 17);
      if (rectWidth > 42) {
        context.save();
        context.beginPath();
        context.rect(x1 + 3, y, rectWidth - 6, 18);
        context.clip();
        context.fillStyle = "#0b0d10";
        context.font = "10px system-ui, sans-serif";
        context.fillText(state.names.get(scope.name_id), x1 + 5, y + 13);
        context.restore();
      }
      state.hitRegions.push({ x1, x2, y1: y, y2: y + 18, scope, thread });
    }
    laneTop += laneHeight;
  }

  const markers = state.session.markers.filter(
    (marker) => threadMatches(marker.thread_id) && nameMatches(marker.name_id)
  );
  for (const marker of markers) {
    if (marker.timestamp_ns < state.viewStart || marker.timestamp_ns > state.viewEnd) continue;
    const x = xForTime(marker.timestamp_ns, left, plotWidth);
    context.strokeStyle = colorFor(marker.name_id);
    context.setLineDash([4, 3]);
    context.beginPath();
    context.moveTo(x, headerHeight);
    context.lineTo(x, height);
    context.stroke();
    context.setLineDash([]);
    context.fillStyle = colorFor(marker.name_id);
    context.beginPath();
    context.moveTo(x - 4, headerHeight);
    context.lineTo(x + 4, headerHeight);
    context.lineTo(x, headerHeight + 6);
    context.fill();
  }

  elements.timeWindow.textContent =
    `${formatDuration(state.viewStart - state.session.start_ns)} — ` +
    `${formatDuration(state.viewEnd - state.session.start_ns)}`;
}

function renderCounters() {
  const counters = state.session.counters.filter(
    (counter) => threadMatches(counter.thread_id) && nameMatches(counter.name_id)
  );
  const series = new Map();
  for (const counter of counters) {
    const key = `${counter.thread_id}:${counter.name_id}`;
    if (!series.has(key)) series.set(key, []);
    series.get(key).push(counter);
  }
  const rows = [...series.values()];
  const cssHeight = Math.max(220, rows.length * 78 + 30);
  const { context, width, height } = setCanvasSize(elements.counters, cssHeight);
  elements.counterEmpty.hidden = rows.length !== 0;
  const left = Math.min(150, Math.max(90, width * 0.2));
  const plotWidth = Math.max(1, width - left - 12);
  context.clearRect(0, 0, width, height);
  context.fillStyle = "#101317";
  context.fillRect(0, 0, width, height);
  drawTimeGrid(context, left, plotWidth, height);

  rows.forEach((points, index) => {
    points.sort((a, b) => a.timestamp_ns - b.timestamp_ns);
    const top = 28 + index * 78;
    const rowHeight = 64;
    let min = Infinity;
    let max = -Infinity;
    for (const point of points) {
      min = Math.min(min, point.value);
      max = Math.max(max, point.value);
    }
    if (min === max) { min -= 1; max += 1; }
    context.strokeStyle = "#293038";
    context.strokeRect(left, top, plotWidth, rowHeight);
    context.fillStyle = "#b5bdc6";
    context.font = "10px system-ui, sans-serif";
    context.textAlign = "left";
    const first = points[0];
    context.fillText(state.names.get(first.name_id), 10, top + 16);
    context.fillStyle = "#77828d";
    context.fillText(state.threads.get(first.thread_id), 10, top + 31);
    context.fillText(`${formatNumber(min)} – ${formatNumber(max)}`, 10, top + 47);

    context.strokeStyle = colorFor(first.name_id);
    context.lineWidth = 1.5;
    context.beginPath();
    let started = false;
    for (const point of points) {
      if (point.timestamp_ns < state.viewStart || point.timestamp_ns > state.viewEnd) continue;
      const x = xForTime(point.timestamp_ns, left, plotWidth);
      const y = top + rowHeight - ((point.value - min) / (max - min)) * rowHeight;
      if (!started) { context.moveTo(x, y); started = true; } else context.lineTo(x, y);
    }
    context.stroke();
    context.lineWidth = 1;
  });
}

function renderAggregates() {
  const groups = new Map();
  for (const scope of state.session.scopes) {
    if (!threadMatches(scope.thread_id) || !nameMatches(scope.name_id)) continue;
    const duration = scope.end_ns - scope.start_ns;
    if (!groups.has(scope.name_id)) {
      groups.set(scope.name_id, {
        name: state.names.get(scope.name_id),
        count: 0,
        total: 0,
        min: Infinity,
        max: -Infinity,
      });
    }
    const group = groups.get(scope.name_id);
    group.count += 1;
    group.total += duration;
    group.min = Math.min(group.min, duration);
    group.max = Math.max(group.max, duration);
  }
  const rows = [...groups.values()].map((group) => ({
    ...group,
    average: group.total / group.count,
  })).sort((a, b) => b.total - a.total);

  elements.aggregates.replaceChildren();
  for (const row of rows) {
    const tr = document.createElement("tr");
    const values = [row.name, row.count, formatDuration(row.total), formatDuration(row.average),
      formatDuration(row.min), formatDuration(row.max)];
    for (const value of values) {
      const td = document.createElement("td");
      td.textContent = String(value);
      tr.append(td);
    }
    elements.aggregates.append(tr);
  }
}

function renderAll() {
  if (!state.session) return;
  renderTimeline();
  renderCounters();
  renderAggregates();
}

async function requestSession(url, options) {
  const response = await fetch(url, options);
  const payload = await response.json().catch(() => ({ error: "The server returned an invalid response" }));
  if (!response.ok) throw new Error(payload.error || `Request failed (${response.status})`);
  loadSession(payload);
}

elements.file.addEventListener("change", async () => {
  const file = elements.file.files[0];
  if (!file) return;
  const data = new FormData();
  data.append("trace", file);
  setError("");
  try {
    await requestSession("/api/session", { method: "POST", body: data });
  } catch (error) {
    setError(error.message);
  } finally {
    elements.form.reset();
  }
});

elements.threadFilter.addEventListener("change", () => {
  state.threadFilter = elements.threadFilter.value;
  renderAll();
});

elements.nameFilter.addEventListener("input", () => {
  state.nameFilter = elements.nameFilter.value.trim().toLocaleLowerCase();
  renderAll();
});

elements.reset.addEventListener("click", () => {
  state.viewStart = state.session.start_ns;
  state.viewEnd = Math.max(state.session.end_ns, state.session.start_ns + 1);
  renderAll();
});

elements.timeline.addEventListener("wheel", (event) => {
  event.preventDefault();
  const rect = elements.timeline.getBoundingClientRect();
  const left = Math.min(150, Math.max(90, rect.width * 0.18));
  const fraction = Math.max(0, Math.min(1, (event.clientX - rect.left - left) / Math.max(1, rect.width - left - 12)));
  const range = state.viewEnd - state.viewStart;
  const anchor = state.viewStart + range * fraction;
  const factor = event.deltaY > 0 ? 1.25 : 0.8;
  const fullRange = Math.max(1, state.session.end_ns - state.session.start_ns);
  const nextRange = Math.max(1, Math.min(fullRange, range * factor));
  state.viewStart = anchor - nextRange * fraction;
  state.viewEnd = state.viewStart + nextRange;
  if (state.viewStart < state.session.start_ns) {
    state.viewStart = state.session.start_ns;
    state.viewEnd = state.viewStart + nextRange;
  }
  if (state.viewEnd > state.session.end_ns) {
    state.viewEnd = state.session.end_ns;
    state.viewStart = state.viewEnd - nextRange;
  }
  renderAll();
}, { passive: false });

elements.timeline.addEventListener("pointerdown", (event) => {
  state.dragging = true;
  state.dragX = event.clientX;
  state.dragStart = state.viewStart;
  state.dragEnd = state.viewEnd;
  elements.timeline.setPointerCapture(event.pointerId);
  elements.timeline.style.cursor = "grabbing";
});

elements.timeline.addEventListener("pointermove", (event) => {
  if (state.dragging) {
    const range = state.dragEnd - state.dragStart;
    const delta = ((event.clientX - state.dragX) / elements.timeline.clientWidth) * range;
    let start = state.dragStart - delta;
    let end = state.dragEnd - delta;
    if (start < state.session.start_ns) { end += state.session.start_ns - start; start = state.session.start_ns; }
    if (end > state.session.end_ns) { start -= end - state.session.end_ns; end = state.session.end_ns; }
    state.viewStart = start;
    state.viewEnd = end;
    renderAll();
    return;
  }
  const rect = elements.timeline.getBoundingClientRect();
  const x = event.clientX - rect.left;
  const y = event.clientY - rect.top;
  const hit = state.hitRegions.find((region) => x >= region.x1 && x <= region.x2 && y >= region.y1 && y <= region.y2);
  if (!hit) {
    elements.tooltip.hidden = true;
    return;
  }
  const scope = hit.scope;
  elements.tooltip.textContent =
    `${state.names.get(scope.name_id)}\n${hit.thread.label}\n` +
    `Duration  ${formatDuration(scope.end_ns - scope.start_ns)}\n` +
    `Start     ${formatDuration(scope.start_ns - state.session.start_ns)}\n` +
    `Scope ID  ${scope.scope_id}`;
  elements.tooltip.style.left = `${Math.min(x + 12, rect.width - 220)}px`;
  elements.tooltip.style.top = `${Math.max(4, y - 86)}px`;
  elements.tooltip.hidden = false;
});

function finishDrag() {
  state.dragging = false;
  elements.timeline.style.cursor = "crosshair";
}
elements.timeline.addEventListener("pointerup", finishDrag);
elements.timeline.addEventListener("pointercancel", finishDrag);
elements.timeline.addEventListener("pointerleave", () => { if (!state.dragging) elements.tooltip.hidden = true; });

new ResizeObserver(() => renderAll()).observe(elements.timelineWrap);

requestSession("/api/session").catch((error) => {
  if (!error.message.includes("No trace is open")) setError(error.message);
});
