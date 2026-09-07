# Backend documentation

Start at [the overview](index.html). The backend guides include:

- [Resource recipes](resource-recipes.html): every RHI resource kind, allocation and ownership, with usage examples.
- [Capability recipes](capability-recipes.html): every predicate in the native capability conformance matrix, its guard and executable workload.
- [CUDA and graphics](cuda-interop.html): inherited operands, multiple kernels, graphics alternatives, architecture selection, packing and submission lifetime.
- [GPU validation setup](validation.html): local provisioning, discovery, unavailable-layer skips and failure diagnosis.
- [Complete examples](gpu-examples.html): compiled test helpers, shared fixtures, HLSL and artifact registrations.
- [Canonical API reference](api-reference.html): exact symbols and contracts, readable without JavaScript; JavaScript adds search and filtering.

## Keeping the docs synchronized

Public header comments own the CUDA and operand contracts. Explicit ownership, errors and threading comments also replace older authored API boilerplate while preserving canonical anchors. Edit these contracts in the header, then regenerate; do not edit the generated HTML or generated blocks in API data.

`Skills/document-codebase/scripts/backend_recipes.py` owns recipe explanations and selects literal code excerpts from compiled tests. It reads resource kinds from `ArdaRHIResource.h` and capability predicates from `ArdaExtendedRHIParityTests.cpp`, failing if any kind or predicate lacks coverage. `recipe-coverage.json` is a generated audit artifact.

Run from the repository root with Python 3.10+ and Node.js on PATH:

```sh
python Skills/document-codebase/scripts/sync_api_inventories.py
python Skills/document-codebase/scripts/backend_recipes.py
python Skills/document-codebase/scripts/sync_api_inventories.py --check
python Skills/document-codebase/scripts/backend_recipes.py --check
python Skills/document-codebase/scripts/validate_docs.py --verbose
```

Serve the repository with `python -m http.server 8873 --bind 127.0.0.1` and open `http://127.0.0.1:8873/Docs/ArdaBackend/` to check subpath hosting. Check mobile/desktop reflow, keyboard navigation, glossary focus, API search and JavaScript-disabled reading after layout changes. Source-backed examples still need native GPU tests; a skipped capability is not an executed test. See the validation guide before interpreting GPU results.
