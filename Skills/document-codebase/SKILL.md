---
name: document-codebase
description: Builds and audits this repository's textbook-depth, GitHub Pages-compatible documentation, including one centralized canonical API reference per module, conceptual chapters, tutorials, glossary entries, and explanatory diagrams. Use when creating, expanding, restructuring, validating, or repairing codebase documentation, architecture pages, API references, rendering-system chapters, or technical guides for this repository.
---

# Document the Codebase

Create documentation that works simultaneously as a navigable engineering wiki, a canonical API reference, and a production-quality real-time rendering textbook. Begin with transferable graphics, computer science, and mathematics fundamentals; build durable mental models; then connect them precisely to this project's architecture, code, and engineering constraints.

Do not rely on an API list stored in this skill. Discover the current source tree every time.

## Non-negotiable outcomes

- Publish static HTML, CSS, JavaScript, and SVG that work on GitHub Pages.
- Use relative URLs throughout. Support hosting beneath an arbitrary repository subpath.
- Do not require server routing, server-side rendering, databases, runtime source access, or other server-only features.
- Preserve source-of-truth spelling, capitalization, namespaces, signatures, and ownership names.
- Inventory public code before writing or revising prose.
- Give every module exactly one centralized `api-reference.html` containing both the complete searchable public-symbol index and the canonical detailed documentation for every public class, struct, enum, alias, constant, macro, free function, method, member variable, and user-facing tool.
- Never embed, append, or generate API lists or detailed API dumps in feature, topic, project, architecture, or tutorial chapters. Link each relevant symbol name in prose directly to its stable canonical entry in the module's `api-reference.html`.
- Teach mechanisms to textbook depth: fundamentals and mental models first, exact project architecture second, with memory/data layout, CPU/GPU execution, platform differences, ownership, synchronization, performance, invariants, alternatives, failure analysis, and production practice where relevant.
- Maintain one global glossary for graphics, computer science, mathematics, and project terminology.
- Give every difficult mechanism a purposeful SVG diagram, timeline, flowchart, memory-layout view, or example visualization that teaches something the prose alone does not make equally clear.
- Include practical usage recipes and realistic failure modes.
- Forbid shallow catalog prose, repetitive API dumps, unsupported claims about design intent, and diagrams that merely restate nearby text.

## Workflow

### 1. Establish scope and conventions

1. Read repository guidance, build files, existing documentation, and documentation tooling.
2. Identify modules from current source/build boundaries, not assumptions.
3. Inspect existing page layout, CSS, JavaScript, URL conventions, and generated artifacts before changing them.
4. Preserve good existing structure. Repair stale pages rather than duplicating them.
5. Keep generated files reproducible and clearly distinguish generated data from authored content.

### 2. Inventory public code first

Inspect public headers, exported declarations, public interfaces in implementation files, shaders exposed to callers, command-line entry points, examples, build targets, and user-facing tools. Follow includes, inheritance, aliases, macros, and registration mechanisms where necessary to understand the exposed surface.

Create or refresh a machine-checkable API inventory used by validation. Derive it from the current repository; never copy a fixed inventory from this skill. Record at least:

- stable symbol ID and canonical source name;
- kind: module, namespace, class, struct, enum, enumerator, alias, constant, macro, function, method, member variable, or tool;
- containing module/type/namespace and visibility;
- canonical declaration/signature, qualifiers, template parameters, defaults, and source location;
- summary, canonical module `api-reference.html` URL, and stable fragment anchor;
- parameters, return value, side effects, ownership/lifetime, errors/failure behavior, threading/synchronization, and performance notes when applicable;
- relationships such as inheritance, construction, calls, produces/consumes, and related symbols;
- inventory provenance so validation can report uncovered or stale symbols.

Use a deterministic ID convention based on canonical qualified names, with collision handling for overloads. Keep anchors stable when prose or layout changes.

Before writing, compare the inventory with existing documentation and produce a private work list of missing, stale, renamed, and orphaned entries.

### 3. Design the information architecture

Provide:

- a landing page with system purpose, audience paths, module map, and build/use entry points;
- one overview and exactly one centralized `api-reference.html` per module;
- a complete searchable index and every canonical detailed public-symbol entry inside that module's `api-reference.html`;
- feature/topic/project chapters that teach fundamentals, mental models, project design, diagrams, derivations, worked examples, invariants, recipes, failure modes, summaries, checkpoints, and further reading without embedded or appended API lists;
- cross-module architecture and execution-flow pages where useful;
- a global glossary;
- discoverable navigation, breadcrumbs, previous/next links where sequential reading helps, and source links where stable.

Every public inventory item must appear in exactly one canonical detailed location: its owning module's `api-reference.html`. Do not duplicate generated symbol indexes, signature catalogs, member lists, or detailed entries elsewhere. Topic prose should naturally link individual relevant names to their stable API anchors, while remaining conceptual and task-oriented. The centralized API page may link back to chapters that teach context, but those chapters are not API-detail destinations.

### 4. Write as a production textbook

Each substantial chapter must include:

- explicit learning objectives and relative links to prerequisite chapters or glossary concepts;
- a progression from transferable graphics/CS/math foundations, through a mental model and design derivation, to exact project types, flows, and source-backed behavior;
- purposeful diagrams for every difficult mechanism, especially execution order, state transitions, memory layout, resource ownership, synchronization, and CPU/GPU interaction;
- at least one worked example that applies the model step by step;
- a summary, reader checkpoints or self-test questions, and curated further-reading links inside the documentation;
- direct relative links from relevant symbol names to canonical entries in the owning module's `api-reference.html`.

For each topic, answer:

1. What problem does it solve, and for whom?
2. Which transferable graphics, CS, and mathematics principles are prerequisites?
3. What mental model predicts its behavior, and how is the design derived from constraints?
4. Where does it sit in the larger system, and how are data and memory laid out?
5. How do control and data move across CPU, GPU, queues, threads, frames, and platform boundaries?
6. Why is it designed this way, and what alternatives or tradeoffs matter?
7. Which invariants, ownership/lifetime rules, ordering constraints, state transitions, and synchronization rules must hold?
8. What are the performance model, bottlenecks, scaling behavior, and measurement practices?
9. How do APIs, capabilities, memory models, and synchronization differ across supported platforms/backends?
10. How is it used in a worked, realistic scenario?
11. How does it fail, how is failure diagnosed, and how is it recovered from in production?
12. Which canonical APIs, glossary terms, neighboring chapters, source locations, and further reading provide detail?

Separate verified source facts from interpretation. Do not invent intent; mark unresolved rationale explicitly and cite evidence from code, tests, examples, comments, or build configuration. Do not substitute a catalog of names for an explanation: prose must build a causal model that lets a reader predict behavior, reason about tradeoffs, and diagnose failures.

### 5. Add recipes and failure modes

Recipes must be copyable, minimal, and grounded in current repository APIs. Include prerequisites, setup, complete critical calls, expected result, cleanup/lifetime behavior, and links to all involved API details.

Failure-mode sections should cover precondition violations, invalid state/order, ownership and lifetime mistakes, synchronization hazards, unsupported capabilities, malformed inputs, build/configuration errors, and diagnostic signals where relevant. State symptom, cause, prevention, and recovery.

## Concise page templates

### Module overview or chapter

```html
<main>
  <header><!-- purpose, audience, learning objectives, source boundary --></header>
  <nav aria-label="On this page"><!-- relative fragment links --></nav>
  <section id="prerequisites"><!-- relative links to required concepts --></section>
  <section id="fundamentals"><!-- transferable graphics/CS/math model --></section>
  <section id="architecture"><!-- derivation, exact project design, teaching diagrams --></section>
  <section id="execution"><!-- layout, CPU/GPU flow, ownership, synchronization --></section>
  <section id="tradeoffs"><!-- alternatives, platforms, performance, invariants --></section>
  <section id="worked-example"><!-- step-by-step realistic application --></section>
  <section id="failure-modes"><!-- symptom/cause/prevention/recovery --></section>
  <section id="summary"><!-- recap and checkpoints --></section>
  <section id="further-reading"><!-- internal and external learning links --></section>
</main>
```

Do not add an API list to this template. Link relevant names in its prose to `api-reference.html#stable-symbol-anchor`.

### Centralized module API reference

```html
<main>
  <header><!-- module scope, coverage, usage guidance --></header>
  <section id="api-search"><!-- progressively enhanced complete symbol search/filter --></section>
  <nav id="api-index"><!-- complete index linking to entries on this page --></nav>
  <section id="api-details">
    <!-- every canonical detailed public-symbol entry for this module -->
  </section>
</main>
```

### Detailed API entry

```html
<section class="api-entry" id="stable-symbol-anchor">
  <h3><!-- canonical qualified name --></h3>
  <pre><code><!-- canonical declaration/signature --></code></pre>
  <!-- purpose; parameters/members/enumerators; return; ownership/lifetime;
       state and side effects; errors; threading; performance; examples;
       related APIs; source link -->
</section>
```

Place this entry only in the owning module's `api-reference.html`. Adapt sections to the symbol kind, but never omit relevant contracts. Document public member variables and enumerators individually. API entries may link to conceptual chapters for deeper teaching; they must not delegate or fragment the canonical contract.

## Global glossary

Define domain terms once in a global glossary, grouped or filterable across graphics, CS, mathematics, and project concepts. Each entry needs:

- a stable, human-readable anchor independent of page ordering;
- concise plain-language definition;
- project-specific meaning or distinction;
- aliases/acronyms and related terms;
- links to representative topic and API pages.

In prose, the first meaningful use of a glossary term in a section must be a normal relative link to its glossary anchor and expose the short definition on both pointer hover and keyboard focus. Use semantic links enhanced by accessible tooltip markup/CSS and minimal progressive-enhancement JavaScript. Keep the definition available to assistive technology with `aria-describedby` or an equivalent robust association. Do not rely on `title`, hover alone, color alone, or JavaScript-only navigation. Tooltips must remain readable at zoom, avoid viewport clipping, dismiss with Escape when interactive, and never block the underlying link.

## Diagram and visualization rules

- Use SVG so visuals remain crisp, searchable, accessible, and GitHub Pages-compatible.
- Diagram every difficult mechanism using the form that best teaches it: boxes/arrows for architecture, timelines for ordering and synchronization, flowcharts for decisions, and compact views for memory/data layout, state, queues, or transforms.
- One diagram should communicate one main idea and expose a relationship, sequence, spatial layout, invariant, or contrast that is materially harder to learn from prose alone. Remove decorative nodes, repeated prose, and irrelevant internals.
- Match labels exactly to source-of-truth names. Distinguish calls, data flow, ownership, dependency, and optional paths with a legend when more than one relation appears.
- Keep direction consistent, minimize edge crossings, align to a grid, and ensure labels remain legible at narrow widths.
- Provide a meaningful `<title>`, `<desc>`, accessible association from the page, and nearby text containing the same essential conclusion.
- Use CSS-friendly styling, adequate contrast, non-color cues, responsive `viewBox`, and no inaccessible text rendered only as paths.
- Link diagram nodes to detailed docs when useful, using relative URLs.
- Prefer a tiny faithful diagram over a comprehensive but unreadable poster.
- Reject diagrams that merely restate headings, prose sentences, or flat symbol catalogs without adding explanatory structure.

## Static-site implementation rules

- Use standards-based HTML with semantic landmarks and a logical heading hierarchy.
- Keep core reading and navigation functional without JavaScript; use JavaScript only for progressive enhancement such as search, filtering, and glossary tooltips.
- Use relative asset/page links and directory-safe link resolution. Avoid root-relative `/...` URLs.
- Avoid network dependencies where practical. If vendored assets are needed, document provenance and licensing.
- Make layouts responsive without horizontal page scrolling; allow code blocks and wide diagrams to scroll within their own containers.
- Support keyboard use, visible focus, reduced motion, sufficient contrast, zoom/reflow, descriptive link text, and meaningful alt/accessibility text.
- Preserve deep links and stable anchors. Add redirects only in a GitHub Pages-compatible static form.
- Escape source-derived content and treat generated API data as untrusted input when rendering.

## Validation loop

Run available repository checks and add focused static checks when missing. Do not declare completion until all applicable checks pass:

1. Parse every HTML file and validate document structure, duplicate IDs, heading order, landmarks, language, titles, and required metadata.
2. Parse every SVG as XML and validate `viewBox`, title/description, link targets, text readability conventions, and unique IDs.
3. Crawl every relative page, asset, source, and fragment link from the same base-path behavior used by GitHub Pages.
4. Compare the fresh public-code inventory with each module's centralized `api-reference.html`. Fail on missing, duplicate, stale, orphaned, or noncanonical detailed entries.
5. Fail if feature/topic/project chapters contain generated API indexes, signature/member dumps, or canonical detailed entries; verify their relevant symbol links target stable anchors in module API references.
6. Check every substantial chapter for learning objectives, prerequisite links, fundamentals, mental model, project mapping, worked example, summary/checkpoints, further reading, and diagrams for difficult mechanisms.
7. Check glossary anchors, duplicate definitions, term links, hover/focus semantics, and accessible descriptions.
8. Check code/API names and signatures against source-of-truth declarations.
9. Test representative pages at narrow mobile, tablet, and wide desktop widths, plus 200% zoom.
10. Test keyboard-only navigation, focus order/visibility, skip links, API search, tooltips, reduced motion, contrast, and a representative screen-reader path.
11. Load the site from a local static file server under a non-root base path with network access disabled; verify no server-only assumptions.
12. Re-run checks after every correction and record commands/results in the work summary.

Validation scripts must report actionable file, anchor/symbol, and reason. Never hide exceptions merely to obtain a green result.

## Naive-reader acceptance test

After automated validation passes, launch an isolated naive-reader subagent with access to only the completed `Docs` tree. Do not provide source code, the API inventory, implementation notes, or answers.

Quiz it with concrete tasks that sample:

- finding the correct module and API for a realistic goal;
- using the centralized module API search/index to find and interpret a canonical contract;
- explaining a cross-component CPU/GPU flow, memory/data layout, ownership, synchronization, and ordering invariants;
- deriving a project design from transferable graphics/CS/math fundamentals and its documented mental model;
- locating and interpreting a graphics, CS, math, and project glossary term;
- following a worked example and predicting its result, resource lifetime, and performance implications;
- diagnosing a documented failure mode and selecting production-grade evidence or tooling;
- comparing an alternative or cross-platform tradeoff from the documentation;
- using chapter prose and diagram links to reach exact canonical API entries;
- answering chapter checkpoints and locating prerequisites and further reading.

Require the reader to cite the pages and anchors used, explain reasoning rather than repeat labels, state uncertainties, and flag contradictions or missing prerequisites. Treat confident unsupported answers, answers possible only by scanning API dumps, and inability to transfer a chapter's mental model to a new scenario as documentation failures. Patch shallow explanations, unclear navigation, missing canonical links, weak definitions, absent contracts, missing derivations, or non-teaching diagrams; rerun automated validation; then repeat the isolated test with a fresh naive reader. Continue until the sampled tasks are answerable from `Docs` alone.

## Completion checklist

- [ ] Current public code was inventoried before prose was written.
- [ ] Every module has exactly one centralized `api-reference.html` with a complete searchable index.
- [ ] Every public symbol/member/tool has exactly one canonical detailed entry in its owning module's `api-reference.html`.
- [ ] Feature/topic/project chapters contain no generated API lists, signature/member dumps, or detailed API entries.
- [ ] Relevant names in chapter prose link individually to stable canonical API anchors.
- [ ] Names, signatures, links, and source locations match current source.
- [ ] Chapters begin with learning objectives and prerequisite links, teach transferable fundamentals and mental models, then connect them to exact project architecture.
- [ ] Relevant chapters explain memory/data layout, CPU/GPU execution, platform differences, ownership, synchronization, performance, invariants, alternatives/tradeoffs, failure analysis, and production practices.
- [ ] Chapters include worked examples, design derivations, summaries/checkpoints, and further-reading links.
- [ ] Practical recipes and symptom/cause/prevention/recovery failure modes are present.
- [ ] Every difficult mechanism has a useful accessible SVG that teaches beyond the prose; no diagram merely restates text.
- [ ] No shallow catalog prose, repetitive API dumps, or unsupported design intent remains.
- [ ] The global glossary covers graphics, CS, math, and project terms with stable anchors.
- [ ] Prose terms provide accessible hover/focus definitions and normal glossary links.
- [ ] URLs are relative and the site works under a GitHub Pages subpath without server-only features.
- [ ] HTML, SVG, links, fragments, API coverage, responsive layouts, and accessibility pass validation.
- [ ] A fresh isolated naive reader succeeded using only `Docs`.
- [ ] Reader-discovered gaps were patched and both automated and reader tests were rerun.
- [ ] Final summary lists changed pages, inventory/coverage results, validation commands, and remaining known limitations.
