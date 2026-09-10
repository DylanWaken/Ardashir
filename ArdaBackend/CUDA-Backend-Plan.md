# CUDA backend integration plan

Scope: backend contracts, native providers, reusable compute operands, and GPU tests. RDG registration and scheduling are a subsequent change.

## Design

1. Add opt-in shareable buffer/texture allocation. Query actual support using the device, descriptor, and CUDA execution mode. Represent buffers as linear ranges and textures as arrays/surfaces. Graphics acceleration structures are opaque implementation data, not CUDA/OptiX traversable handles; reject them explicitly and allow their ordinary source geometry buffers instead.
2. Keep representation support, mapped representations, and access ownership separate. A resource can have both graphics and CUDA views without permitting simultaneous writes. Native resources own mappings and their lifetime; callers cannot mutate tracker flags. Typed bindings retain the underlying RHI resource.
3. Add `TArdaComputeOperand<Parameters>` with user-overridden `Dispatch`, optional `DispatchDeferred`, and `GetOperandSupport`. The author owns host logic, shape/tuning policy, native library calls and any number of CUDA source files. Shader-like parameter metadata describes host values and retained resource dependencies independently of kernel ABI. The base has no implementation registry or selector policy.
4. Add optional native CUDA implementations with SDK-free public headers and runtime feature probing. Providers own allocation export, CUDA context identity, mapping, synchronization and launch. Use D3D12 CiG command-list capture and Vulkan native CUDA kernel launch where qualified. Use an ordinary CUDA context with deferred, serialized graphics/CUDA segments when native queue execution is unavailable or fails surface qualification. Never infer support from vendor name alone.
5. Integrate dispatch with existing RHI command recording/submission and retention. Preserve resource state validation and explicit asynchronous completion. Serialize work where native CiG ordering requires it. Native queue execution remains asynchronous. ContextSwitch deliberately blocks at submission boundaries; recording itself never launches ordinary-context kernels.
6. Add real GPU samples and contract tests: buffer kernels, multiple kernels, image writes/readback, graphics/CUDA handoffs, invalid bindings and architectures, resource lifetime, missing CUDA/fallback selection, and clean CUDA-off compilation. Run the existing backend regression suite after integration. Record actual tested paths separately from unavailable hardware/SDK paths.

## Acceptance

- Users derive a typed operand class, declare parameters once, and implement immediate execution and/or deferred recording in ordinary C++. Support queries and dispatch policy are user-defined.
- Shared data is checked against a CPU oracle after actual GPU execution; metadata and compilation checks alone are not a passing GPU test.
- Support queries and rejection messages agree with implemented paths. No fake acceleration-structure conversion or untested capability advertised as supported.
- Native CUDA headers and mandatory driver DLL linkage do not leak into core public contracts.
- Existing RDG changes remain untouched.

## Primary contracts

- [CUDA external resources](https://docs.nvidia.com/cuda/cuda-driver-api/group__CUDA__EXTRES__INTEROP.html)
- [CiG context creation](https://docs.nvidia.com/cuda/cuda-driver-api/group__CUDA__CTX.html)
- [CiG stream capture](https://docs.nvidia.com/cuda/cuda-driver-api/group__CUDA__STREAM.html)
- [CUDA graphics interoperability](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html)
- [Vulkan CUDA kernel launch](https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_cuda_kernel_launch.html)
- [Vulkan external compute queues](https://docs.vulkan.org/refpages/latest/refpages/source/VK_NV_external_compute_queue.html)

## Implemented backend contracts

`RHI/ArdaRHICuda.h` contains the portable launch descriptors. `Compute/ArdaComputeOperand.h` contains the inheritable operand. CUDA SDK types occur only in the native implementation. CUDA is disabled by default; native providers return an explicit unavailable reason when omitted or unsupported at runtime.

Set `mbCudaInterop` when creating a buffer or texture. This is an allocation requirement, not a conversion of an arbitrary existing resource. Use `Device.GetCudaCapabilities()` before choosing the allocation. Textures additionally require `mbSurfaceAccess`. Ordinary graphics resources continue to work with graphics implementations of an operand.

`Resource.GetCudaResourceInfo()` reports the actual representation established for that allocation:

- Ordinary allocations and opaque objects report `None`, `Graphics`, sharing disabled.
- A successfully qualified shared buffer reports `LinearBuffer`, `GraphicsAndCuda`, sharing enabled.
- A successfully qualified shared texture reports `Surface`, `GraphicsAndCuda`, sharing enabled.

These immutable properties are derived from native allocation/mapping state. They are not caller-controlled ownership flags. Native queue paths execute inside graphics API command streams. ContextSwitch transfers Vulkan ownership to EXTERNAL around CUDA and reacquires it before the next graphics segment; these temporary transitions remain private to the provider. Existing RHI state tracking and submission fences remain the source of truth for access ordering. Having both representations does not permit unsynchronized concurrent writes.

Buffers expose ranges to kernels, with the offset incorporated into the native address. Textures expose a surface for a selected mip, including its array layers or 3D depth. Neither leaks a native address through the public API. A Vulkan buffer device address is valid for `VK_NV_cuda_kernel_launch`; it is not offered as an ordinary CUDA-runtime pointer.

Supported texture storage comprises 24 raw integer/floating-point R, RG and RGBA formats, across 1D, 1D array, 2D, 2D array and 3D images. One format table supplies CUDA channel width/type/count and admission. Normalized, sRGB, BGRA, packed, compressed, depth/stencil, cube and multisample images are rejected. The API supplies surface reads/writes, not sampled texture objects or filtering. Packing into linear tensor storage, swizzling and color conversion are explicit operator work; they do not happen during binding.

Acceleration structures, opacity micromaps, sampler-feedback objects, samplers and pipelines retain `None`. A graphics AS address is not a CUDA/OptiX traversal handle. Ordinary vertex/index/instance source buffers can be shared. AS-storage buffers, CPU-visible, versioned, sparse and placed allocations are rejected. Native resource imports cannot opt into sharing without backend-created allocation/mapping ownership.

## Implementing a custom operand

Read [How CUDA works with graphics](cuda-graphics.html) for the general memory and synchronization model. The [two launch methods](cuda-interop.html#launch-methods) show both the implemented RHI/PTX descriptor path and an ordinary CUDA C++ host helper containing `Kernel<<<Grid, Block, 0, Stream>>>(Arguments)`. The direct helper is a documentation example; a compatible native resource/stream adapter is not implemented in the public RHI.

Derive `TArdaComputeOperand<FMyParameters>`. The non-template `FArdaComputeOperand` supplies type-erased identity, support and metadata; the typed base adds virtual `Dispatch(const FMyParameters&)` and `DispatchDeferred(IArdaRHICommandList&, const FMyParameters&)`. Implement `GetName` and `GetOperandSupport() const`. Bind a device, CUDA context, library engine or other runtime state through your own constructor. The support query does no launches; dispatch validates operation-specific shapes, dtype/layout, ranges and runtime/resource compatibility.

Immediate Dispatch executes user C++ now and may directly invoke native kernels or libraries. It defines its own completion contract and owns asynchronous resource/stream/runtime lifetime. Deferred Dispatch runs CPU recording logic now but appends GPU work to a caller-owned open command list; it must not close/submit it or launch unordered external work. Both default hooks return Unsupported. The deferred default never silently invokes immediate work. A compatible native-library submission/interop adapter is required before a library can participate in deferred RHI ordering.

`Compute/ArdaComputeParameters.h` supplies shader-like struct authoring macros for values, retained buffer/texture ranges, nested structs and fixed arrays. Static metadata can enumerate leaves and collect retained Read/Write/ReadWrite dependencies with member paths. Aliasing views, subresource ranges and null optional declarations are preserved. Values may contain normally copied C++ data such as shape vectors; they are not serialized or scanned for hidden pointers. Dispatch determines required inputs and validates actual device/ranges/sharing. The metadata is a foundation for future RDG import/resolve integration, not an implemented graph scheduler or a CUDA argument ABI.

There is no required kernel count, file count, variant registry, tuning callback or selector in the base. Use arbitrary .cpp/.cu/.cuh files and implement tuning/policy inside dispatch. Compile/link CUDA normally or optionally use `FArdaCudaModule` for immutable source/includes/options and successful per-SM PTX caching. Failed compilations are not cached. Create a new module when sources or compilation policy change. The optional `Ardashir::ArdaCudaCompiler` target provides a dynamically loaded NVRTC adapter; set `ARDASHIR_NVRTC_INCLUDE_DIR`, pass an absolute library path to `CreateArdaNvrtcCompiler`, and deploy the matching builtins on the process library search path. This helper compiles individual self-contained units; cross-unit device linking needs an application compiler/linker adapter or ahead-of-time build.

TensorRT and exported PyTorch kernel sequences can use this author-owned interface in future integrations. No such runtime adapter is implemented here, and the public RHI does not convert resources into arbitrary raw CUDA pointers/streams. Direct implementations must supply native allocation/interop, synchronization and lifetime management appropriate to their runtime.

The example is split between the compiled host files `ArdaTestComputeOperand.h/.cpp` and optional runtime-compiled device inputs `ArdaCudaOperand.cu`, `ArdaCudaOperandFinish.cu`, `ArdaCudaOperandMath.cuh` and `ArdaCudaOperandValue.h` in `Source/ArdaBackend/Tests`. `FArdaAddOperand` uses embedded PTX by default, or NVRTC-generated PTX when source modules are supplied. Both paths explicitly record +7 then +11 through `FArdaCudaKernel` descriptors, choosing a block size inside DispatchDeferred from typed shape vectors. The current CXX-only build does not compile these test `.cu` files into CUDA host launch objects. An optional graphics callback implements the same semantics. Immediate Dispatch in this sample chooses to create/submit its own RHI list and exposes its submission ID. Its mutable diagnostics require caller serialization.

`TypedDispatchOwnsPolicyAndMultipleCudaSourceModules` keeps count 128, changes shape [4, 32] to [1, 128], checks blocks 32/128, every output value and exactly two compilations across two source units. It also exercises typed virtual immediate dispatch in place. Other tests prove metadata recursion/alias retention, host-only runtime policy, default deferred rejection without immediate side effects, invalid shapes, compiler-error propagation and graphics alternatives. Errors do not trigger retries after partially recorded writes.

For optional runtime-source tests, put NVRTC/builtins on the process library search path and set `ARDA_TEST_NVRTC_LIBRARY` to its absolute library path. Run `ArdaBackendTests --gtest_filter=ArdaCompute*:ArdaCudaModule.*:ArdaCudaCompiler.*:*ArdaCudaGpu*` in CUDA-on and CUDA-off builds. Missing optional compilation/launch support produces explicit skips; host-only metadata and virtual dispatch tests still execute. The [typed operand guide](cuda-interop.html) contains the current source-backed examples and canonical API links.

## Native execution and lifetime

**D3D12:** dynamically load the CUDA driver and match the adapter LUID/node mask, then query CiG context and stream-capture support. Create a CiG context on the graphics queue. Shared committed allocations are exported as NT resource handles, imported once, and retained with CUDA mappings. The export handle is closed after import; mapped buffers, arrays and surface objects are released before their native allocation. PTX modules are cached per context, with a bounded cache and retention by recorded batches.

CUDA calls are captured into the open D3D12 graphics command list using `cuStreamBeginCaptureToCig` / `cuStreamEndCaptureToCig`. UAV barriers surround the capture. The next graphics/compute pipeline bind restores native state. A CiG context admits one unsubmitted recorded command list at a time; submit or discard it before recording another. Captured lists are single-use. Streams, modules and resources are held through graphics-fence completion. A failed capture poisons its batch; a failure before capture begins may be corrected or submitted as graphics-only work. Normal dispatch adds no device-wide CPU wait.

**Vulkan:** require and enable `VK_NV_cuda_kernel_launch`, its feature and buffer device addresses. Use `VK_NVX_image_view_handle` 64-bit storage-image handles for surfaces. Cache CUDA modules/functions per native device. Record CUDA launches directly in graphics/compute command buffers, with memory barriers between kernels and graphics work, and retain executable/resources with the existing submission recording. Copy queues are rejected. This NVIDIA CUDA extension is provisional; no general-vendor support is implied.

Public validation checks resources, views, launch limits and owned argument construction. D3D12 additionally checks argument count/byte sizes against CUDA function metadata. Vulkan exposes no equivalent metadata query here: its PTX parameter ABI remains the kernel author's responsibility, like a custom graphics shader's binding ABI. A failed selected implementation returns its error; it does not silently retry another implementation after partial recording.

## Historical GPU qualification — 2026-09-07

The following records the original native-queue implementation. Current fallback behavior and qualification are described below; the historical counts are not current test totals.

Machine: Windows, NVIDIA RTX PRO 6000 Blackwell, compute capability 12.0, driver 610.62 / CUDA driver 13.3. Compiled with MSVC Debug, CUDA 13.3.29 headers, the repository's pinned Vulkan headers and D3D12 Agility SDK. Both GPU fixtures enable native validation and require zero error/fatal diagnostics.

- D3D12 CiG and Vulkan native CUDA buffer kernels pass element-by-element CPU oracles, non-multiple work sizes, offset views, guard bytes and ordered in-place updates.
- A graphics shader → CUDA sequence → graphics shader passes on both native APIs. Ordinary buffers select the registered graphics alternative automatically. The same graphics alternative runs in the CUDA-disabled build.
- Vulkan surface tests pass for **24 formats × 5 dimensions = 120 combinations**, writing and reading a nonzero mip and every array layer/depth slice using actual CUDA kernels. Results are checked both through CUDA buffer output and graphics texture readback. An additional 12 allocations cover differing extents, mip counts and render-target flags; a nonuniform 2D kernel independently checks pixel addressing and selected-mip readback.
- Submission retention, CiG ordering, discard and single-use rejection are tested, as are invalid ranges/alignment, oversized launches, binding indices, missing alternatives, duplicate registration, architecture exclusion and invalid tuning selections.
- **D3D12 CiG images are unavailable on this tested driver.** Importing the same shared images succeeds, but `cuExternalMemoryGetMappedMipmappedArray` returns `CUDA_ERROR_UNKNOWN` in a CiG context across 12 allocation combinations. A controlled probe using an ordinary CUDA context successfully mapped those same configurations. The shipping provider probes an image at initialization, records its error, and disables surface admission if mapping fails. D3D12 texture operands can select graphics alternatives. The original implementation did not include ordinary CUDA execution. The current automatic policy now uses it when this probe fails.
- Qualification covers this Windows GPU/driver. Linux, other GPU architectures, future drivers that pass the D3D12 surface probe, arbitrary external CUDA libraries, sampled CUDA textures, and independent CUDA streams are not claimed as tested.

## Build and repeat the checks

`nvcc` is a developer/CI build dependency for ahead-of-time CUDA source compilation, not an end-user runtime requirement. The [compiler lookup guide](cuda-interop.html#nvcc-build) covers optional detection, Ninja/Makefile compiler overrides and Visual Studio toolset selection. The [deployment guide](cuda-interop.html#runtime-deployment) distinguishes linked device code, packaged PTX, runtime libraries and optional NVRTC. The standalone documentation example demonstrates the lookup; the current CXX-only Ardashir provider build still uses explicit SDK header paths and does not perform nvcc discovery. A packaged application must not require nvcc at startup.

For ordinary context execution, install CUDA 12.0 or newer headers (D3D12 CiG capture additionally requires CUDA 13.3+) and configure with `-DARDASHIR_ENABLE_CUDA=ON -DARDASHIR_CUDA_INCLUDE_DIR=<SDK>/include`. A missing SDK produces a configure error; an old SDK produces an explicit compile error. Vulkan native CUDA launch uses Vulkan headers rather than the CUDA runtime API. Set `ARDASHIR_ENABLE_CUDA=OFF` for a build without the CUDA driver implementation. Public headers compile independently with either setting and require no CUDA SDK includes.

This workspace uses isolated `build/cuda-on` and `build/cuda-off` directories, because the older top-level build tree contained mixed generator state. Build `ArdaBackendTests`, `ArdaRHITests`, `ArdaBackendPublicHeaders` and `ArdaRHISourceBoundary`. Run `ArdaBackendTests --gtest_filter=*ArdaCudaGpu*` for CUDA qualification, then the unfiltered binary for regression. Test builds now provision validation layers automatically; see [validation setup and skip behavior](Validation-Tests.md) for local layer overrides and offline builds.

Local XML/log evidence is in `build/cuda-implementation/`: `cuda-gpu.xml`, `texture-matrix.xml`, `cuda-disabled.xml`, `backend-cuda-on.xml`, `backend-cuda-off.xml`, and `rhi-cuda-off.xml`. CUDA-only tests skip explicitly when the relevant execution mode is unavailable; a skip is not GPU qualification. The broader backend suite also has pre-existing hardware capability skips. RDG source, node registration and scheduling were not modified by this change.

Historical native-only qualification counts: the CUDA-focused run passes 13 of 16 tests, with two D3D12 image skips from the failed probe and one CiG-only test skipped on Vulkan. CUDA-disabled qualification passes six tests and skips ten CUDA-only cases. The full CUDA-enabled regression run passes 278 of 306 tests; the final CUDA-disabled regression, including the subsequently added two lifetime cases, passes 277 of 308. Both have zero failures. The enabled aggregate run also declines CUDA on a D3D12 adapter that fails LUID matching; the separate fresh-process CUDA qualification explicitly confirms D3D12 CiG mode and SM 120 before executing its GPU samples. The RHI contract suite passes all 17 tests. Both builds pass public-header isolation and source-boundary checks. PE dependency inspection confirms no mandatory CUDA driver/runtime DLL import in the enabled executable.


## Context-switching fallback — 2026-09-08

`FArdaBackendConfiguration::mCudaExecutionMode` is fixed at device initialization. `Automatic` prefers D3D12 CiG or Vulkan native kernel launch. It uses an ordinary context when the native path is unavailable, or when D3D12's surface-import probe fails. `GraphicsQueue` pins the native path for applications that accept its qualified resource limits. `ContextSwitch` forces separate execution, including on devices that support CiG. The selected `FArdaCudaCapabilities::mLaunchMode`, `mFallbackReason`, surface flags and unavailable reasons are the runtime source of truth.

The driver loader separates baseline entry points from optional CiG functions. CUDA 12.0 headers compile the ordinary path; 13.3 headers compile both. Missing `cuFuncGetParamInfo` disables only argument-metadata verification, so PTX authors must honor their declared parameter ABI on older drivers. Public headers contain no CUDA or Vulkan SDK types. There is no mandatory CUDA DLL/shared-library import.

Recording validates and retains PTX, scalar arguments, mappings and native resources. Each `DispatchCuda` closes the preceding graphics segment and records a deferred CUDA batch. Submission drains preceding graphics work, submits and waits for each graphics segment, launches CUDA and synchronizes that stream, then submits the next graphics segment. This conservative CPU synchronization intentionally trades submission latency and queue overlap for compatibility. The final graphics segment uses the ordinary RHI completion fence. Both providers restore graphics/compute bindings after a split. Context-switching lists are single-use until reset, and discarding an unsubmitted list executes no kernels. Independent lists can be recorded before either is submitted.

Vulkan uses dedicated allocations exported through `VK_KHR_external_memory_win32` or `VK_KHR_external_memory_fd` and matches CUDA by physical-device UUID. Buffers receive CUDA mappings rather than reinterpreted Vulkan device addresses. Image layouts are GENERAL during external access; release/acquire barriers transfer each declared resource to/from `VK_QUEUE_FAMILY_EXTERNAL`. D3D12 matches LUID/node identity and shares committed resources. Mapping destruction precedes native allocation destruction; CUDA context pushes restore the caller's thread-local context.

On the qualification machine, D3D12 ordinary-context surface reads/writes pass all 24 formats and five dimensions. Vulkan ordinary-context buffers and 1D/2D/3D surfaces pass; layered Vulkan surfaces fail the cross-API layer-stride oracle despite successful mapping and CUDA-local readback. Consequently `mbLayeredSurfaceAccess` is false for Vulkan ContextSwitch and those allocations return Unsupported. Native Vulkan kernels retain their qualified layered-surface support. Applications can use ordinary graphics textures and an explicit graphics operand alternative for this case.

Research used NVIDIA's [CUDA 12.0 external-resource driver contract](https://docs.nvidia.com/cuda/archive/12.0.0/cuda-driver-api/group__CUDA__EXTRES__INTEROP.html), [context creation contract](https://docs.nvidia.com/cuda/cuda-driver-api/group__CUDA__CTX.html), [simpleD3D12 sample](https://github.com/NVIDIA/cuda-samples/blob/master/cpp/5_Domain_Specific/simpleD3D12/simpleD3D12.cpp), [simpleVulkan sample](https://github.com/NVIDIA/cuda-samples/blob/master/Samples/5_Domain_Specific/simpleVulkan/main.cpp), and Khronos's [device creation contract](https://docs.vulkan.org/refpages/latest/refpages/source/VkDeviceCreateInfo.html). NVIDIA's samples demonstrate imported resources and explicit synchronization. This implementation uses CPU waits at each fallback boundary to enforce separate execution across the supplied graphics queues.

### Fallback revision verification

The final CUDA 13.3-header focused run executes 48 cases: 40 pass, eight explicitly skip for unavailable or inapplicable modes, and none fail. It includes real host-owned Vulkan adoption, malformed descriptor cleanup, host lifetime retention, forced context-switching on both providers, mixed graphics/CUDA readback, discarded recordings and replay rejection. Complete CUDA-enabled and CUDA-disabled CTest suites each contain 423 cases and report zero failures, with hardware capability skips.

A full build against official CUDA 12.0.107 headers also passes the available focused tests: 37 pass, 11 skip, zero fail. These runs use the installed CUDA 13.3-capable driver, not an older driver installation. CUDA 13.3 headers were restored after this compatibility check. Reproduce with the commands above; local evidence is in `build/final-gpu.xml`, `build/cuda12-gpu.xml`, `build/full-revisions.log` and `build/cuda-off-full-revisions.log`.
