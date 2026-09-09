/* Small deterministic DAG layout; the diagram shows operation prerequisites, not call-stack edges. */
((scope) => {
  "use strict";
  function layout(flow) {
    const layers = new Map(), placed = new Map(), pending = new Set(flow.nodes.map(node => node.id));
    const incoming = id => flow.edges.filter(edge => edge.target === id).map(edge => edge.source);
    while (pending.size) {
      let progress = false;
      for (const id of pending) {
        const parents = incoming(id);
        if (!parents.every(parent => layers.has(parent))) continue;
        layers.set(id, parents.length ? Math.max(...parents.map(parent => layers.get(parent))) + 1 : 0);
        pending.delete(id); progress = true;
      }
      if (!progress) throw new Error("Operation flow contains an unresolved edge or cycle");
    }
    const columns = Math.max(...layers.values()) + 1;
    const rows = Math.max(...Array.from({length:columns}, (_, column) => [...layers.values()].filter(value => value === column).length));
    for (let column = 0; column < columns; column++) {
      const ids = flow.nodes.filter(node => layers.get(node.id) === column).map(node => node.id);
      ids.forEach((id, index) => placed.set(id, {x:20 + column*245, y:42 + (index + (rows-ids.length)/2)*130, width:190, height:78, column}));
    }
    return {placed, width:columns*245, height:rows*130+50};
  }
  function render(container, stageId, details, enabled, configuration) {
    const {escape:e, sourceMarkup} = scope.Atlas;
    const geometry = layout(details.flow);
    const calls = new Map(details.calls.map(call => [call.id, call]));
    const gateEnabled = node => enabled && (!node.gate || (node.gate === "hardware" || node.gate === "software" ? configuration.tracing === node.gate
      : node.gate === "vsm" || node.gate === "conventional" ? configuration.shadows === node.gate : Boolean(configuration[node.gate])));
    const marker = `flow-arrow-${stageId}`;
    const edges = details.flow.edges.map((edge,index) => {
      const a = geometry.placed.get(edge.source), b = geometry.placed.get(edge.target);
      const x1 = a.x+a.width, y1 = a.y+a.height/2, x2 = b.x, y2 = b.y+b.height/2;
      const bypass = b.column > a.column+1;
      const top = 12 + index%3*7;
      const path = bypass ? `M${x1} ${y1} L${x1+13} ${y1} L${x1+13} ${top} L${x2-15} ${top} L${x2-15} ${y2} L${x2} ${y2}`
        : `M${x1} ${y1} C${x1+28} ${y1},${x2-28} ${y2},${x2} ${y2}`;
      const labelY = bypass ? top-3 : (y1+y2)/2-8;
      return `<path d="${path}" marker-end="url(#${marker})"/><text x="${(x1+x2)/2}" y="${labelY}" text-anchor="middle">${e(edge.label)}</text>`;
    }).join("");
    container.innerHTML = `<p class="flow-intro">${e(details.overview)}</p><div class="flow-reading-note"><strong>Key operation flow.</strong> Arrows show selected control/data prerequisites. This is not a complete C++ call graph or a GPU duration chart. Select an operation to inspect its work and related functions. Scroll horizontally if needed.</div>
      <div class="operation-flow-scroll" tabindex="0" role="region" aria-label="Key operations for ${e(stageId)}"><div class="operation-flow" style="width:${geometry.width}px;height:${geometry.height}px"><svg aria-hidden="true" width="${geometry.width}" height="${geometry.height}" viewBox="0 0 ${geometry.width} ${geometry.height}"><defs><marker id="${marker}" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0 0L8 4L0 8"/></marker></defs>${edges}</svg>${details.flow.nodes.map((node,index) => {
        const p=geometry.placed.get(node.id);
        return `<button type="button" class="operation-node${gateEnabled(node) ? "" : " inactive"}" data-flow-node="${node.id}" aria-pressed="false" style="left:${p.x}px;top:${p.y}px;width:${p.width}px;height:${p.height}px"><small>${String(index+1).padStart(2,"0")}${node.gate ? " · " + e(node.gate) : ""}</small><strong>${e(node.label)}</strong></button>`;
      }).join("")}</div></div><p class="section-note">Faded operations illustrate a path disabled by the current page configuration. Other source conditions are described in the operation text; the chart is not a runtime simulator.</p><section class="flow-operation-detail" aria-live="polite"></section>
      <details class="call-reference"><summary>All selected functions for this stage · ${details.calls.length}</summary><p class="section-note">Source-checked call sites and helper definitions. CPU functions can declare RDG work whose shaders execute later; shader types and RDG event labels are identified in their descriptions.</p>${details.calls.map(call => `<article class="function-entry"><h4><code>${e(call.name)}</code></h4><span class="badge neutral">${e(call.kind)}</span><p>${e(call.role)}</p>${sourceMarkup([call.source])}</article>`).join("")}</details>`;
    function select(id) {
      const node = details.flow.nodes.find(node => node.id === id);
      if (!node) return;
      container.querySelectorAll("[data-flow-node]").forEach(button => button.setAttribute("aria-pressed", String(button.dataset.flowNode === id)));
      container.querySelector(".flow-operation-detail").innerHTML = `<h3>${e(node.label)}</h3><p>${e(node.detail)}</p>${!gateEnabled(node) ? '<p class="fallback-note">This illustrated branch is inactive under the selected configuration. Its explanation remains available.</p>' : ""}<h4>Related functions</h4>${node.callIds.map(id => {
        const call = calls.get(id);
        return `<article class="function-entry"><h4><code>${e(call.name)}</code></h4><p>${e(call.role)}</p><p class="section-note">${e(call.kind)} · ${e(call.when)}</p>${sourceMarkup([call.source])}</article>`;
      }).join("")}`;
    }
    container.addEventListener("click", event => { const button=event.target.closest("[data-flow-node]"); if (button) select(button.dataset.flowNode); });
    select(details.flow.nodes[0].id);
  }
  if (typeof module !== "undefined" && module.exports) module.exports={layout};
  else scope.UnrealOperationFlow={layout, render};
})(typeof window === "undefined" ? globalThis : window);
