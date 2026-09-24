# ArdaBackend modules

Include `ArdaBackend.h` for the complete public API, or include an owning module's
header directly when a narrower dependency is sufficient. `Public` contains only
`ArdaBackend.h`, `RHI`, and `FileOperations`. Each RHI header lives in the category
that owns its declarations. `Private` mirrors those categories for implementation
files and internal state; tests and their shader fixtures live in `Tests`.

Native Vulkan, D3D12 and CUDA implementations remain separate linked targets in
`ArdaBackendImpls` and implement the contracts in `RHI/Providers`. Public contracts
do not include native provider SDKs or private implementation headers.

| Directory | Responsibility |
| --- | --- |
| `RHI/Context` | Backend and registry state, resource lifetime tracking, shader compiler/registry/directory state, and CUDA execution contexts. Each context has its own header. Internal process state remains private. |
| `RHI/Device` | Adapter identity, backend lifecycle, device interface and facade implementation, device diagnostics. |
| `RHI/Providers` | Backend module, provider device, command list and translated provider object contracts; module registration. |
| `RHI/Resources` | Abstract resource and reference ownership, buffer/texture/view/sampler/framebuffer/acceleration structure categories and resource creation. |
| `RHI/CUDA` | Kernel variants and registration, launch parameters, external call preparation and CUDA command recording. |
| `RHI/Pipelines` | Individual graphics, compute, meshlet, ray tracing and work graph pipelines; fixed function states, resolution and persistent pipeline caches. |
| `RHI/Shaders` | Shader types and parameter structures, compiler invocation, registered shader maps/artifacts, binding layouts/sets, bindless descriptor tables and shader tables. |
| `RHI/Memory` | GPU allocation, heaps, memory requirements/planning, residency and sparse tiling. |
| `RHI/Interop` | Opaque native handles, external device/resource descriptions, resource imports and CUDA resource mappings. |
| `RHI/Scheduling` | Command lists, resource transitions and copies, queues, fences, queries, CUDA batches/semaphores/graphs/timing/sequences and presentation. |
| `RHI/Config` | Backend/device configuration, feature requirements and capability hierarchy, CUDA execution policy, result types, assertions and logging. |
| `FileOperations` | Shared path checks, binary/text reads, temporary-file lifetime, atomic replacement and multi-file cache publication. |

Use category paths such as `RHI/Resources/ArdaRHIBuffer.h`,
`RHI/Shaders/ArdaRHIBindingLayout.h`, and `RHI/Scheduling/ArdaRHICommandList.h`.
There are no forwarding headers at the root of `RHI` or former category paths.
The abstract resource contract is defined in `RHI/Resources/ArdaRHIResource.h`;
individual resource categories own their descriptions and interfaces.

Contexts own state and lifetime. Devices coordinate the facade and provider;
resource, shader, pipeline, memory, interop and scheduling implementations own
their corresponding operations. CUDA launch setup belongs to `RHI/CUDA`, with
its execution contexts in `RHI/Context` and ordering, graph and timing operations
in `RHI/Scheduling`. Shared filesystem operations belong to `FileOperations`.

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

## Optional compute alternatives

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
