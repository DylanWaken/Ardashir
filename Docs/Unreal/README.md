# Unreal rendering atlas

Open [Scene & GPU types](scene-types.html) or [Deferred rendering with Nanite & Lumen](deferred-pipeline.html). Both pages work as local files and on a static HTTP server. They require no CDN, package installation or engine checkout at viewing time. The [text reference](reference.md) contains the authored explanations and implementation checklists without JavaScript.

The source is Unreal Engine 5.8.1, commit `71fe36aac5a8df5ccd66c763ffc902b29b6a9c43`. The catalog is a **scoped lexical inventory**, not every generated/plugin/editor type in Unreal. Read the coverage explanation in the page and the file hashes in [source-manifest.json](source-manifest.json). Source hyperlinks target Epic's access-controlled GitHub repository; “Copy path” gives the path in `D:/UnrealEngine`.

To preview from the repository root:

```powershell
python -m http.server 8765 --bind 127.0.0.1 --directory Docs
```

Then open `http://127.0.0.1:8765/Unreal/scene-types.html`.

To regenerate against an available engine checkout:

```powershell
python Scripts/Docs/BuildUnrealCatalog.py --engine-root D:/UnrealEngine
python Scripts/Docs/BuildUnrealGuide.py --engine-root D:/UnrealEngine
python -m unittest discover -s Scripts/Docs -p "test_*.py"
python Skills/document-codebase/scripts/validate_docs.py
```

Pass `--check` to either builder to detect stale generated output. `BuildUnrealGuide.py` rejects missing/moved pinned anchors. When changing Unreal versions, re-review the authored explanations in `Scripts/Docs/UnrealGuideContent.py`, not just the line numbers. C++ inheritance is extracted; group membership and semantic responsibilities are authored. Conditional bases retain their preprocessor conditions, and ambiguous base candidates remain explicit. The parser is intentionally not a compiler and does not evaluate build configuration.

Operation checklist marks are stored only in the current browser, keyed by source revision. They are personal planning state, not implementation/test results. The “Clear operation checklist” button resets them. The configuration switches illustrate alternatives and never modify Unreal settings.

Verification includes extractor/data regression tests, the repository documentation validator, browser interaction checks and the requested independent [docs-only question audit](integrity-audit.md), including findings, fixes and retest results.
