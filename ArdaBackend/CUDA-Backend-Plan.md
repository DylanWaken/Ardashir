# CUDA operand implementation record

The current design is implemented in the native D3D12/Vulkan providers and the RDG adapter. Read [operand authoring](cuda-interop.html) for source-backed recipes and [CUDA in graphics](cuda-graphics.html) for the general memory/scheduling model.

## Public contract

An operand dispatch launches exactly one precompiled kernel. Authors implement `BindKernelVariants()` and `SelectKernel()` plus a diagnostic name. `Dispatch()`, `DispatchDeferred()`, `PrepareDispatch()` and support validation belong to the framework and cannot be overridden. Algorithms needing several kernels can compose these operands with `FArdaCudaSequence`, or attach registered CUDA nodes to `FArdaDependencyGraph` for automatic batching, keeping one stream and one graphics handoff around the whole sequence. See [CUDA sequences](CUDA-Sequences.md) for usage and validation rules.

`ARDA_CUDA_PARAMETER_STRUCT` generates a host resource/value representation and its plain `FCuda` argument type from one field list. `TArdaDependencyCudaParameters` rebinds the same fields to logical graph accesses. Every bound kernel accepts the exact `FCuda` type by value as its only parameter.

Schema eligibility, unsupported host values, signatures, native coverage, resource formats/ranges/alignment and ownership are runtime checks with error statuses. Normal C++/CUDA syntax and template rules still apply. Nontrivial values and raw scalar pointers are rejected; authors must not hide host pointers or dependencies inside plain value aggregates.

Resource conversion means preparing CUDA pointers and arrays/surfaces from declared shared RHI resources. It does not normalize, change color space, swizzle or pack tensors. Such transformations require explicit passes. Buffer views are checked for bounds, element size and alignment; texture views select one mip and all layers of an admitted raw format. Graphics acceleration structures and specialized graphics objects are not CUDA storage handles.

Each `TArdaCudaKernelVariant<Kernel, Payload>` has a distinct symbol-specific wrapper type. A registry checks its signature and retains the compiled entry, diagnostic name, payload, requirements and generated build information. Binding is synchronized once per operand and freezes an immutable registry. Selection filters native coverage and execution requirements before consulting the author. The chosen launch plan freezes values and retained resource views; provider recording resolves addresses and checks actual compiled-kernel limits.

The backend provider interface version is now 12, including allocation-requirements and residency queries, retained CUDA Graph execution, submission completion and queue-qualified timestamp capabilities. The RHI supports explicit sequence recording, and native CUDA dispatch accepts a nonempty ordered batch of compiled kernels and external library calls against one resource binding table. Rebuild external provider modules and consumers against these headers; providers must process every operation in a batch or reject it. [External CUDA calls](CUDA-External-Calls.md) describes cuBLAS/cuDNN adapters and their context-owned handle/plan lifetime. [CUDA texture buffers](CUDA-Texture-Buffers.md) describes the explicit GPU copy path for processing graphics textures through linear CUDA pointers, including D3D12 CiG without native surface support.

## Build and packaging

`Cmake/ArdaCudaKernels.cmake` locates nvcc only during project configuration when `ARDASHIR_BUILD_CUDA_KERNELS` is enabled. Explicit `CMAKE_CUDA_COMPILER` or first-configure `CUDACXX` selects it for Ninja; Visual Studio uses its CUDA toolset. `find_package(CUDAToolkit)` provides matching libraries/includes. Disabling kernel builds skips compiler discovery; compiling the provider itself still needs CUDA SDK headers.

`ardashir_add_cuda_kernels` creates separate static targets for finite flag/architecture profiles, generates native coverage and a profile identity, and selects static cudart. Template permutations use C++17 integer sequences; compiler-option permutations use CMake targets/loops. Exported binding functions and generated profile namespaces prevent archive stripping and symbol collisions. `DEPENDS` adds included project files to the profile fingerprint; normal compiler dependency scanning also handles rebuilds.

The runtime PTX/module/compiler route is removed completely: no `FArdaCudaModule`, `FArdaCudaCompiler`, NVRTC adapter, string entry lookup or provider PTX launch path remains. The helper uses explicit `-real` native images. Missing architecture coverage returns Unsupported and requires a rebuilt package. Ordinary C++ `.h` consumers need no CUDA SDK; binding `.cu` translation units include the nvcc bridge.

Ship linked native code and required runtime libraries, with a compatible installed NVIDIA driver. End users do not need nvcc or a full CUDA Toolkit. The supplied static-library workflow keeps compiled code loaded for process lifetime; unloadable CUDA plugins require an additional explicit module-lifetime contract.

## Native execution and synchronization

- D3D12 CiG associates a CUDA context with the graphics queue and captures registered entry launches into the actual D3D12 command list. Streams, entries, mappings and allocations survive until graphics retirement. The current backend allows one captured-but-unsubmitted list per context and rejects replay or capture-order violations.
- Vulkan CiG reserves external compute capacity during Vulkan device creation, creates `VK_NV_external_compute_queue`, obtains its opaque data and creates a CUDA CiG context using the NV blob. Kernel invocation is a native CUDA stream launch. The prior `VK_NV_cuda_kernel_launch` module route is removed. Borrowed Vulkan devices have no external-queue reservation contract and cannot use this CiG path.
- Ordinary CUDA uses a context matched by D3D12 LUID/node mask or Vulkan UUID. The bridge pushes/restores its current context on each calling thread; it does not change a process-global device or expose raw streams to operands.
- Both ordinary contexts and Vulkan CiG use GPU synchronization around CUDA segments. D3D12 signals shared fence value 1; CUDA waits 1, launches, then signals 2; graphics waits 2. Vulkan exports a binary Ready/Done semaphore pair and releases/reacquires external queue-family ownership with appropriate image layout/barriers. Successful launches have no per-segment CPU idle wait. Failure cleanup drains partially enqueued CUDA work before releasing resources.
- The final graphics submission fence/timeline joins CUDA completion and determines retirement. Explicit `WaitForIdle` and host readbacks still wait as their public contracts require. CUDA-containing lists are single-use.

`Automatic` prefers CiG and falls back to ordinary CUDA when context creation or D3D12 surface qualification fails. `GraphicsQueue` requires CiG; `ContextSwitch` forces an ordinary context. Mode selection happens before resource allocation. Capabilities report surface support and layered support independently; allocation can still fail for a particular descriptor.

## Dependency graph integration

`RegisterArdaCudaOperandNode` installs a typed operand definition in the singleton
registry. `TArdaDependencyCudaParameters` binds its schema to graph resource values.
ArdaInductor derives resource dependencies, coalesces eligible CUDA chains, prepares
all steps, and retains a native capture cache per frame slot and batch. Each resource
value has one producer, and attachment order does not define execution order.

D3D12 CiG batches use ordered graphics-queue capture/submission. Failures can occur
after earlier work submitted; frame receipts retain accepted submissions until
completion and failed frames do not publish readbacks. `FArdaCudaSequence` remains
the backend's direct batch API for applications that do not use a dependency graph.

## Qualification and limits

Local hardware: Windows, NVIDIA RTX PRO 6000 Blackwell Workstation Edition, compute capability 12.0, driver 610.62, nvcc 13.3.33. The build-only toolkit is local ignored tooling, not an application runtime dependency.

Native tests verify immediate/deferred shared plans, frozen values, single-use recordings, resource runtime errors, nonzero buffer offsets/guards, kernel/resource lifetime and the coalesced graph result `Output[i] = Input[i] + 18`. These buffer/RDG tests pass on D3D12 ordinary CUDA, Vulkan ordinary CUDA, D3D12 CiG and Vulkan CiG. Nonzero-mip R32UInt surface tests pass on ordinary D3D12/Vulkan and Vulkan CiG.

D3D12 CiG surface mapping returns `CUDA_ERROR_UNKNOWN` on this driver. The provider's startup probe reports surface unavailability; that test explicitly skips and Automatic selects an ordinary context. Layered Vulkan imports remain excluded because earlier cross-API layer-stride qualification failed; the removal of the former Vulkan native-module route does not carry its old image-matrix qualification forward. This implementation does not claim all 24 raw formats × all dimensions have been requalified through the new compiled entries.

Host tests cover paired layout metadata, runtime signature/schema rejection, immutable registration, concurrent one-time binding, architecture filtering, permutations and single-kernel launch validation. CUDA-off builds compile the public facade without CUDA headers/compiler. Inspect packaged binaries with `cuobjdump --list-elf` and `--list-ptx`; the local tested executable contains native `sm_80`/`sm_120` code and no PTX.

Other architectures, Linux, alternate drivers, arbitrary external CUDA libraries, sampled textures, cooperative/cluster launches, dynamically unloaded kernel libraries and application-owned independent streams are not qualified by these tests. No automatic graphics retry occurs after CUDA recording or partial writes.

## Reproduction

Use the [build-time nvcc lookup recipe](cuda-interop.html#nvcc-build), then build `ArdaBackendTests`, `ArdaBackendPublicHeaders` and `ArdaGraphTests`. Run the backend executable with `--gtest_filter=ArdaCompute*:ArdaCuda*:*ArdaCudaGpu*`, followed by the broader backend/RHI/RDG regression suites. Configure another build with CUDA disabled for the SDK-free contract. Native validation must be available; distinguish its explicit capability skips from executed tests.

Documentation is generated from the Python sources named in [README.md](README.md). Regenerate inventories/recipes, run `validate_docs.py`, check responsive and no-JavaScript reading, and use a fresh Docs-only reader before publishing.

Local validation completed on 2026-09-10:

- CUDA-enabled backend/public-header/RDG targets and CUDA-disabled backend/public-header targets build successfully. After cleanup, the focused CUDA/operand/provider suite passes 31 tests with one D3D12 CiG surface capability skip; the CUDA-disabled suite passes all 12 tests. Regression cases include independent parameter snapshots for two dispatches in one command list, operand destruction before submission, and fast-math opt-in/portable fallback.
- The native-validation RDG regression suite passes all 57 tests. The broader backend run passes 291 tests with 31 explicit skips, and the RHI suite passes all 17 tests. Focused CUDA tests run separately to qualify all four execution modes independently of broader-suite adapter selection.
- The standalone triple-chevron example builds. `cuobjdump --list-elf` reports native `sm_80`/`sm_120` images and `--list-ptx` reports no PTX. `dumpbin /dependents` confirms the tested executable has no dynamic CUDA runtime dependency; the NVIDIA driver remains required.
- Configuration probes confirm missing nvcc produces the build-time diagnostic and `80-virtual` is rejected. Namespace and opaque-RHI boundary checks pass with `ARDASHIR_SOURCE_DIR` set to the repository's `Source` directory; `git diff --check` passes.
- API inventory and recipe `--check` commands pass. `validate_docs.py` checks 34 HTML pages, 52 SVGs and the complete public inventories with zero errors. Twenty browser scenarios cover both CUDA chapters and the affected RDG guides at desktop/tablet/mobile/zoom-equivalent widths, local subpath hosting without external network access, no-JavaScript CUDA reading, keyboard navigation, glossary focus and API search.
- A fresh isolated Docs-only reader passes the sampled authoring, runtime, build/deployment, graphics synchronization, RDG failure and worked-example tasks, with 402 local page/fragment/image references resolving successfully.

Cleanup consolidates device admission and candidate filtering into one preparation path, resolves native parameter patches once during recording, and retains only the compiled entry, launch configuration and patched values. The single-use validation template/header, unused device argument to metadata `Prepare`, unnecessary device-header dependency, redundant single-kernel loops and unreferenced context-switch diagram are removed. Resource eligibility checks at the host, RHI and native boundaries remain necessary for callers entering at those different layers.
