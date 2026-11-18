# Unreal graph and CUDA integration fixture

The maintained sources are in `ProjectTemplate`. `ArdaSetupUnrealTest.ps1` copies
them into `D:/UnrealProjects/ArdashirUnrealTest`, adds the plugin directory to the
project descriptor, and compiles the CUDA kernel as native `sm_120` CUBIN. The
default target matches the RTX PRO 6000 Blackwell in this development machine.
Select `-CudaArchitecture sm_XX` for another supported NVIDIA architecture.
The script discovers MSVC through `vswhere` when `cl.exe` is not on `PATH`.

Build the plugin's shared Arda library first using `../Scripts/ArdaBuild.py`.
Then run from PowerShell:

```powershell
./ArdaSetupUnrealTest.ps1 -Build
./ArdaRunUnrealTest.ps1 -Contracts
./ArdaRunUnrealTest.ps1
```

All scripts accept `-ProjectRoot` and `-EngineRoot`. The setup script refreshes
the fixture sources/configuration; author changes in this template before
refreshing the external project. It does not modify the engine or vendored Arda.
The scripts fail on compiler errors, process failure, timeout, missing results,
or failed assertions. Run them from an environment permitted to write both the
engine's build outputs and the external project directory.

The ordinary test launches a real offscreen game viewport in
`/Engine/Maps/Templates/Template_Default`. A scene view extension subscribes to
the post-tonemap callback. Open the project in Unreal Editor for an interactive
view; `r.Arda.PostProcess 0` disables the effect, and `1` enables it.

## Graph authoring and validation

`ArdaUnrealGraphNodes` registers two ordinary `TArdaNodeDefinition` classes with
`ARDA_DEFINE_GRAPH_NODE`:

- `unreal-test.compute-postprocess` declares a scene-color input and shared UAV
  output, binds an imported Unreal compute shader through Arda, and dispatches
  through `FArdaDependencyExecutionContext::GetCommands()`.
- `unreal-test.cuda-postprocess` declares the same texture with a CUDA
  read/write parameter schema. `PrepareCuda` resolves graph resources and adds
  the compiled native kernel plan to `FArdaCudaSequence`.

The callback imports the two host textures, authors both node invocations inside
`FArdaGraphEdit`, keeps the output, commits, compiles, and submits the graph inside
`FArdaUnrealRHIScope`. The compiler must retain exactly two operations and one
CUDA batch. Their shared texture dependency orders compute before CUDA. The render thread records and submits both nodes on Unreal's borrowed graphics
list. The fixture disables auxiliary queues. Shader parameter binding uses a deferred UE
callback: UE owns its compiled shader's loose-uniform offsets and resource
reflection ABI. The callback retains RHI objects and values, never RDG allocation
pointers. Arda still owns node scheduling, pipeline binding, dispatch, resource
dependencies and CUDA recording.

Unreal then runs a second compute shader consuming the CUDA result. A small
sentinel region checks all four channels across 64 pixels for at least six
completed frames. Expected RGBA is `(0.375, 0.25, 0.625, 1)`; skipping or reordering
either Arda operation changes the result. The rest of the texture processes the
actual scene image. Three slots exercise resource reuse; each holds its graph,
CUDA mapping, and ticket until UE readback and Arda completion both retire.
Pending slots defer work without a CPU wait. `Saved/ArdaHandoffResult.json` records
the successful graph submission and completed-frame counts.

`-Contracts` runs the separate `Ardashir.Unreal.ProviderContract` automation test
with `-ArdaDisablePostProcess`, avoiding backend initialization-order ambiguity.
It requires the test's explicit success log entry. Logs are under `Saved/Logs`.

## CUDA ownership handoff

The project supplies `FArdaUnrealTestCudaInterop` through the runtime's public
`IArdaUnrealCudaInterop` interface before initializing Arda. It matches CUDA to
Unreal's D3D12 adapter LUID and node mask, owns dedicated shared RGBA32F textures,
imports their memory and shared fences, and supplies actual native kernel
entry/limit metadata. The provider forwards the graph's CUDA batch and resource
patch table to this adapter. This fixture admits one mapped surface per batch;
unsupported resources, external calls, graph capture, and timing fail explicitly.

Unreal RHI has no portable CUDA-in-Graphics context/capture API. The fixture
therefore uses explicit context switching: graphics transitions the texture to
COMMON and signals a shared fence; the CUDA stream waits, launches the selected
kernel, and signals a second fence; the graphics queue waits before continuing.
All submission commands run in ordered RHI lambdas. No per-frame CPU fence wait,
queue idle, or device idle is used. GPU idle is reserved for module teardown,
before releasing mappings, unregistering node callbacks, shutting down Arda, and
clearing the project-owned adapter.

This version deliberately requires D3D12 legacy barriers and a single GPU.
`r.D3D12.PreferredBarrierImplementation=1` is configured and checked at runtime.
The CUDA capability probe must successfully map a real surface before advertising
surface support. CPU/null-RHI runs cannot satisfy the positive handoff test.
