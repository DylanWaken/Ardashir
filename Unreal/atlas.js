/* Shared, dependency-free rendering helpers. All source strings are escaped. */
(() => {
  "use strict";
  const data = window.UnrealGuide;
  const escape = value => String(value ?? "").replace(/[&<>"']/g, char => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[char]));
  const list = (values, ordered = false, className = "") => `<${ordered ? "ol" : "ul"} class="${escape(className)}">${values.map(value => `<li>${escape(value)}</li>`).join("")}</${ordered ? "ol" : "ul"}>`;
  const sourceMarkup = sources => `<section class="sources"><h3>Verify in the source</h3><p class="section-note">Paths and one-based lines are pinned to the checkout below. GitHub source links require Epic repository access.</p>${sources.map(source => {
    const relative = `${source.path}:${source.line}`;
    const remote = `https://github.com/EpicGames/UnrealEngine/blob/${data.meta.commit}/${source.path}#L${source.line}`;
    return `<div class="source-item"><div><code>${escape(relative)}</code>${source.label ? `<code>${escape(source.label)}</code>` : ""}<a href="${escape(remote)}" target="_blank" rel="noopener">Source at pinned commit ↗</a></div><button type="button" data-copy="${escape(data.meta.engineRoot + "/" + relative)}">Copy path</button></div>`;
  }).join("")}</section>`;
  const colors = {setup:"#426b91",geometry:"#185bb8",caches:"#08766d",lighting:"#9a6522",output:"#765598"};
  document.querySelectorAll("[data-version]").forEach(element => { element.textContent = data.meta.version; });
  document.querySelectorAll("[data-provenance]").forEach(element => { element.textContent = `UE ${data.meta.version} · commit ${data.meta.commit.slice(0, 12)} · ${data.meta.engineRoot}`; });
  document.getElementById("contracts").innerHTML = data.invariants.map(item => `<article class="contract"><h3>${escape(item.title)}</h3><p>${escape(item.text)}</p></article>`).join("");
  document.getElementById("faq").innerHTML = data.faq.map(item => `<details class="faq-item"><summary>${escape(item.question)}</summary><p>${escape(item.answer)}</p></details>`).join("");
  document.addEventListener("click", async event => {
    const button = event.target.closest("[data-copy]");
    if (!button) return;
    try {
      await navigator.clipboard.writeText(button.dataset.copy);
      button.textContent = "Copied";
    } catch {
      // file:// and non-secure hosts may not provide clipboard access.
      const input = document.createElement("textarea");
      input.value = button.dataset.copy;
      input.setAttribute("aria-label", "Source path to copy");
      button.after(input);
      input.focus();
      input.select();
      button.textContent = "Select & copy";
    }
  });
  window.Atlas = {data, escape, list, sourceMarkup, colors};
})();
