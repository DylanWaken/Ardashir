# CUDA backend integration plan

Scope: backend contracts, native providers, reusable compute operands, and GPU tests. RDG registration and scheduling are a subsequent change.

## Design

1. Add opt-in shareable buffer/texture allocation. Query actual support using the device, descriptor, and CUDA execution mode. Represent buffers as linear ranges and textures as arrays/surfaces. Graphics acceleration structures are opaque implementation data, not CUDA/OptiX traversable handles; reject them explicitly and allow their ordinary source geometry buffers instead.
2. Keep representation support, mapped representations, and access ownership separate. A resource can have both graphics and CUDA views without permitting simultaneous writes. Native resources own mappings and their lifetime; callers cannot mutate tracker flags. Typed bindings retain the underlying RHI resource.
3. Add `FArdaComputeOperand`, inherited by custom operators, with one input/output binding contract, one or more named kernel sequences, architecture/launch requirements, deterministic selection and optional custom tuning policy, and registered graphics compute alternatives. Validate the whole dispatch before recording; do not execute candidates on live inputs while tuning.
4. Add optional native CUDA implementations with SDK-free public headers and runtime feature probing. Providers own allocation export, CUDA context identity, mapping, synchronization and launch. Use D3D12 CiG command-list capture and Vulkan native CUDA kernel launch where qualified. Keep an independent external-memory/CUDA-stream executor outside this change: it needs a different submission/ownership protocol. Never infer support from vendor name alone.
5. Integrate dispatch with existing RHI command recording/submission and retention. Preserve resource state validation and explicit asynchronous completion. Serialize work where native CiG ordering requires it. No hidden device-wide CPU waits in normal dispatch.
6. Add real GPU samples and contract tests: buffer kernels, multiple kernels, image writes/readback, graphics/CUDA handoffs, invalid bindings and architectures, resource lifetime, missing CUDA/fallback selection, and clean CUDA-off compilation. Run the existing backend regression suite after integration. Record actual tested paths separately from unavailable hardware/SDK paths.

## Acceptance

- Users derive one operand class, declare bindings once, register implementations, and dispatch through the backend.
- Shared data is checked against a CPU oracle after actual GPU execution; registration alone is not a passing GPU test.
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

These immutable properties are derived from native allocation/mapping state. They are not caller-controlled ownership flags. Both implemented launch paths run inside graphics API command streams, so there is no CUDA-only queue-owner state to toggle. Existing RHI state tracking and submission fences remain the source of truth for access ordering. Having both representations does not permit unsynchronized concurrent writes.

Buffers expose ranges to kernels, with the offset incorporated into the native address. Textures expose a surface for a selected mip, including its array layers or 3D depth. Neither leaks a native address through the public API. A Vulkan buffer device address is valid for `VK_NV_cuda_kernel_launch`; it is not offered as an ordinary CUDA-runtime pointer.

Supported texture storage comprises 24 raw integer/floating-point R, RG and RGBA formats, across 1D, 1D array, 2D, 2D array and 3D images. One format table supplies CUDA channel width/type/count and admission. Normalized, sRGB, BGRA, packed, compressed, depth/stencil, cube and multisample images are rejected. The API supplies surface reads/writes, not sampled texture objects or filtering. Packing into linear tensor storage, swizzling and color conversion are explicit operator work; they do not happen during binding.

Acceleration structures, opacity micromaps, sampler-feedback objects, samplers and pipelines retain `None`. A graphics AS address is not a CUDA/OptiX traversal handle. Ordinary vertex/index/instance source buffers can be shared. AS-storage buffers, CPU-visible, versioned, sparse and placed allocations are rejected. Native resource imports cannot opt into sharing without backend-created allocation/mapping ownership.

## Registering a custom operand

Inherit `FArdaComputeOperand` and pass a stable name plus ordered ports to the base constructor. A port declares buffer/surface type, read/write access, minimum buffer bytes and alignment; surface ports may constrain texture format and dimension. This is one contract shared by every implementation.

Register named CUDA alternatives with `RegisterCuda(name, minimumSM, maximumSM, builder)`. Architectures use `major * 10 + minor`, so Blackwell SM 12.0 is `120`. The builder receives the validated invocation and returns one or more ordered `FArdaCudaKernel` descriptions: PTX, entry point, grid/block dimensions, dynamic shared bytes and arguments. `FArdaCudaArgument::Binding(index)` selects a declared resource view; `Value<T>(value)` copies a trivially-copyable parameter into owned storage. No NVRTC or runtime CUDA-C compilation dependency is imposed; callers can supply precompiled PTX.

Register graphics alternatives with `RegisterGraphics(name, callback)`. The callback records ordinary RHI compute-shader work and returns its status. It is responsible for shader-specific transitions, pipeline and descriptor bindings. Override `ValidateInvocation` for operation-specific shapes, parameter schemas, matching input/output sizes and dtype rules before either implementation runs. General port checks do not prove that arbitrary user kernel code respects its buffers.

Call `Dispatch(commands, invocation, policy, optionalVariantName)` on an open command list. `Auto` selects the first eligible registered implementation. Register preferred CUDA variants before graphics fallback variants. `RequireCuda` and `RequireGraphics` enforce the requested implementation family. Architecture-ineligible CUDA variants, unsupported CUDA queues and variants requiring unavailable resource representations are excluded before their builders execute. A CUDA-only operand without an eligible implementation returns `Unsupported`.

Override `SelectVariant(invocation, capabilities, eligibleIndices)` to use an external tuning cache; the default is deterministic registration order. The selected index is checked against eligibility. Benchmarking candidates on live inputs is deliberately absent: tuning should use scratch resources, then select a named variant using workload/device information. The callback API permits multi-kernel operands without creating a second node registry inside the backend. RDG can consume this later.

The compiled examples in `Source/ArdaBackend/Tests/ArdaCudaTests.cpp` are the usage source of truth: `FAddOperand` registers two ordered CUDA kernels with an HLSL compute alternative; `FSelectionOperand` demonstrates custom selection and registration rejection. `ArdaCudaFallback.hlsl` implements the same add operation for both native graphics providers.

## Native execution and lifetime

**D3D12:** dynamically load the CUDA driver and match the adapter LUID/node mask, then query CiG context and stream-capture support. Create a CiG context on the graphics queue. Shared committed allocations are exported as NT resource handles, imported once, and retained with CUDA mappings. The export handle is closed after import; mapped buffers, arrays and surface objects are released before their native allocation. PTX modules are cached per context, with a bounded cache and retention by recorded batches.

CUDA calls are captured into the open D3D12 graphics command list using `cuStreamBeginCaptureToCig` / `cuStreamEndCaptureToCig`. UAV barriers surround the capture. The next graphics/compute pipeline bind restores native state. A CiG context admits one unsubmitted recorded command list at a time; submit or discard it before recording another. Captured lists are single-use. Streams, modules and resources are held through graphics-fence completion. A failed capture poisons its batch; a failure before capture begins may be corrected or submitted as graphics-only work. Normal dispatch adds no device-wide CPU wait.

**Vulkan:** require and enable `VK_NV_cuda_kernel_launch`, its feature and buffer device addresses. Use `VK_NVX_image_view_handle` 64-bit storage-image handles for surfaces. Cache CUDA modules/functions per native device. Record CUDA launches directly in graphics/compute command buffers, with memory barriers between kernels and graphics work, and retain executable/resources with the existing submission recording. Copy queues are rejected. This NVIDIA CUDA extension is provisional; no general-vendor support is implied.

Public validation checks resources, views, launch limits and owned argument construction. D3D12 additionally checks argument count/byte sizes against CUDA function metadata. Vulkan exposes no equivalent metadata query here: its PTX parameter ABI remains the kernel author's responsibility, like a custom graphics shader's binding ABI. A failed selected implementation returns its error; it does not silently retry another implementation after partial recording.

## GPU qualification and limitations — 2026-09-07

Machine: Windows, NVIDIA RTX PRO 6000 Blackwell, compute capability 12.0, driver 610.62 / CUDA driver 13.3. Compiled with MSVC Debug, CUDA 13.3.29 headers, the repository's pinned Vulkan headers and D3D12 Agility SDK. Both GPU fixtures enable native validation and require zero error/fatal diagnostics.

- D3D12 CiG and Vulkan native CUDA buffer kernels pass element-by-element CPU oracles, non-multiple work sizes, offset views, guard bytes and ordered in-place updates.
- A graphics shader → CUDA sequence → graphics shader passes on both native APIs. Ordinary buffers select the registered graphics alternative automatically. The same graphics alternative runs in the CUDA-disabled build.
- Vulkan surface tests pass for **24 formats × 5 dimensions = 120 combinations**, writing and reading a nonzero mip and every array layer/depth slice using actual CUDA kernels. Results are checked both through CUDA buffer output and graphics texture readback. An additional 12 allocations cover differing extents, mip counts and render-target flags; a nonuniform 2D kernel independently checks pixel addressing and selected-mip readback.
- Submission retention, CiG ordering, discard and single-use rejection are tested, as are invalid ranges/alignment, oversized launches, binding indices, missing alternatives, duplicate registration, architecture exclusion and invalid tuning selections.
- **D3D12 CiG images are unavailable on this tested driver.** Importing the same shared images succeeds, but `cuExternalMemoryGetMappedMipmappedArray` returns `CUDA_ERROR_UNKNOWN` in a CiG context across 12 allocation combinations. A controlled probe using an ordinary CUDA context successfully mapped those same configurations. The shipping provider probes an image at initialization, records its error, and disables surface admission if mapping fails. D3D12 texture operands can select graphics alternatives. Ordinary CUDA-stream image execution has not been added, and the ordinary-context diagnostic was removed from shipping code.
- Qualification covers this Windows GPU/driver. Linux, other GPU architectures, future drivers that pass the D3D12 surface probe, arbitrary external CUDA libraries, sampled CUDA textures, and independent CUDA streams are not claimed as tested.

## Build and repeat the checks

For D3D12 CiG, install CUDA 13.3 or newer headers and configure with `-DARDASHIR_ENABLE_CUDA=ON -DARDASHIR_CUDA_INCLUDE_DIR=<SDK>/include`. A missing SDK produces a configure error; an old SDK produces an explicit compile error. Vulkan native CUDA launch uses Vulkan headers rather than the CUDA runtime API. Set `ARDASHIR_ENABLE_CUDA=OFF` for a build without the CUDA driver implementation. Public headers compile independently with either setting and require no CUDA SDK includes.

This workspace uses isolated `build/cuda-on` and `build/cuda-off` directories, because the older top-level build tree contained mixed generator state. Build `ArdaBackendTests`, `ArdaRHITests`, `ArdaBackendPublicHeaders` and `ArdaRHISourceBoundary`. Run `ArdaBackendTests --gtest_filter=*ArdaCudaGpu*` for CUDA qualification, then the unfiltered binary for regression. Test builds now provision validation layers automatically; see [validation setup and skip behavior](Validation-Tests.md) for local layer overrides and offline builds.

Local XML/log evidence is in `build/cuda-implementation/`: `cuda-gpu.xml`, `texture-matrix.xml`, `cuda-disabled.xml`, `backend-cuda-on.xml`, `backend-cuda-off.xml`, and `rhi-cuda-off.xml`. CUDA-only tests skip explicitly when the relevant execution mode is unavailable; a skip is not GPU qualification. The broader backend suite also has pre-existing hardware capability skips. RDG source, node registration and scheduling were not modified by this change.

Final qualification counts: the CUDA-focused run passes 13 of 16 tests, with two D3D12 image skips from the failed probe and one CiG-only test skipped on Vulkan. CUDA-disabled qualification passes six tests and skips ten CUDA-only cases. The full CUDA-enabled regression run passes 278 of 306 tests; the final CUDA-disabled regression, including the subsequently added two lifetime cases, passes 277 of 308. Both have zero failures. The enabled aggregate run also declines CUDA on a D3D12 adapter that fails LUID matching; the separate fresh-process CUDA qualification explicitly confirms D3D12 CiG mode and SM 120 before executing its GPU samples. The RHI contract suite passes all 17 tests. Both builds pass public-header isolation and source-boundary checks. PE dependency inspection confirms no mandatory CUDA driver/runtime DLL import in the enabled executable.
