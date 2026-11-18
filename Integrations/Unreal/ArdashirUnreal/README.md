# ArdashirUnreal

Unreal Engine integration for Ardashir. The plugin registers the `unreal-rhi`
backend and runs Arda compute work on Unreal's existing device and immediate
graphics command list. The initial integration targets Windows x64 and the
Unreal 5.8.1 source checkout at `D:/UnrealEngine`.

## Layout and dependency ownership

| Location | Responsibility |
| --- | --- |
| `Source/ArdaUnrealRuntime` | Backend registration, device lifecycle, and Unreal RHI adapter |
| `Source/ArdaUnrealRuntime/Private/BackendImpl` | Unreal RHI provider implementation |
| `Source/ArdaUnrealRuntime/Public/BackendImpl` | Host command scope and Unreal object import APIs |
| `Source/ArdaUnrealEditor` | Editor module reserved for future tools and configuration |
| `Source/ArdaHeaders` | Shared Arda headers and ABI definitions |
| `Source/ArdaExternal` | Import library and runtime deployment for the shared Arda library |
| `ThirdParty/Ardashir` | Separate, ignored upstream Git checkout |
| `Intermediate/Ardashir` | Out-of-source dependency build and generated artifacts |
| `Tests/ProjectTemplate` | Versioned fixture sources installed into an external Unreal test project |

The dependency is a Git checkout, not a source copy. Do not edit it. Library
changes needed for Unreal must be made in the Ardashir development repository,
validated, committed, pushed upstream, and then pulled here. See the mandatory
[coding conventions](../../../Conventions/ArdaCodingConventions.md#game-engine-integrations).
The CMake wrapper builds `ArdaUnrealCore.dll` from the dependency's own source
lists. Runtime and project modules use this same DLL, keeping the backend and
node registries shared. Project code can register nodes and call the normal
Arda dependency graph API. Do not additionally link static Arda libraries into
an Unreal module, which would create a second set of registries.

Project-owned node callables work across this DLL boundary. For built-in core
nodes, use facade functions such as `AllocateBuffer`, or `NodeType::Invoke`.
Importing the core's global callable objects as data is not covered by the
current Windows export annotations.

## Build

From the Ardashir development repository:

```powershell
python Integrations/Unreal/ArdashirUnreal/Scripts/ArdaBuild.py
```

The script clones the upstream repository when needed, checks that the checkout
is clean, and builds the library with native D3D12 and Vulkan providers
disabled. The default build uses the revision in `ArdaRevision.json`.
`--update` advances to the latest published commit on the configured `main` branch; `--ref COMMIT` selects
a particular published revision. Unreal remains the owner of the graphics API.
For a local dependency download cache, add
`--dependency-cache D:/Projects/Ardashader/build/_deps`.

The library uses the release MSVC runtime and a matching EASTL configuration.
The consuming Unreal modules enable exceptions and keep Unreal's RTTI disabled.
The shared library enables RTTI for Arda's internal resource casts. Both sides
use the same EASTL configuration, iterator debug level, and Eigen alignment.
Build.cs reports missing library artifacts with the bootstrap command; it does
not modify the dependency repository during an Unreal build.

Modules that call the Arda graph/library API must link the core explicitly:

```csharp
PrivateDependencyModuleNames.AddRange(new[] {
    "ArdaUnrealRuntime", "ArdaExternal", "ArdaHeaders"
});
CppStandard = CppStandardVersion.Cpp20;
bEnableExceptions = true;
bUseRTTI = false;
```

Use public dependencies when your public headers expose these APIs. Unreal does
not carry the runtime module's external import library across another module's
DLL boundary automatically.

Enable `ArdashirUnreal` in a project whose `AdditionalPluginDirectories` contains
the plugin's parent directory, or place the complete plugin in the project's
`Plugins` directory. The test project lives under `D:/UnrealProjects`, outside the
library repository.

## Recording and ownership

Call `arda::InitializeUnrealBackend()` on the render thread after RHI startup and
obtain the device through `arda::GetUnrealBackendDevice()`. Shader compilation
and cooking remain Unreal responsibilities: `ImportUnrealComputeShader` imports
a compiled Unreal compute shader into an Arda shader object. Resource binding
indices come from Unreal's reflected parameter map.

Inside an immediate graphics RDG pass, create an `FArdaUnrealRHIScope` around its
`FRHICommandListImmediate`. Submit an Arda graph on that thread,
or open, record, close, and execute an Arda command list before leaving the scope.
Deferred command lists replay onto the borrowed Unreal list at execution;
immediate lists append during recording. Normal recording does not flush the
queue or wait for the GPU. Unreal calls can occur between Arda operations, with
compute pipeline and shader parameters rebound by each caller.

```text
Unreal RDG pass / immediate graphics list
    Unreal commands
    FArdaUnrealRHIScope
        Arda facade -> unreal-rhi provider -> same Unreal command list
    Unreal commands
Unreal submits and retires the command list
```

RDG cannot discover resource references hidden inside Arda. Declare every
resource access in the enclosing RDG pass. Import the host texture with its
current state and synchronize Arda's tracking again after an Unreal-side state
change. The provider retains imported RHI objects; the host must preserve the
RDG allocation and its ownership contract until the pass has completed.
Immediate recording has side effects when commands are recorded; a failed
later call does not roll earlier commands back.

The first provider supports compute, deferred graph command recording, imported
and ordinary 2D single-sample color textures, whole-resource transitions, and
qualified copies. It rejects independent queues, owned devices, presentation,
and unimplemented graphics/ray-tracing features. sRGB imports, partial texture
state tracking, CPU mapping, and shader buffer bindings are not qualified.
Capability flags describe the adapter implementation, not everything the
underlying GPU supports.

## CUDA handoff

CUDA uses a separate execution context and stream. The portable Unreal RHI does
not expose a CUDA stream or shared external semaphores, and direct CUDA calls
cannot be inserted as ordinary Unreal compute dispatches. The D3D12 integration
therefore uses an explicit handoff on shared resources and fences:

```text
Unreal / Arda compute -> release shared resource -> signal graphics fence
CUDA stream          -> wait graphics fence -> CUDA kernel -> signal CUDA fence
Unreal graphics      -> wait CUDA fence -> reacquire resource -> consume result
```

The ordering must include Unreal's RHI submission thread; obtaining a native
queue pointer alone does not order work against deferred Unreal commands.
CUDA context selection, shared resource lifetime, and both fence directions
belong to the handoff implementation. Any CPU waits used by test readback or
shutdown are separate from the frame's GPU dependency chain.

The CUDA fixture is specific to NVIDIA on Windows D3D12. The runtime exposes
`IArdaUnrealCudaInterop` for a host bridge installed before backend initialization.
Arda's normal CUDA node/sequence dispatch reaches that bridge, which receives the
actual compiled kernel entries, binding table, and parameter patches. Only
resources with existing, qualified mappings advertise CUDA sharing. Porting the
handoff to another engine RHI needs that API's external-memory and semaphore
implementation and separate validation.

## Validation

Nodes can query the active Unreal provider before invocation with
`FArdaMyNode::CheckRequiredFeatures(Device, Parameters)`. The returned
`FArdaRHIStatus` contains `mCode` and a diagnostic `mMessage`; graph compilation
also checks every node's requirements, including cached compilations. Declare
portable features and environment predicates through `GetRequirements`.

A CUDA node can optionally implement `RecordComputeAlternative` and declare
`GetComputeAlternativeRequirements`. When the provider has no qualified CUDA
bridge, Arda selects that compute route before preparation and schedules it as
ordinary compute work. Without CUDA or an authored alternative, invocation
returns `Unsupported`. The alternative must use shader operations supported by
this provider. See the dependency's `Docs/ArdaRDG/feature-requirements.html` and
`Source/ArdaInfra/ArdaBackend/README.md` for the graph and operand contracts.

The external test project contains a view extension and two registered Arda
nodes: a compute shader node and a CUDA node. Project code imports the scene and
shared textures, invokes the nodes, keeps the output, compiles the graph, and
submits it inside the Unreal pass. GPU validation checks actual readback values
after the graphics/Arda/CUDA handoffs. `Ardashir.Unreal.ProviderContract` checks
scope ownership, deferred replay order, duplicate execution, and rejected calls.

Unreal's shader parameter metadata owns its loose constant layout. The compute
node uses a deferred host callback to bind those parameters between Arda's
pipeline binding and dispatch; Arda still schedules the operation and declares
its texture dependencies. Submit from the render-thread scope so native recording runs in that scope. Use `Submit` and poll completion; `Execute` waits for
completion and is unsuitable inside the scope.

See `VALIDATION.md` for the exact environment, commands, results, and remaining
limitations of the latest recorded run.
