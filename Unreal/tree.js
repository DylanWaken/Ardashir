(() => {
  "use strict";
  const {data, escape: e, sourceMarkup} = window.Atlas;
  const catalog = window.UnrealCatalog, primitive = window.UnrealPrimitives, storage = window.UnrealPrimitiveStorage;
  const all = new Map([...catalog.types, ...primitive.relatedTypes, ...primitive.types].map(node => [node.id, node]));
  const curated = new Map(data.types.map(node => [node.id, node]));
  const profiles = new Map(storage.profiles.map(profile => [profile.id, profile]));
  const hierarchy = window.UnrealHierarchy.createHierarchy(primitive.types);
  const fullHierarchy = window.UnrealHierarchy.createHierarchy([...all.values()]);
  const roots = {components:primitive.roots.UPrimitiveComponent, proxies:primitive.roots.FPrimitiveSceneProxy};
  const tree = document.getElementById("tree"), search = document.getElementById("type-search");
  const moduleFilter = document.getElementById("module-filter"), detail = document.getElementById("type-detail");
  const more = document.getElementById("more-types");
  const counts = new Map();
  for (const node of primitive.types) counts.set(node.qualified, (counts.get(node.qualified) || 0) + 1);
  let mode = "components", selected = roots.components, limit = 150, timer;

  document.getElementById("inventory-stat").innerHTML = `<strong>${primitive.types.length}</strong><span>primitive class definitions<br>2 actual inheritance trees</span>`;
  document.getElementById("scope-content").innerHTML = `<p><strong>Every tree edge is a declared C++ base:</strong> the component tree starts at <code>UPrimitiveComponent</code>; the proxy tree starts at <code>FPrimitiveSceneProxy</code>. Components create proxies through an association; neither root is a subclass of the other. Interfaces/mixins outside these roots are listed in the inspector. A class with multiple in-tree bases appears under each.</p><p>Search and module filters retain ancestors in their original positions. The separate class reference is a flat inventory, not another hierarchy.</p><p>${e(primitive.meta.primitiveScope)}</p><p><strong>GPU data:</strong> ${storage.profiles.length} reviewed profiles explain major rendering families separately from class inheritance. Other subclasses explicitly show base-class context; their storage overrides have not all been reviewed. CPU wrapper objects are distinguished from GPU-resident bytes/texels. Same-name local definitions keep their source identity; the lexical parser does not include function names in qualification.</p><p>Reference inventory: ${catalog.meta.declarations.toLocaleString()} declarations across ${catalog.meta.files.toLocaleString()} files. ${e(catalog.meta.scope)} Primitive discovery additionally inspected ${primitive.meta.inspectedPrimitiveFiles} files across runtime and plugins. ${primitive.unresolved.length} ambiguous primitive descendants were excluded from the tree. See <a href="primitive-reference.md">all primitive definitions and storage explanations</a>; <a href="primitives.js">primitive source hashes</a>; <a href="primitive-storage.js">storage source hashes</a>.</p>`;
  moduleFilter.innerHTML += [...new Set([...all.values()].map(node => node.module))].sort().map(module => `<option>${e(module)}</option>`).join("");

  function typeButton(id, ambiguous = false) {
    const node = all.get(id);
    return node ? `<button type="button" data-type="${e(id)}">${e(node.qualified)}${ambiguous ? " · candidate" : ""}</button>` : "";
  }
  function resourceMarkup(item) {
    return `<details class="storage-card"><summary>${e(item.name)}</summary><p><strong>On the GPU:</strong> ${e(item.gpu)}</p><p><strong>CPU role / conditions:</strong> ${e(item.cpu)}</p>${sourceMarkup([item.source])}</details>`;
  }
  function storageMarkup(id) {
    const associations = storage.associations[id];
    if (!associations) return "";
    return `<section class="storage-section"><p class="eyebrow">ASSOCIATED DATA · NOT INHERITANCE</p><h3>GPU representation & storage</h3>${associations.map(association => {
      const profile = profiles.get(association.profileId), direct = association.fromId === id;
      const peers = profile.peerIds.filter(peer => peer !== id);
      return `<div class="storage-profile"><h3>${e(profile.title)}</h3>${direct ? '<p class="section-note">Reviewed mapping for this definition.</p>' : `<p class="fallback-note">Base-class context from ${typeButton(association.fromId)}. This subclass's resource overrides have not been separately reviewed; this is not a verified storage layout for the subclass.</p>`}<p>${e(profile.role)}</p>${profile.resources.map(resourceMarkup).join("")}<p class="section-note">${e(profile.caveat)}</p>${peers.length ? `<h4>Related implementations · association only</h4><div class="relation-list">${peers.map(peer => typeButton(peer)).join("")}</div>` : ""}</div>`;
    }).join("")}<details class="scope common-storage"><summary>Shared GPU storage & CPU boundaries</summary>${storage.common.map(resourceMarkup).join("")}</details><details class="scope"><summary>${e(storage.topology.name)}</summary><p>${e(storage.topology.text)}</p>${sourceMarkup([storage.topology.source])}</details></section>`;
  }
  function select(id, updateHash = true) {
    const node = all.get(id);
    if (!node) return;
    selected = id;
    if (updateHash) history.replaceState(null, "", `#${id}`);
    tree.querySelectorAll(".selected").forEach(item => item.classList.remove("selected"));
    tree.querySelectorAll(`[data-select="${id}"]`).forEach(item => item.classList.add("selected"));
    const explanation = curated.get(id);
    const bases = node.baseLinks.map(base => `<div class="base-relation"><code>${e(base.name)}</code><div class="relation-list">${base.targets.map(target => typeButton(target, base.resolution !== "unique")).join("") || '<span class="unresolved">Outside inventory / unparsed</span>'}</div>${base.conditions?.length ? `<p class="section-note">Conditional: ${e(base.conditions.join("; "))}</p>` : ""}</div>`).join("");
    const children = fullHierarchy.children.get(id) || [];
    const relatedStages = data.stages.filter(stage => stage.typeIds.includes(id));
    detail.innerHTML = `<div class="badges"><span class="badge">${e(node.module)}</span><span class="badge neutral">${e(node.kind)} · CPU definition</span></div><h2 id="detail-title">${e(node.qualified)}</h2><p class="definition-location"><code>${e(node.path)}:${node.line}</code></p>
      ${hierarchy.nodes.has(id) ? '<p class="section-note">The tree shows C++ inheritance. GPU data associations are explained below.</p>' : `<p class="detail-lede">${e(explanation?.role || `Source-indexed ${node.kind} in ${node.module}. No class-specific semantic explanation is included for this reference entry.`)}</p>`}
      <h4>Declared direct bases · C++ inheritance</h4>${bases || '<p class="section-note">No declared base class.</p>'}
      ${sourceMarkup([node])}
      ${storageMarkup(id)}
      ${!hierarchy.nodes.has(id) && explanation?.implementation ? `<div class="operation-note"><h3>If implementing this responsibility</h3><p>${e(explanation.implementation)}</p></div>` : ""}
      <details class="scope subclasses"><summary>Direct subclasses in the inventory · ${children.length}</summary><div class="relation-list">${children.map(child => typeButton(child)).join("") || '<span class="unresolved">No direct subclass found in scope.</span>'}</div></details>
      ${node.methods.length || node.fields.length ? `<details class="scope"><summary>Observed source surface · mechanical hints</summary><p>Lexically observed names may include nested declarations; this is not a complete API or member layout.</p><p>${[...node.methods, ...node.fields].map(name => `<code>${e(name)}</code>`).join(", ")}</p></details>` : ""}
      ${relatedStages.length ? `<h4>Follow this type through the frame</h4><div class="relation-list">${relatedStages.map(stage => `<a href="deferred-pipeline.html#${stage.id}">${e(stage.title)} ↗</a>`).join("")}</div>` : ""}`;
    detail.scrollTop = 0;
  }
  function nodeElement(id, visible, matches, filtering, depth, ancestry = new Set()) {
    if (ancestry.has(id)) return document.createTextNode("");
    const node = hierarchy.nodes.get(id), next = new Set(ancestry).add(id);
    const childIds = (hierarchy.children.get(id) || []).filter(child => visible.has(child));
    const element = document.createElement("details");
    element.className = "type-node";
    element.dataset.node = id;
    const duplicate = counts.get(node.qualified) > 1;
    element.innerHTML = `<summary data-select="${id}" class="${id === selected ? "selected " : ""}${filtering && !matches.has(id) ? "ancestor-context" : ""}"><span>${e(node.qualified)}</span>${childIds.length ? `<span class="node-count">${childIds.length}</span>` : ""}${duplicate ? `<small class="definition-tag">${e(node.path.split("/").slice(-2).join("/"))}:${node.line}</small>` : ""}</summary><div class="node-note">${e(node.module)} · ${e(node.kind)}${filtering && !matches.has(id) ? " · ancestor retained" : ""}</div>`;
    if (childIds.length) {
      const branch = document.createElement("div"); branch.className = "branch";
      childIds.forEach(child => branch.append(nodeElement(child, visible, matches, filtering, depth + 1, next)));
      element.append(branch);
    }
    element.open = filtering || depth === 0 || ["UMeshComponent", "UStaticMeshComponent", "Nanite::FSceneProxyBase"].includes(node.qualified);
    return element;
  }
  function render() {
    const query = search.value.trim().toLowerCase(), filtering = Boolean(query || moduleFilter.value);
    const predicate = node => (!moduleFilter.value || node.module === moduleFilter.value) && (!query || `${node.qualified} ${node.path} ${curated.get(node.id)?.role || ""}`.toLowerCase().includes(query));
    tree.replaceChildren(); more.hidden = true;
    document.getElementById("expand-tree").disabled = mode === "catalog";
    document.getElementById("collapse-tree").disabled = mode === "catalog";
    if (mode === "catalog") {
      const filtered = [...all.values()].filter(predicate).sort((a, b) => a.qualified.localeCompare(b.qualified));
      document.getElementById("tree-count").textContent = `${filtered.length.toLocaleString()} definitions`;
      document.getElementById("tree-legend").textContent = "Flat class reference · select a definition to inspect all declared bases.";
      tree.innerHTML = filtered.slice(0, limit).map(node => `<button type="button" class="match ${node.id === selected ? "selected" : ""}" data-select="${node.id}"><strong>${e(node.qualified)}</strong><span>${e(node.path)}:${node.line}</span></button>`).join("");
      more.hidden = filtered.length <= limit;
      more.textContent = `Show more (${Math.min(limit, filtered.length)} of ${filtered.length})`;
      if (!filtered.length) tree.innerHTML = '<p class="empty">No matching definitions.</p>';
    } else {
      const {visible, matches} = hierarchy.filter(roots[mode], predicate);
      document.getElementById("tree-count").textContent = `${matches.size} ${filtering ? "matches" : "classes"}${filtering ? ` · ${visible.size - matches.size} ancestors` : ""}`;
      document.getElementById("tree-legend").textContent = "Parent → child means direct C++ inheritance. GPU storage appears in the inspector.";
      if (visible.size) tree.append(nodeElement(roots[mode], visible, matches, filtering, 0));
      else tree.innerHTML = '<p class="empty">No matches in this tree. Try the other root or the class reference.</p>';
    }
  }
  function changeMode(value) {
    mode = value; limit = 150;
    for (const name of ["components", "proxies", "catalog"]) document.getElementById(`mode-${name}`).setAttribute("aria-pressed", String(mode === name));
    render();
  }
  function navigate(id, updateHash = true) {
    if (!all.has(id)) return;
    const targetMode = hierarchy.nodes.has(id) ? (hierarchy.descendants(roots.components).has(id) ? "components" : "proxies") : "catalog";
    search.value = ""; moduleFilter.value = "";
    // A reference deep link must remain visible beyond the first results page.
    if (targetMode === "catalog") search.value = all.get(id).qualified;
    changeMode(targetMode); select(id, updateHash);
    const target = tree.querySelector(`[data-select="${id}"]`);
    if (target) {
      let parent = target.parentElement;
      while (parent && parent !== tree) { if (parent.tagName === "DETAILS") parent.open = true; parent = parent.parentElement; }
      const y = target.getBoundingClientRect().top - tree.getBoundingClientRect().top;
      tree.scrollTop += y - 25;
    }
  }
  for (const name of ["components", "proxies", "catalog"]) document.getElementById(`mode-${name}`).addEventListener("click", () => {
    changeMode(name);
    if (name !== "catalog" && !hierarchy.descendants(roots[name]).has(selected)) select(roots[name]);
  });
  document.getElementById("expand-tree").addEventListener("click", () => tree.querySelectorAll("details").forEach(node => { node.open = true; }));
  document.getElementById("collapse-tree").addEventListener("click", () => tree.querySelectorAll("details").forEach(node => { node.open = false; }));
  search.addEventListener("input", () => { clearTimeout(timer); timer = setTimeout(() => { limit = 150; render(); }, 90); });
  moduleFilter.addEventListener("change", () => { limit = 150; render(); });
  more.addEventListener("click", () => { limit += 150; render(); });
  tree.addEventListener("click", event => { const target = event.target.closest("[data-select]"); if (target) select(target.dataset.select); });
  detail.addEventListener("click", event => { const target = event.target.closest("[data-type]"); if (target) navigate(target.dataset.type); });
  window.addEventListener("hashchange", () => navigate(location.hash.slice(1), false));
  navigate(all.has(location.hash.slice(1)) ? location.hash.slice(1) : roots.components, false);
})();
