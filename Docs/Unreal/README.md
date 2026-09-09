# Unreal rendering atlas

Open [Primitive class hierarchy & GPU storage](scene-types.html) or [Deferred rendering with Nanite & Lumen](deferred-pipeline.html). Both pages work as local files and on a static HTTP server. They require no CDN, package installation or engine checkout at viewing time. The [primitive reference](primitive-reference.md) contains the actual inheritance trees, all declared bases and storage explanations; the [pipeline/resource reference](reference.md) contains the rendering walkthrough and implementation checklists without JavaScript.

The source is Unreal Engine 5.8.1, commit `71fe36aac5a8df5ccd66c763ffc902b29b6a9c43`. The general catalog is a **scoped lexical inventory**; the separate primitive discovery follows declared descendants across runtime and plugin sources. Its 371 class definitions form two actual inheritance trees rooted at `UPrimitiveComponent` and `FPrimitiveSceneProxy`. Search/module filters preserve ancestors and every edge; GPU associations appear separately in 31 reviewed family profiles. Optional or unreviewed subclasses show explicit base context. This does not claim a complete compiler-generated inventory or a reviewed GPU field layout for every plugin.

Read the coverage explanation in the page and the file hashes in [source-manifest.json](source-manifest.json), [primitives.js](primitives.js) and [primitive-storage.js](primitive-storage.js). Source hyperlinks target Epic's access-controlled GitHub repository; “Copy path” gives the path in `D:/UnrealEngine`.

To preview from the repository root:

```powershell
python -m http.server 8765 --bind 127.0.0.1 --directory Docs
```

Then open `http://127.0.0.1:8765/Unreal/scene-types.html`.

To regenerate against an available engine checkout:

```powershell
python Scripts/Docs/BuildUnrealCatalog.py --engine-root D:/UnrealEngine
python Scripts/Docs/BuildUnrealGuide.py --engine-root D:/UnrealEngine
python Scripts/Docs/BuildUnrealPrimitives.py --engine-root D:/UnrealEngine
python Scripts/Docs/BuildUnrealPrimitiveStorage.py --engine-root D:/UnrealEngine
python -m unittest discover -s Scripts/Docs -p "test_*.py"
node --test Scripts/Docs/test_unreal_tree.cjs
python Skills/document-codebase/scripts/validate_docs.py
```

Pass `--check` to any builder to detect stale generated output. Storage/guide builders reject missing source anchors. When changing Unreal versions, re-review the authored explanations in `Scripts/Docs/UnrealGuideContent.py` and `Scripts/Docs/UnrealPrimitiveStorage.py`, not just the line numbers. C++ inheritance is extracted; storage associations and semantic responsibilities are authored. Conditional bases retain their preprocessor conditions, and ambiguous base candidates remain explicit. The parser is intentionally not a compiler and does not evaluate build configuration. It retains source locations to distinguish same-name local classes but does not qualify their names with enclosing functions.

Operation checklist marks are stored only in the current browser, keyed by source revision. They are personal planning state, not implementation/test results. The “Clear operation checklist” button resets them. The configuration switches illustrate alternatives and never modify Unreal settings.

Verification includes extractor/data regression tests, the repository documentation validator, browser interaction checks and the requested independent [docs-only question audit](integrity-audit.md), including findings, fixes and retest results.
