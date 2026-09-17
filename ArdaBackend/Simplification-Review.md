# Backend and dependency graph review

Review date: September 17, 2026.

The implemented changes remove **646 production lines (0.99%)** while retaining the public APIs, native providers, and existing test coverage. The requested 50% reduction was not achieved. The reviewed code does not contain enough interchangeable implementation to justify deleting 32,596 lines without removing behavior or making the code harder to read.

## Findings fixed

| Priority | Finding | Resolution and regression coverage |
| --- | --- | --- |
| P1 | Shader parameter metadata constructed member-name strings and dereferenced nested metadata before validating them. Null names or null nested descriptors could crash instead of returning an error. Invalid nested structure bounds were also checked too late. | Validate structure during the single flattening traversal, before dereferencing. Invalid metadata yields an empty flattened result. Covered by `ArdaShaderStructs.RejectsInvalidMembersBeforeFlattening`. |
| P1 | A manually authored resource array could declare more references than its storage held. Binding construction then read outside the parameter struct. Multiplying count by stride would also permit integer wraparound. | Validate reference size, alignment, stride, and the final element with division-based bounds. Keep padded strides and unpadded final elements valid. Covered by `ArdaShaderStructs.ValidatesResourceArrayStorageWithoutOverflow`. |
| P2 | Batch shader publication could move an existing directory out of the destination path as though it were an artifact backup. Separate single-job and batch rollback implementations had diverged. | Use one publication transaction, reject non-file destinations, restore backed-up files on failure, and retain a backup if restoration itself fails. Directory obstruction and Windows locked-sidecar cases extend `ArdaShaderStructs.BuildsAndCooksRegistrationDrivenShaderJobs`. |
| P2 | Texture and sampler descriptors considered positive and negative floating-point zero equal but hashed their different object representations. Equivalent sampler requests could therefore miss the cache and create duplicate native samplers. | Hash floats through the existing value-hash combiner. Covered by `ArdaRHI.EqualDescriptorsHashSignedZeroIdentically` and the two-provider `EquivalentSignedZeroSamplersShareCacheEntry` regression. Persistent shader/PSO hash formats are unchanged. |

The changes are in [shader parameter validation](../../Source/ArdaInfra/ArdaBackend/Private/ShaderStructs/ArdaShaderParameters.cpp), [shader publication](../../Source/ArdaInfra/ArdaBackend/Private/ShaderStructs/ArdaShaderCompiler.cpp), and [descriptor hashing](../../Source/ArdaInfra/ArdaBackend/Private/RHI/ArdaRHITypes.cpp).

## Simplifications implemented

- **RHI:** Texture and buffer imports share validation, cache lookup, allocation verification, initial-state resolution, and lifetime retention. Graphics/mesh shader resolution and pipeline binding-layout admission share implementations. Acceleration structures and opacity micromaps share their common state and lifetime implementation. Bulk submission delegates to the single-list submission path after checking queue compatibility.
- **Native providers:** Vulkan graphics and mesh pipelines share raster pipeline setup. D3D12 pipelines share blend, rasterizer, and depth-state conversion; indirect commands share command-signature execution. Both providers use one staging-copy implementation for upload and readback.
- **Shader support:** Single-job and batch publication share rollback. Diagnostic construction and temporary-file cleanup are centralized. The loaded shader reference replaces a parallel load-state array, including its unreachable failure state.
- **Directed graph:** One outgoing endpoint index replaces three indexes for the same edge. Incoming/outgoing adjacency vectors remain, preserving efficient traversal and removal. Public lookup APIs and expected constant-time endpoint lookup remain available.
- **Dependency graph:** One node-binding record replaces six parallel per-node maps. Typed native resource records, import/access checks, queue transfers, and state-conformance capture share implementations. Native record storage owns entries in one vector, removing the duplicate raw-pointer vector and the failure window between updating two containers.

The graph changes retain generation-checked handles, transaction snapshots, attachment ordering, resource hazards, queue ownership, completion tickets, frame reuse, adaptive scheduling, timing, and CUDA paths. Similar-looking memory-node admission and scalar barrier lowering retain their distinct policies.

## Measurement

| Module | Before | After | Net lines removed | Reduction |
| --- | ---: | ---: | ---: | ---: |
| ArdaBackend | 28,601 | 28,402 | 199 | 0.70% |
| ArdaBackendImpls | 22,298 | 22,094 | 204 | 0.91% |
| ArdaGraph | 828 | 791 | 37 | 4.47% |
| ArdaRenderGraph | 13,465 | 13,259 | 206 | 1.53% |
| **Total** | **65,192** | **64,546** | **646** | **0.99%** |

Counts are physical lines in production `.h`, `.cpp`, `.cuh`, and `.cu` files under the four module directories, excluding `Tests`. The baseline is the working tree at review start, including the user's existing Vulkan changes. Documentation, CMake, tests, generated output, third-party code, and unrelated working-tree changes do not contribute to the reduction. Both versions use the repository's expanded C++ layout; clang-format 19 was applied to changed sources. Added validation is included in the final production count.

The backend and providers together shrink by 403 lines; the two graph modules shrink by 243. The structural gains exceed the line-count reduction: fewer indexes and containers must remain synchronized, and future fixes to shared paths have one implementation to update.

## Validation

- **Debug, MSVC, D3D12 + Vulkan:** full project build passed, including the examples and standalone backend/graph public-header checks. The complete CTest inventory contains 865 cases: 719 passed and 146 were skipped for unavailable build/hardware capabilities, with no unresolved failures.
- **Rollback fixture correction:** the first complete run had one failure in the new test fixture, which restored an LF manifest through a Windows text-mode stream. Restoring in binary mode preserves the original bytes. The rebuilt failed case then passed with `ctest --test-dir build --rerun-failed --output-on-failure`. No implementation change was needed after the complete run.
- **Release, MSVC, D3D12 + CUDA:** backend, graph, and RHI test targets built successfully in `build/pixel-sort-parameter-validation`. The focused CUDA, shader-struct, and signed-zero regressions ran 174 cases: 110 passed, 64 skipped, zero failures. This configuration has Vulkan disabled; skipped capabilities, including unavailable CUDA modes, are not claimed as runtime-validated.
- **Static checks:** clang-format 19.1.5 reports no changes needed in the 21 modified module/test files; the type-name audit checked 1,420 declarations in 327 authored files with zero violations; `git diff --check` passed.
- **Baseline:** before refactoring, the existing Debug suite ran 860 cases with zero failures. No existing tests were removed. Five additional registered cases come from the new regressions, and the existing compiler test now covers more failure paths.

Local evidence is retained in `build/simplification-full-build.log`, `build/simplification-final-ctest.log`, `build/simplification-rerun-ctest.log`, `build/simplification-cuda-ctest.log`, and their JUnit XML results. The initial and final production counts are in `build/simplification-baseline.json` and `build/simplification-final-metrics.json`.

## Assessment of the 50% target

Most remaining volume implements distinct native API behavior, public contracts, validation, resource lifetime, synchronization, shader compilation, and graph execution. Removing one provider or advanced features would reduce code substantially but conflict with the requested feature preservation. Hiding forwarding methods in macros, collapsing formatting, or deleting public documentation would lower a line count without improving maintainability.

Further worthwhile work would need its own design and measurements:

- Break the large provider and RHI implementation files into focused components to make review easier; splitting files alone does not shrink the codebase.
- Consider a descriptor-field schema that drives equality, in-memory hashing, and persistent serialization, with explicit rules for object identity and floating-point values. Persistent cache compatibility must be preserved or deliberately versioned.
- Investigate remaining scalar barrier lowering and memory-node overlap only after documenting their differing state, range, admission, and rollback rules. Their similar structure is not sufficient evidence that their semantics are interchangeable.

No remaining architectural change identified in this review provides a defensible estimate of a feature-preserving 50% reduction. The implemented refactors are independently reviewable and preserve the existing module boundaries.
