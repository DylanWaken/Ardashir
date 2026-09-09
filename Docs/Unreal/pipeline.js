(() => {
  "use strict";
  const {data, escape:e, list, sourceMarkup, colors} = window.Atlas;
  const stages = data.stages;
  const byId = new Map(stages.map(stage => [stage.id, stage]));
  const timeline = document.getElementById("timeline");
  const scroll = document.getElementById("timeline-scroll");
  const detail = document.getElementById("stage-detail");
  const stageList = document.getElementById("stage-list");
  const storageKey = `unreal-atlas-checklist-${data.meta.commit}`;
  let completed = new Set(), storageAvailable = true;
  try { const saved = JSON.parse(localStorage.getItem(storageKey) || "[]"); if (Array.isArray(saved)) completed = new Set(saved.filter(value => typeof value === "string")); }
  catch { storageAvailable = false; }
  let selected = byId.has(location.hash.slice(1)) ? location.hash.slice(1) : stages[0].id;
  let currentTab = "explain";
  const config = () => ({
    nanite: document.getElementById("feature-nanite").checked,
    lumen: document.getElementById("feature-lumen").checked,
    async: document.getElementById("feature-async").checked,
    tracing: document.getElementById("trace-mode").value,
    shadows: document.getElementById("shadow-mode").value
  });
  const enabled = stage => stage.requires.every(key => key === "vsm" ? config().shadows === "vsm" : config()[key]);
  const dependencies = stage => stage.id === "shadows" && config().shadows === "conventional" ? ["views", "nanite-visibility"] : stage.depends;
  const number = id => String(stages.findIndex(stage => stage.id === id) + 1).padStart(2, "0");
  const stageLink = id => `<button type="button" data-stage="${id}">${e(byId.get(id).title)}${enabled(byId.get(id)) ? "" : " · disabled"}</button>`;
  const operationKeys = new Set(stages.flatMap(stage => stage.operations.map((_, index) => `${stage.id}:${index}`)));
  completed = new Set([...completed].filter(key => operationKeys.has(key)));

  function saveProgress() {
    try { localStorage.setItem(storageKey, JSON.stringify([...completed])); }
    catch { storageAvailable = false; }
    updateProgress();
  }
  function updateProgress() {
    const text = `${completed.size} of ${operationKeys.size} operations checked. ${storageAvailable ? "Checklist saved in this browser for this source revision." : "Browser storage unavailable; checklist lasts for this page session."} These are personal planning marks, not verified implementation results.`;
    document.getElementById("checklist-status").textContent = text;
    detail.querySelectorAll("[data-progress]").forEach(element => { element.textContent = text; });
  }
  function configurationText() {
    const c = config();
    const parts = [c.nanite ? "Nanite visibility and material shading enabled alongside conventional meshes." : "Nanite stages disabled: conventional depth/base-pass geometry remains; a non-Nanite mesh representation is needed."];
    parts.push(c.lumen ? (c.tracing === "hardware" ? "Lumen unresolved rays use hardware triangle/AS tracing; cached hit lighting versus Hit Lighting remains a separate choice." : "Lumen unresolved rays use configured mesh distance fields/heightfields and global-distance-field coverage. Screen traces remain conditional in both modes.") : "Lumen GI and reflections are disabled in this explorer. Choose another indirect/reflection method or accept the missing contributions; direct lighting remains.");
    parts.push(c.shadows === "vsm" ? "VSM page requests depend on receiver depth; conventional early shadow scheduling does not apply." : "Conventional shadow maps replace VSM page allocation. Their optional early scheduling is a different path, not a relocation of VSM work.");
    parts.push(c.async && c.lumen ? "Dashed stages mark opportunities for async work. Actual overlap depends on input readiness and platform support." : "No async opportunity markers shown. This diagram switch illustrates scheduling and does not configure an Unreal runtime.");
    return parts.join(" ");
  }
  function selectedConfiguration(stage) {
    const c = config();
    if (!enabled(stage)) return "This stage is disabled by the selected feature switch. Its documentation remains visible to explain the omitted work. Consumers use the selected alternative path or omit that lighting contribution.";
    if (["trace-scene", "gi", "reflections"].includes(stage.id)) return c.tracing === "hardware"
      ? "Selected tracing alternative: hardware triangles / acceleration structures. Build/refit BLAS and TLAS before ray consumers. Surface-cache lookup and Hit Lighting are separate shading choices; the cache/sky completion phase is not a software GDF trace in this mode."
      : "Selected tracing alternative: software distance fields. Provide mesh SDF/heightfield data where supported and maintain global SDF coverage. Screen traces alone cannot supply arbitrary off-screen geometry; representation coverage differs from hardware triangles.";
    if (stage.id === "shadows") return c.shadows === "vsm"
      ? "Selected shadow alternative: VSM. Implement receiver-driven virtual-page requests, physical-page allocation/reuse and cache invalidation before drawing dirty shadow depth."
      : "Selected shadow alternative: conventional maps. Use per-light shadow views and depth allocation instead of VSM page requests. An earlier shadow pass can be enabled when dependencies permit; this does not make VSM early-compatible.";
    if (stage.id === "gbuffer" && !c.nanite) return "Nanite is disabled: use conventional mesh material draws to populate the deferred surface representation. Nanite visibility decoding and shading bins are omitted.";
    if (stage.id === "composite") return c.lumen
      ? (c.async ? "Async illustration: indirect work can overlap direct lights; the regular Lumen composite after RenderLights waits for the indirect outputs." : "Serial illustration: execute the required indirect work without async overlap; preserve the same material and lighting composition contracts.")
      : "Lumen is disabled: no Lumen indirect result is available here. Deferred sky/reflection composition still follows the separately chosen methods and settings.";
    return "This stage includes conditional subfeatures described below. The explorer illustrates major choices; it is not a complete Unreal console-variable simulator.";
  }
  function drawEdges() {
    const svg = document.getElementById("edges");
    const bounds = timeline.getBoundingClientRect();
    svg.setAttribute("viewBox", `0 0 ${timeline.offsetWidth} ${timeline.offsetHeight}`);
    let markup = '<defs><marker id="arrow" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="5" markerHeight="5" orient="auto-start-reverse"><path d="M0 0L8 4L0 8" fill="#91acbe"/></marker><marker id="active-arrow" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="5" markerHeight="5" orient="auto-start-reverse"><path d="M0 0L8 4L0 8" fill="#185bb8"/></marker></defs>';
    for (const stage of stages) for (const dependency of dependencies(stage)) {
      const start = document.getElementById(`card-${dependency}`).getBoundingClientRect();
      const end = document.getElementById(`card-${stage.id}`).getBoundingClientRect();
      const x1 = start.right - bounds.left, y1 = start.top + start.height / 2 - bounds.top;
      const x2 = end.left - bounds.left, y2 = end.top + end.height / 2 - bounds.top;
      const dx = Math.max(14, Math.min(55, (x2 - x1) / 2));
      const focused = selected === stage.id || selected === dependency;
      const inactive = !enabled(stage) || !enabled(byId.get(dependency));
      markup += `<path class="dependency${focused ? " focused" : ""}${inactive ? " inactive" : ""}" data-from="${dependency}" data-to="${stage.id}" d="M${x1} ${y1} C${x1 + dx} ${y1},${x2 - dx} ${y2},${x2} ${y2}" marker-end="url(#${focused ? "active-arrow" : "arrow"})"/>`;
    }
    svg.innerHTML = markup;
  }
  function renderTimeline() {
    timeline.querySelectorAll(":scope > :not(svg)").forEach(node => node.remove());
    for (let column = 0; column < 16; column++) {
      const label = document.createElement("span"); label.className = "column-label";
      label.style.gridColumn = column + 2; label.style.gridRow = 1; label.textContent = String(column + 1).padStart(2,"0"); timeline.append(label);
    }
    data.lanes.forEach((lane, index) => {
      const label = document.createElement("div"); label.className = "lane-label";
      label.style.gridRow = index + 2; label.style.gridColumn = 1; label.style.setProperty("--lane", colors[lane.id]); label.textContent = lane.label; timeline.append(label);
    });
    stages.forEach(stage => {
      const button = document.createElement("button"); button.type = "button"; button.id = `card-${stage.id}`;
      button.dataset.stage = stage.id;
      button.className = `stage-card${stage.id === selected ? " selected" : ""}${stage.asyncCapable && config().async && enabled(stage) ? " async" : ""}${enabled(stage) ? "" : " inactive"}`;
      button.style.gridColumn = stage.column + 2; button.style.gridRow = data.lanes.findIndex(lane => lane.id === stage.lane) + 2;
      button.style.setProperty("--lane", colors[stage.lane]);
      button.setAttribute("aria-pressed", String(stage.id === selected));
      button.setAttribute("aria-label", `${number(stage.id)}. ${stage.title}${enabled(stage) ? "" : ". Feature disabled; view explanation"}`);
      button.innerHTML = `<span class="stage-number">${number(stage.id)} / ${enabled(stage) ? "STAGE" : "DISABLED"}</span><strong>${e(stage.title)}</strong>`;
      timeline.append(button);
    });
    stageList.innerHTML = stages.map(stage => `<button type="button" data-stage="${stage.id}" class="${enabled(stage) ? "" : "inactive"}" aria-current="${stage.id === selected}"><span>${number(stage.id)}</span>${e(stage.title)}${enabled(stage) ? "" : " (off)"}</button>`).join("");
    document.getElementById("configuration-note").textContent = configurationText();
    document.getElementById("trace-mode").disabled = !config().lumen;
    requestAnimationFrame(drawEdges);
  }
  function renderDetail() {
    const stage = byId.get(selected);
    const index = stages.indexOf(stage);
    const types = stage.typeIds.map(id => data.types.find(node => node.id === id));
    detail.innerHTML = `<div class="stage-title-row"><div><div class="badges"><span class="badge">${e(data.lanes.find(lane => lane.id === stage.lane).label)}</span><span class="badge neutral">${number(stage.id)} / ${stages.length}</span>${stage.asyncCapable ? '<span class="badge neutral">Conditional async opportunity</span>' : ""}</div><h2 id="detail-title">${e(stage.title)}</h2></div><div class="toolbar"><button type="button" data-stage="${stages[Math.max(0,index-1)].id}" ${index === 0 ? "disabled" : ""} aria-label="Previous stage">← Previous</button><button type="button" data-stage="${stages[Math.min(stages.length-1,index+1)].id}" ${index === stages.length-1 ? "disabled" : ""} aria-label="Next stage">Next →</button></div></div>
      <p class="detail-lede">${e(stage.summary)}</p><div class="fallback-note">${e(selectedConfiguration(stage))}</div>
      <div class="stage-tabs" role="tablist" aria-label="Stage details">${[["explain","How it works"],["operations","Operations to implement"],["verify","Correctness & source"]].map(([id,title]) => `<button type="button" role="tab" id="tab-${id}" aria-controls="panel-${id}" data-tab="${id}" aria-selected="${currentTab === id}" tabindex="${currentTab === id ? 0 : -1}">${title}</button>`).join("")}</div>
      <section class="tab-panel" role="tabpanel" tabindex="0" id="panel-explain" aria-labelledby="tab-explain" ${currentTab === "explain" ? "" : "hidden"}><div class="io-grid"><div><h3>Inputs</h3>${list(stage.inputs)}</div><div><h3>Outputs</h3>${list(stage.outputs)}</div></div><h3>The work inside this stage</h3>${list(stage.steps, true, "method-steps")}
      ${stage.caveat ? `<div class="notice"><strong>Configuration & ordering.</strong> ${e(stage.caveat)}</div>` : ""}<h4>Major prerequisites</h4><div class="relation-list">${dependencies(stage).length ? dependencies(stage).map(stageLink).join("") : '<span class="section-note">Queued frame inputs and persistent scene state</span>'}</div><h4>Types involved</h4><div class="relation-list">${types.map(node => `<a href="scene-types.html#${node.id}">${e(node.qualified)} ↗</a>`).join("")}</div></section>
      <section class="tab-panel" role="tabpanel" tabindex="0" id="panel-operations" aria-labelledby="tab-operations" ${currentTab === "operations" ? "" : "hidden"}><h3>Implementation operation checklist</h3><p class="section-note">An inferred breakdown of the required responsibilities. Check items to plan your own implementation; this does not indicate existing Ardashir support.</p><ul class="checklist">${stage.operations.map((operation,index) => { const key=`${stage.id}:${index}`; return `<li><label><input type="checkbox" data-operation="${key}" ${completed.has(key) ? "checked" : ""}><span>${e(operation)}</span></label></li>`; }).join("")}</ul><p class="checklist-status" data-progress></p><a href="#roadmap">See the phased implementation roadmap ↓</a></section>
      <section class="tab-panel" role="tabpanel" tabindex="0" id="panel-verify" aria-labelledby="tab-verify" ${currentTab === "verify" ? "" : "hidden"}><h3>Proposed correctness checks</h3><p class="section-note">Run these on a renderer you build. They are test criteria, not reports of an implemented Unreal-equivalent renderer.</p>${list(stage.validation)}${stage.caveat ? `<div class="notice">${e(stage.caveat)}</div>` : ""}${sourceMarkup(stage.sources)}</section>`;
    updateProgress();
  }
  function choose(id, moveTimeline = true, hash = true) {
    if (!byId.has(id)) return;
    selected = id;
    if (hash) history.replaceState(null, "", `#${id}`);
    renderTimeline(); renderDetail();
    if (moveTimeline) {
      const card = document.getElementById(`card-${id}`);
      const left = card.offsetLeft - (scroll.clientWidth - card.offsetWidth) / 2;
      scroll.scrollTo({left:Math.max(0,left), behavior:matchMedia("(prefers-reduced-motion: reduce)").matches ? "instant" : "smooth"});
    }
  }
  function selectTab(id, focus = false) {
    currentTab = id;
    detail.querySelectorAll("[data-tab]").forEach(button => {
      const active = button.dataset.tab === id;
      button.setAttribute("aria-selected", String(active)); button.tabIndex = active ? 0 : -1;
      if (active && focus) button.focus();
    });
    detail.querySelectorAll("[role=tabpanel]").forEach(panel => { panel.hidden = panel.id !== `panel-${id}`; });
  }
  document.addEventListener("click", event => {
    const stageButton = event.target.closest("[data-stage]");
    if (stageButton) { choose(stageButton.dataset.stage); return; }
    const tab = event.target.closest("[data-tab]"); if (tab) selectTab(tab.dataset.tab);
  });
  detail.addEventListener("keydown", event => {
    const tab = event.target.closest("[data-tab]");
    if (!tab || !["ArrowRight","ArrowLeft","Home","End"].includes(event.key)) return;
    const ids = ["explain","operations","verify"], index = ids.indexOf(tab.dataset.tab);
    const next = event.key === "Home" ? 0 : event.key === "End" ? 2 : (index + (event.key === "ArrowRight" ? 1 : 2)) % 3;
    event.preventDefault(); selectTab(ids[next], true);
  });
  detail.addEventListener("change", event => { if (event.target.matches("[data-operation]")) { const key=event.target.dataset.operation; if (event.target.checked) completed.add(key); else completed.delete(key); saveProgress(); } });
  document.querySelectorAll(".pipeline-controls input,.pipeline-controls select").forEach(element => element.addEventListener("change", () => { renderTimeline(); renderDetail(); }));
  document.getElementById("timeline-left").addEventListener("click", () => scroll.scrollBy({left:-Math.max(300,scroll.clientWidth*.7),behavior:"smooth"}));
  document.getElementById("timeline-right").addEventListener("click", () => scroll.scrollBy({left:Math.max(300,scroll.clientWidth*.7),behavior:"smooth"}));
  document.getElementById("reset-view").addEventListener("click", () => {
    document.getElementById("feature-nanite").checked=true; document.getElementById("feature-lumen").checked=true; document.getElementById("feature-async").checked=true;
    document.getElementById("trace-mode").value="hardware"; document.getElementById("shadow-mode").value="vsm";
    currentTab="explain"; choose(stages[0].id); scroll.scrollTo({left:0});
  });
  document.getElementById("clear-checklist").addEventListener("click", () => { completed.clear(); saveProgress(); renderDetail(); });
  document.getElementById("stage-stat").textContent=stages.length;
  document.getElementById("roadmap-items").innerHTML=data.roadmap.map(item => `<article class="roadmap-item"><h3>${e(item.title)}</h3><p>${e(item.work)}</p><p><strong>Correctness gate</strong><br>${e(item.gate)}</p></article>`).join("");
  window.addEventListener("hashchange", () => { if (byId.has(location.hash.slice(1))) choose(location.hash.slice(1),true,false); });
  new ResizeObserver(() => requestAnimationFrame(drawEdges)).observe(scroll);
  choose(selected, location.hash.length > 1, false);
})();
