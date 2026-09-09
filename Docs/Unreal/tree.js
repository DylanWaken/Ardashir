(() => {
  "use strict";
  const {data, escape: e, sourceMarkup} = window.Atlas;
  const catalog = window.UnrealCatalog;
  const all = new Map(catalog.types.map(node => [node.id, node]));
  const curated = new Map(data.types.map(node => [node.id, node]));
  const tree = document.getElementById("tree");
  const search = document.getElementById("type-search");
  const moduleFilter = document.getElementById("module-filter");
  const detail = document.getElementById("type-detail");
  const more = document.getElementById("more-types");
  let mode = "guided", limit = 150, selected = "", timer;
  const derived = new Map();
  for (const node of catalog.types) for (const base of node.baseLinks) for (const target of base.targets) {
    if (!derived.has(target)) derived.set(target, []);
    derived.get(target).push({node, ambiguous: base.resolution !== "unique"});
  }
  document.getElementById("inventory-stat").innerHTML = `<strong>${catalog.meta.declarations.toLocaleString()}</strong><span>indexed declarations<br>${data.types.length} explained core types</span>`;
  document.getElementById("scope-content").innerHTML = `<p><strong>Two views:</strong> explained types organize the core architecture by responsibility. Full source index exposes all ${catalog.meta.declarations.toLocaleString()} indexed declarations across ${catalog.meta.files.toLocaleString()} files, including platform RHIs and renderer helpers.</p><p><strong>Edges:</strong> group membership is organizational. Nested type branches follow a uniquely resolved C++ base in the same group/module. Multiple bases and ambiguous/conditional declarations remain visible in the inspector. They are not ownership arrows.</p><p>${e(catalog.meta.scope)}</p><p>Authored explanations cover the core types. Other entries show module context, inheritance and mechanically observed member hints; these hints are not a reviewed API contract. <a href="reference.md">Read the complete scope and explanations</a>.</p>`;
  moduleFilter.innerHTML += catalog.meta.modules.sort().map(module => `<option>${e(module)}</option>`).join("");

  function typeButton(node, ambiguous = false) {
    return `<button type="button" data-type="${e(node.id)}">${e(node.qualified)}${ambiguous ? " · candidate" : ""}</button>`;
  }
  function select(id, updateHash = true) {
    const baseNode = all.get(id);
    if (!baseNode) return;
    selected = id;
    const node = curated.get(id) || baseNode;
    if (updateHash) history.replaceState(null, "", `#${id}`);
    tree.querySelectorAll(".selected").forEach(item => item.classList.remove("selected"));
    tree.querySelectorAll(`[data-select="${id}"]`).forEach(item => item.classList.add("selected"));
    const relatedStages = data.stages.filter(stage => stage.typeIds.includes(id));
    const bases = node.baseLinks.map(base => (base.targets.length
      ? base.targets.map(target => typeButton(all.get(target), base.resolution !== "unique")).join("")
      : `<span class="unresolved">${e(base.name)} · outside index / unparsed</span>`) + (base.conditions?.length ? `<span class="badge neutral">${e(base.conditions.join("; "))} · conditional mixin</span>` : "")).join("");
    const children = derived.get(id) || [];
    const group = data.groups.find(group => group.id === node.group);
    const moduleContext = {
      RHI:"Portable GPU resource, command and device contracts", RHICore:"Shared low-level RHI implementation utilities", RenderCore:"Shader, render-resource and render-graph infrastructure", Renderer:"Scene rendering algorithms and their pass/resource state", Engine:"World/assets/components and their render-facing data", D3D12RHI:"Direct3D 12 backend objects and implementation", VulkanRHI:"Vulkan backend objects and implementation", MetalRHI:"Apple Metal backend objects and implementation", OpenGLDrv:"OpenGL backend implementation", NullDrv:"Null RHI implementation", Landscape:"Landscape geometry and scene integration"
    };
    detail.innerHTML = `<div class="badges"><span class="badge">${e(group?.title || node.module)}</span><span class="badge neutral">${e(node.kind)}</span><span class="badge neutral">${curated.has(id) ? "Authored explanation" : "Source inventory"}</span></div>
      <h2 id="detail-title">${e(node.qualified)}</h2>
      <p class="detail-lede">${e(node.role || `${node.qualified} is an indexed ${node.kind} in ${node.module}. Module context: ${moduleContext[node.module] || node.module}. Its declaration and structural relationships are shown below; a class-specific semantic review is not included for this inventory entry.`)}</p>
      ${node.implementation ? `<div class="operation-note"><h3>If implementing this responsibility</h3><p>${e(node.implementation)}</p></div>` : ""}
      <h4>Declared bases · C++ inheritance</h4><div class="relation-list">${bases || '<span class="unresolved">No base in the indexed declaration</span>'}</div>
      ${node.bases.length ? `<p class="section-note">Declaration: ${node.bases.map(base => `<code>${e(base)}</code>`).join(", ")}. Template arguments remain in this text; navigation targets the indexed base definition when resolvable.</p>` : ""}
      <h4>Indexed direct subclasses · ${children.length}</h4><div class="relation-list">${children.slice(0, 40).map(({node, ambiguous}) => typeButton(node, ambiguous)).join("") || '<span class="unresolved">No direct subclass found in scope</span>'}</div>
      ${children.length > 40 ? `<details><summary>Show ${children.length - 40} more subclasses</summary><div class="relation-list">${children.slice(40).map(({node, ambiguous}) => typeButton(node, ambiguous)).join("")}</div></details>` : ""}
      ${node.methods.length || node.fields.length ? `<details class="scope"><summary>Observed source surface · mechanical hints</summary><p>Names found lexically inside this definition, possibly including nested declarations. This is not an exhaustive member list or an API guarantee.</p>${node.methods.length ? `<p><strong>Callable-name hints:</strong> ${node.methods.map(name => `<code>${e(name)}</code>`).join(", ")}</p>` : ""}${node.fields.length ? `<p><strong>Member-name hints:</strong> ${node.fields.map(name => `<code>${e(name)}</code>`).join(", ")}</p>` : ""}</details>` : ""}
      ${relatedStages.length ? `<section class="type-stage-links"><h4>Follow this type through the frame</h4><div class="relation-list">${relatedStages.map(stage => `<a href="deferred-pipeline.html#${stage.id}">${e(stage.title)} ↗</a>`).join("")}</div></section>` : ""}
      ${sourceMarkup([node])}`;
  }
  function nodeElement(node, children, visited = new Set()) {
    const element = document.createElement("details");
    element.className = "type-node";
    const next = new Set(visited).add(node.id);
    const descendants = (children.get(node.id) || []).filter(child => !next.has(child.id));
    const explanation = curated.get(node.id)?.role;
    element.innerHTML = `<summary data-select="${e(node.id)}" class="${node.id === selected ? "selected" : ""}">${e(node.qualified)}${descendants.length ? `<span class="node-count">${descendants.length}</span>` : ""}</summary>`;
    let built = false;
    element.addEventListener("toggle", () => {
      if (!element.open || built) return;
      built = true;
      const note = document.createElement("div");
      note.className = "node-note";
      note.textContent = explanation || `${node.kind} · ${node.module} · ${node.path}:${node.line}. Select the name to inspect bases and source surface.`;
      element.append(note);
      if (descendants.length) {
        const branch = document.createElement("div"); branch.className = "branch";
        descendants.forEach(child => branch.append(nodeElement(child, children, next)));
        element.append(branch);
      }
    });
    return element;
  }
  function addGroup(group, nodes) {
    if (!nodes.length) return;
    const ids = new Set(nodes.map(node => node.id));
    const parents = new Map(), children = new Map();
    for (const node of nodes) {
      const base = node.baseLinks.find(base => base.resolution === "unique" && ids.has(base.targets[0]) && base.targets[0] !== node.id);
      if (base) parents.set(node.id, base.targets[0]);
    }
    // Break possible lexical cycles so every declaration remains reachable.
    for (const node of nodes) {
      const chain = new Set([node.id]); let cursor = parents.get(node.id);
      while (cursor) {
        if (chain.has(cursor)) { parents.delete(node.id); break; }
        chain.add(cursor); cursor = parents.get(cursor);
      }
    }
    for (const node of nodes) if (parents.has(node.id)) {
      const parent = parents.get(node.id);
      if (!children.has(parent)) children.set(parent, []);
      children.get(parent).push(node);
    }
    const element = document.createElement("details"); element.className = "group";
    element.innerHTML = `<summary><span class="group-number">${e(group.number || "◆")}</span>${e(group.title)}<span class="node-count">${nodes.length}</span></summary><p class="group-note">${e(group.summary || "Module group; expand types to follow uniquely resolved in-module inheritance.")}</p>`;
    const branch = document.createElement("div"); branch.className = "branch";
    nodes.filter(node => !parents.has(node.id)).forEach(node => branch.append(nodeElement(node, children)));
    element.append(branch); tree.append(element);
    element.open = mode === "guided" && (group.id === "scene" || moduleFilter.value !== "");
  }
  function render() {
    const query = search.value.trim().toLowerCase();
    const source = mode === "guided" ? data.types : catalog.types;
    const filtered = source.filter(node => (!moduleFilter.value || node.module === moduleFilter.value) && (!query || `${node.qualified} ${node.module} ${node.path} ${curated.get(node.id)?.role || ""} ${node.bases.join(" ")}`.toLowerCase().includes(query)));
    document.getElementById("tree-count").textContent = `${filtered.length.toLocaleString()} ${query ? "matches" : "types"}`;
    tree.replaceChildren(); more.hidden = true;
    if (!filtered.length) { tree.innerHTML = '<p class="empty">No types match. Try a shorter name or another module.</p>'; return; }
    if (query) {
      tree.innerHTML = filtered.slice(0, limit).map(node => `<button type="button" class="match ${node.id === selected ? "selected" : ""}" data-select="${node.id}"><strong>${e(node.qualified)}</strong><span>${e(node.module)} · ${e(node.kind)} · ${curated.has(node.id) ? "explained" : "indexed"}</span></button>`).join("");
      more.hidden = filtered.length <= limit;
      more.textContent = `Show more (${Math.min(limit, filtered.length)} of ${filtered.length})`;
    } else if (mode === "guided") {
      data.groups.forEach(group => addGroup(group, filtered.filter(node => node.group === group.id)));
    } else {
      catalog.meta.modules.forEach(module => addGroup({id:module,title:module}, filtered.filter(node => node.module === module).sort((a,b) => a.qualified.localeCompare(b.qualified))));
    }
  }
  function changeMode(value) {
    mode = value; limit = 150;
    document.getElementById("mode-guided").setAttribute("aria-pressed", String(mode === "guided"));
    document.getElementById("mode-catalog").setAttribute("aria-pressed", String(mode === "catalog"));
    render();
  }
  document.getElementById("mode-guided").addEventListener("click", () => changeMode("guided"));
  document.getElementById("mode-catalog").addEventListener("click", () => changeMode("catalog"));
  document.getElementById("expand-tree").addEventListener("click", () => tree.querySelectorAll(":scope > .group").forEach(node => { node.open = true; }));
  document.getElementById("collapse-tree").addEventListener("click", () => tree.querySelectorAll("details").forEach(node => { node.open = false; }));
  search.addEventListener("input", () => { clearTimeout(timer); timer = setTimeout(() => { limit = 150; render(); }, 90); });
  moduleFilter.addEventListener("change", () => { limit = 150; render(); });
  more.addEventListener("click", () => { limit += 150; render(); });
  tree.addEventListener("click", event => { const target = event.target.closest("[data-select]"); if (target) select(target.dataset.select); });
  detail.addEventListener("click", event => {
    const target = event.target.closest("[data-type]");
    if (!target) return;
    if (!curated.has(target.dataset.type) && mode === "guided") changeMode("catalog");
    select(target.dataset.type); detail.scrollTop = 0;
  });
  window.addEventListener("hashchange", () => { const id = location.hash.slice(1); if (all.has(id)) select(id, false); });
  const initial = location.hash.slice(1);
  if (all.has(initial) && !curated.has(initial)) mode = "catalog";
  changeMode(mode);
  select(all.has(initial) ? initial : data.types.find(node => node.qualified === "FScene").id, false);
})();
