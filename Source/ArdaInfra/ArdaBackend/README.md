# ArdaBackend

ArdaBackend defines the rendering hardware interface used to create GPU
resources, record commands, and submit work. Start with the
[backend setup guide](../../../Docs/ArdaBackend/getting-started.html) to create a
device. Include `ArdaBackend.h` for the facade API and the headers for the
provider features you use.

`Public/ArdaBackend.h` declares process configuration, backend and provider
registration, adapter and device selection, initialization, presentation startup,
and shutdown. The lifecycle implementation lives in `Private/ArdaBackend.cpp`.

`Public` contains `ArdaBackend.h`, `RHI`, `NativeCompute`, and `FileOperations`.
`Private` mirrors the public categories for implementation and internal state.
Find backend tests and their shader fixtures in `Tests`.

Vulkan and D3D12 graphics modules and CUDA, CiG, and NVAPI compute providers are
implemented in `ArdaBackendImpls`. Graphics modules implement the contracts in
`RHI/Providers`. Native compute providers implement `IArdaNativeComputeProvider`
and register with the facade in `NativeCompute/ArdaNativeCompute.h`.

| Directory | Responsibility |
| --- | --- |
| `ArdaBackend.h` and `Private/ArdaBackend.cpp` | Process lifecycle, backend and external-provider registration, configuration, device selection and presentation startup. |
| `RHI/Context` | Resource lifetime tracking, shader compiler, pipeline cache and allocator contexts. |
| `RHI/Device` | Adapter identities, device interfaces and implementation, and device diagnostics. |
| `RHI/Providers` | Module, provider device, command list and translated object contracts. |
| `RHI/Resources` | Abstract resource and reference ownership, buffer/texture/view/sampler/framebuffer/acceleration structure categories and resource creation. |
| `RHI/Pipelines` | Individual graphics, compute, meshlet, ray tracing and work graph pipelines; fixed function states, resolution and persistent pipeline caches. |
| `RHI/Shaders` | Shader types and parameter structures, compiler invocation, registered shader maps/artifacts, binding layouts/sets, bindless descriptor tables and shader tables. |
| `RHI/Memory` | GPU allocation, heaps, memory requirements/planning, residency and sparse tiling. |
| `RHI/Interop` | Opaque native handles, external device and resource descriptions, and resource imports. |
| `RHI/Scheduling` | Command lists, resource transitions and copies, queues, fences, queries and presentation. |
| `RHI/Config` | Device configuration, feature requirements and capability hierarchy, result types, assertions and logging. |
| `NativeCompute` | The operand contract shared by native provider variants and shader fallbacks. |
| `FileOperations` | Shared path checks, binary/text reads, temporary-file lifetime, atomic replacement and multi-file cache publication. |

Use category paths such as `RHI/Resources/ArdaRHIBuffer.h`,
`RHI/Shaders/ArdaRHIBindingLayout.h`, and `RHI/Scheduling/ArdaRHICommandList.h`.
There are no forwarding headers at the root of `RHI` or former category paths.
The abstract resource contract is defined in `RHI/Resources/ArdaRHIResource.h`;
individual resource categories own their descriptions and interfaces.

Register a `FArdaNativeOperand` with ordered native variants and shader fallbacks
that share one resource and parameter contract. The facade qualifies providers
per device and resolves kernel support before preparing an invocation. The operand's
boundary hooks prepare any graphics-to-native and native-to-graphics conversions
around the dispatch. Use `UseArdaNativeDirectResources` to bind shared views directly. Native
kernel libraries use stable keys to resolve provider-specific artifacts. See the
[native compute provider guide](../../../Docs/ArdaNativeCompute/providers.html)
for module registration and kernel library deployment.

The [CUDA public headers](../ArdaBackendImpls/Cuda/Public) define the typed kernel
and external call APIs. The CUDA and [NVAPI](../ArdaBackendImpls/NVAPI/Public)
modules also own their typed RHI compatibility headers under their public
`RHI` directories. Include `CUDA/ArdaCudaProviderCommands.h` when implementing the
typed CUDA recording entry point in a graphics backend. CUDA device contexts,
command recording, timing and sequences are implemented in `ArdaBackendImpls/Cuda`. See
[CUDA sources](../../../Docs/ArdaRDG/native-compute.html#sources) for loading PTX
or cubin and compiling CUDA C++ with NVRTC during preparation.

Public headers must compile independently, without private implementation or
native provider include directories. Add dependencies to the header that uses
them rather than relying on `ArdaBackend.h` to supply transitive declarations.

Use EASTL for backend containers, strings, ownership and utility types where it
provides the required operation. Keep standard filesystem, stream and operating
system synchronization facilities at those integration boundaries.

`ArdaBackendPublicHeaders` checks every public C++ header in isolation.
`ArdaBackendPrivateHeaders` independently checks internal headers with their
declared module dependencies.
`ArdaBackendTests`, `ArdaRHITests` and `ArdaGraphTests`
exercise backend behavior and downstream integration. CUDA registration's
`ArdaCudaKernelBinding.cuh` is intended for nvcc and is exercised by CUDA kernel
targets rather than the host-only header checks.

## Typed CUDA compute alternatives

`TArdaComputeOperand<Parameters, Payload>` can supply an ordinary compute shader
implementation through three public hooks:

- `HasComputeAlternative()` declares that the implementation exists.
- `CheckComputeAlternativeSupport()` checks its device feature requirements,
  without creating resources, recording commands, or submitting work.
- `PrepareComputeAlternative(Parameters, Queue)` returns
  `TArdaRHIResult<FArdaComputeAlternativePlan>` containing the retained compute
  pipeline and binding sets, owned push constant bytes, and work-group counts.
  It may create bindings but must not record or submit work. Set `mbNoWork` only
  when the operand's mathematical contract permits an empty operation.

`CheckRequiredFeatures()` is callable through `FArdaComputeOperand` and validates
the schema, variant registry, and at least one supplied implementation.
`GetOperandSupport()` has the same behavior. `SelectDispatchRoute(Queue)` exposes
the chosen `EArdaComputeDispatchRoute` before allocating graph resources.
Compatible CUDA is preferred; an `Unsupported` CUDA feature/coverage result
selects a supplied, supported compute alternative. Malformed schemas and variant
bindings remain errors. Without either implementation, preflight returns an
error. An alternative permits an empty CUDA registry, so the same operand can
build with `ARDASHIR_ENABLE_CUDA=OFF` and `ARDASHIR_BUILD_CUDA_KERNELS=OFF`.

The author maps the host schema explicitly to shader bindings and constants;
CUDA argument bytes are not a shader constant layout. Ordinary graphics buffers
need no CUDA sharing on the compute route. `PrepareDispatch` freezes the chosen
route in `FArdaCudaDispatchPlan::mRoute` and retains the alternative state in
`mComputeAlternative`. Both immediate and deferred dispatch use that frozen
plan. Submission reports the same route in `FArdaCudaSubmission::mRoute`.
Resource validation, preparation, recording, and execution failures never
trigger another implementation or retry partially written output.

`FArdaCudaSequence` remains CUDA-only and explicitly rejects alternative plans,
including empty ones. Graph schedulers must lower the chosen route to ordinary
compute work before CUDA batching. Command-list errors from void RHI methods
remain observable at `Close()`, as for other RHI compute recording.

`Tests/ArdaComputeAlternativeTests.cpp` demonstrates frozen resources/constants,
immediate and deferred compute dispatch, preflight failures, native preference,
and CUDA sequence rejection. Its compute output checks run on Vulkan and D3D12
with CUDA providers and native kernels disabled.
