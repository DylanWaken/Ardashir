# Unreal rendering atlas

Open [Scene representation for your own renderer](scene-types.html) or [Follow a deferred frame](deferred-pipeline.html). Both pages work as local files and on a static HTTP server without a CDN or engine checkout at viewing time. The scene page is an implementation guide, organized by renderer inputs rather than a class tree: 10 sections, 38 requirements, 18 geometry families and 5 source-extracted classification enums. Each requirement lists data to carry, conversion/implementation work and source references.

Every one of the 21 deferred-frame stages has a **Functions & flow** tab: 94 selected function references and 92 clickable key operations across the page. References distinguish call sites from helper definitions and identify shader/RDG event labels separately. Arrows show selected control/data prerequisites, not an exhaustive C++ call graph or serial GPU timing. Existing configuration controls and the 89-operation planning checklist remain available.

Text equivalents: [scene requirements](scene-reference.md), [functions and operation flows](stage-functions.md), and [pipeline/resource reference](reference.md). The source is Unreal Engine 5.8.1, commit `71fe36aac5a8df5ccd66c763ffc902b29b6a9c43`. Suggested records and conversion policies are an authored design guide, not a public export ABI or claims of existing Ardashir support. New source hashes are included in [scene-guide.js](scene-guide.js) and [stage-details.js](stage-details.js). Source hyperlinks target Epic's access-controlled GitHub repository; “Copy path” gives the path in `D:/UnrealEngine`.

The earlier [primitive declaration appendix](primitive-reference.md), scoped [source manifest](source-manifest.json), `primitives.js` and `primitive-storage.js` remain as source/reference inputs. They do not define the scene page's layout. The lexical inventory is not a compiler-complete API, plugin coverage guarantee or reviewed GPU ABI for every class.

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
python Scripts/Docs/BuildUnrealSceneGuide.py --engine-root D:/UnrealEngine
python Scripts/Docs/BuildUnrealStageDetails.py --engine-root D:/UnrealEngine
python -m unittest discover -s Scripts/Docs -p "test_*.py"
node --test Scripts/Docs/test_unreal_tree.cjs
node --test Scripts/Docs/test_unreal_flows.cjs
python Skills/document-codebase/scripts/validate_docs.py
```

Pass `--check` to any builder to detect stale generated output. Builders reject missing source anchors; pinned function/call-site lines also fail if moved. When changing Unreal versions, re-review `UnrealSceneContent.py`, `UnrealStageDetails.py`, `UnrealGuideContent.py` and `UnrealPrimitiveStorage.py` under `Scripts/Docs`, not just line numbers. Scene classifications are extracted from the named enums; requirements, function explanations and operation flows are authored from source research.

Operation checklist marks are stored only in the current browser, keyed by source revision. They are personal planning state, not implementation/test results. The “Clear operation checklist” button resets them. The configuration switches illustrate alternatives and never modify Unreal settings.

Verification includes extractor/data regression tests, the repository documentation validator, browser interaction checks and the requested independent [docs-only question audit](integrity-audit.md), including findings, fixes and retest results.
