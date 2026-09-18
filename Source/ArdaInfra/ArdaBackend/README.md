# ArdaBackend modules

Include `ArdaBackend.h` for the complete public API, or include a module header
directly when a narrower dependency is sufficient. Public declarations live in
`Public`; implementations and internal contexts live in the matching `Private`
directories. Native Vulkan, D3D12 and CUDA providers remain separate linked
targets in `ArdaBackendImpls` and implement the contracts in `RHI/Providers`.

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
| `RHI/Config` | Backend/device configuration, feature requirements and capability hierarchy, CUDA execution policy and result/diagnostic types. |
| `FileOperations` | Shared path checks, binary/text reads, temporary-file lifetime, atomic replacement and multi-file cache publication. |

The legacy `RHI/ArdaRHI*.h`, `ShaderStructs`, `Compute`, `Allocator`,
`PipelineStateCache` and root interop/provider/presentation headers forward to
the canonical modules for existing consumers. New code should use the module
paths. Resource and type umbrellas aggregate category headers; they no longer
own all category definitions. Public headers must compile independently, without
private implementation or native provider include directories.

`ArdaBackendPublicHeaders` checks every public C++ header in isolation, including
compatibility headers. `ArdaBackendTests`, `ArdaRHITests` and `ArdaGraphTests`
exercise backend behavior and downstream integration. CUDA registration's
`ArdaCudaKernelBinding.cuh` is intended for nvcc and is exercised by CUDA kernel
targets rather than the host-only header checks.
