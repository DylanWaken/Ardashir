(() => {
  "use strict";
  const {escape:e, list, sourceMarkup} = window.Atlas;
  const data = window.UnrealSceneGuide;
  const sectionNames = {contract:"Input contract", geometry:"Geometry families", materials:"Material behavior", textures:"Textures & buffers", instances:"Placements & visibility", lighting:"Lights & baked lighting", environment:"Atmosphere & effects", views:"Views & final image", updates:"Updates & lifetime", fidelity:"Fidelity & acceptance"};
  document.getElementById("scene-nav").innerHTML = data.sections.map((section,i) => `<a href="#${section.id}"><span>${String(i+1).padStart(2,"0")}</span>${e(sectionNames[section.id])}</a>`).join("");
  function enumMarkup(item) {
    return `<details class="scene-enum"><summary>${e(item.name)} <span>· values from this checkout</span></summary><p>${e(item.note)}</p><ul class="enum-values">${item.values.map(value => `<li><code>${e(value)}</code></li>`).join("")}</ul>${sourceMarkup([item.source])}</details>`;
  }
  document.getElementById("scene-content").innerHTML = data.sections.map((section,i) => `<section class="scene-section" id="${section.id}" aria-labelledby="heading-${section.id}"><div class="scene-section-heading"><div><p class="eyebrow">${String(i+1).padStart(2,"0")} / SCENE REPRESENTATION</p><h2 id="heading-${section.id}">${e(section.title)}</h2></div><button type="button" data-expand-section="${section.id}">Expand section</button></div><p class="section-lede">${e(section.summary)}</p>
    ${section.items.map((item,index) => `<details class="requirement" ${(section.id === "contract" || (section.id === "geometry" && index === 0)) ? "open" : ""}><summary><span>${e(item.title)}</span></summary><div class="requirement-body"><p>${e(item.text)}</p><div class="requirement-columns"><div><h3>Data to carry</h3>${list(item.fields)}</div><div><h3>Work & conversion policy</h3>${list(item.work)}</div></div>${item.unrealTypes?.length ? `<p class="source-types"><strong>Unreal examples:</strong> ${item.unrealTypes.map(name => `<code>${e(name)}</code>`).join(", ")}</p>` : ""}${item.storage?.length ? `<details class="storage-example"><summary>Existing Unreal GPU storage</summary>${item.storage.map(resource => `<h4>${e(resource.name)}</h4><p><strong>GPU data:</strong> ${e(resource.gpu)}</p><p><strong>CPU boundary:</strong> ${e(resource.cpu)}</p>`).join("")}</details>` : ""}${item.sources.length ? `<details class="requirement-sources"><summary>Source references</summary>${sourceMarkup(item.sources)}</details>` : ""}</div></details>`).join("")}
    ${section.id === "materials" ? `<div class="enum-reference"><h3>Material classifications to recognize</h3><p class="section-note">These enum names classify behavior; handling an enum value still requires the corresponding material evaluation and render path.</p>${data.enums.filter(item => item.name !== "EPrimitiveType").map(enumMarkup).join("")}</div>` : ""}
    ${section.id === "geometry" ? data.enums.filter(item => item.name === "EPrimitiveType").map(enumMarkup).join("") : ""}</section>`).join("");
  document.querySelectorAll(".requirement-body").forEach((body, index) => {
    const links = data.sections.flatMap(section => section.items)[index].links;
    if (links?.length) body.insertAdjacentHTML("beforeend", `<p class="section-note">${links.map(([title,url]) => `<a href="${e(url)}">${e(title)} ↗</a>`).join(" · ")}</p>`);
  });
  document.addEventListener("click", event => {
    const button = event.target.closest("[data-expand-section]");
    if (!button) return;
    const section = document.getElementById(button.dataset.expandSection);
    const expand = [...section.querySelectorAll(":scope > .requirement")].some(item => !item.open);
    section.querySelectorAll(":scope > .requirement").forEach(item => { item.open = expand; });
    button.textContent = expand ? "Collapse section" : "Expand section";
  });
  function syncSectionButton(section) {
    const items = [...section.querySelectorAll(":scope > .requirement")];
    section.querySelector("[data-expand-section]").textContent = items.every(item => item.open) ? "Collapse section" : "Expand section";
  }
  document.querySelectorAll(".scene-section").forEach(section => {
    syncSectionButton(section);
    section.querySelectorAll(":scope > .requirement").forEach(item => item.addEventListener("toggle", () => syncSectionButton(section)));
  });
  function followHash() {
    let id = location.hash.slice(1);
    // Preserve old source-type deep links by taking readers to the relevant input section.
    if (data.typeSections[id]) { id = data.typeSections[id]; history.replaceState(null, "", `#${id}`); }
    if (!data.sections.some(section => section.id === id)) return;
    document.querySelectorAll("#scene-nav a").forEach(link => {
      if (link.hash === `#${id}`) link.setAttribute("aria-current", "location"); else link.removeAttribute("aria-current");
    });
    document.getElementById(id).scrollIntoView({block:"start"});
  }
  window.addEventListener("hashchange", followHash);
  if (location.hash) requestAnimationFrame(followHash);
})();
