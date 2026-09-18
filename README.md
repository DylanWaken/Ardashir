# Ardashir

*ARithmetic and DAta-driven SHading and Inferencing Runtime*

Ardashir is a modular C++ runtime for graphics and GPU compute infrastructure,
dependency-graph scheduling, and tracing. Its GPU-facing modules use
ArdaBackend's provider-neutral RHI. Native
Vulkan 1.4 and Direct3D 12 Agility SDK providers are shipped as separate,
linkable backend libraries; an engine RHI provider can replace them without
changing renderer code.

**Documentation:** [Canonical GitHub Pages URL](https://dylanwaken.github.io/Ardashir/) · [Documentation source](Docs/index.html)

## Modules

GPU infrastructure lives under [Source/ArdaInfra](Source/ArdaInfra), grouping the
backend, native provider implementations, generic graph, and render graph. Each
module remains a separate CMake library with its existing public include names.
The source tree has three top-level groups:

```text
Source/
  ArdaInfra/   Backend, native providers, graph storage and render graph
  ArdaTests/   Example applications and shared TestSupport
  ArdaTrace/   CPU tracing runtime and its tests
```

- **[ArdaBackend](Source/ArdaInfra/ArdaBackend)** — The graphics backend and RHI layer for
  devices, resources, shaders, pipelines, commands, and presentation. See the
  [provider module contract](Docs/ArdaBackend/BackendModules.md).

- **[ArdaGraph](Source/ArdaInfra/ArdaGraph)** — Generic directed graph storage with indexed
  adjacency, generational handles, traversal, and graph algorithms.

- **[ArdaRenderGraph](Source/ArdaInfra/ArdaRenderGraph)** — Persistent graphics and CUDA
  dependency graphs compiled by ArdaInductor. Typed registered nodes declare
  resources; compilation resolves dependencies, pipelines, CUDA batches, queue
  scheduling, and memory budgets. See the [graph guide](Docs/ArdaRDG/ArdaInductor.md).
  Its [node library](Source/ArdaInfra/ArdaRenderGraph/README.md) provides buffer
  and texture uploads, copies, readbacks, and clears, plus native BLAS/TLAS
  cloning and compaction.

- **[ArdaTrace](Docs/ArdaTrace/README.md)** — Low-overhead CPU scope, counter,
  and marker recording for offline performance analysis.

- **[ArdaTests](Source/ArdaTests)** — Runnable infrastructure examples and
  shared [backend test support](Source/ArdaTests/TestSupport).

Scene, global-illumination, physics, deep-learning and standalone interop module
scaffolding has been removed. The [archived scene representation plan](Docs/ArdaScene/README.md)
and Unreal research guides remain design references. Native device and resource
interop is implemented within ArdaBackend.

## Quick user guides

- **[ArdaBackend quick user's guide](Docs/ArdaBackend/quick-guide.html)**
- **[ArdaRenderGraph quick user's guide](Docs/ArdaRDG/quick-guide.html)**

The guides cover startup, compute, raster, submission, and presentation.
The [Cornell Box example](Source/ArdaTests/Examples/CornellBox/README.md) demonstrates
hardware ray tracing through persistent typed graph nodes and automatic pipelines.

## Public API naming

Public APIs share one `arda` namespace: use `arda::FArdaBackendConfiguration`,
`arda::FArdaRHIDeviceRef`, and `arda::FArdaDependencyGraph`, or place `using namespace arda;`
in application code. Module names remain in descriptive type/function names,
following Unreal's RHI/RenderGraph convention. This is a source/ABI change: remove
the former `backend`, `rhi`, `render_graph`, and `trace` namespace qualifiers and
rebuild consumers. Provider APIs, shared implementation helpers, tests, samples, and
generated-source templates now follow the same flat namespace. Provider types use descriptive names such as
`arda::IArdaRHIProviderDevice`; pipeline-cache helpers include
`arda::ReadArdaPipelineCacheBlob`.

Graph changes are authored between `BeginGraphEdit` and `EndGraphEdit`, which
recompiles the graph. Use `AttachOrFind` with registered typed parameters, then
reuse `Execute` or `Submit`/`Wait` across frames. Public headers are compiled
independently to keep execution internals private.

Host-owned Vulkan devices are supported by `native-vulkan`; see the
[enabled-feature and lifetime contract](Docs/ArdaBackend/external-interop.html#vulkan).
CUDA supports automatic fallback to serialized ordinary-context execution; see
[execution modes and qualification limits](Docs/ArdaBackend/cuda-interop.html#context-switch).
Read [How CUDA works with graphics](Docs/ArdaBackend/cuda-graphics.html) for the
general memory and synchronization model, and compare
[registered native kernels and ordinary CUDA C++ launches](Docs/ArdaBackend/cuda-interop.html#launch-methods)
in the operand guide.

## Examples

Application examples live under `Source/ArdaTests/Examples`, with launchers in
`Scripts/Examples`: `RunARDGExample.py`, `RunCornellBox.py`, and `RunPixelSort.py`.
On Windows these scripts work from ordinary PowerShell or Command Prompt: they
prepare Visual Studio's x64 build environment and locate bundled CMake/Ninja
automatically. Install the **Desktop development with C++** workload with MSVC
and a Windows SDK, plus **C++ CMake tools for Windows** if CMake/Ninja are not
already on `PATH`. New Windows build directories default to Ninja; existing
generators and an explicit `CMAKE_GENERATOR` are retained.

**[Pixel Sort](Source/ArdaTests/Examples/PixelSort/README.md)** is a self-contained Windows CUDA
example that links only ArdaBackend and its compiled kernels. An HLSL compute
shader generates animated noise, a CUDA radix kernel sorts its pixels, and a
fullscreen graphics pass presents the result through D3D12 or Vulkan. Resizing
switches between row and column kernel variants. See the
[CUDA walkthrough](Docs/ArdaBackend/cuda-interop.html#pixel-sort).

Launch an existing Pixel Sort build with either backend:

```powershell
python Scripts/Examples/RunPixelSort.py d3d12 build/pixel-sort Release --run-only
python Scripts/Examples/RunPixelSort.py vulkan build/pixel-sort Release --run-only
```

Omit `--run-only` to configure and build first; see the example README for CUDA
toolkit and architecture selection.

## Dependencies

- [GoogleTest](https://github.com/google/googletest) for module tests
- [GLFW](https://github.com/glfw/glfw) for cross-platform windows and surfaces
- [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) 1.4.357
- Direct3D 12 Agility SDK 1.619.5 on Windows
- CMake 3.24 or newer
- A C++17 compiler

The root build downloads pinned GLFW, DirectX Shader Compiler, Vulkan-Headers,
and Direct3D 12 Agility releases. DXC compiles shaders to DXIL and SPIR-V. The
Vulkan backend dynamically loads the platform Vulkan loader, so a separately
installed Vulkan SDK is not required.

To detect and download missing graphics SDK/validation components before a build,
run the root-level setup script with Python 3.10+:

```powershell
python SetupGraphicsSDK.py --check
python SetupGraphicsSDK.py  # Administrator terminal on Windows
cmake -S . -B build/dev -C build/graphics-sdk/GraphicsSdk.cmake
cmake --build build/dev --config Debug
. ./build/graphics-sdk/ActivateGraphicsSDK.ps1
```

The script reuses compatible installed SDKs and existing project build caches.
Missing D3D12 components come from the project's SHA-256-verified Agility package,
including its matching debug layer. Missing Vulkan headers are downloaded at the
project's pinned tag; missing Khronos validation layers use the existing pinned
CMake source-build provisioner. That build needs Git, CMake and a C++ compiler;
on Windows the script can discover an installed Visual Studio C++ toolchain.
Visual Studio's base Windows SDK and GPU drivers remain platform prerequisites.

Ordinary builds reuse installed Vulkan validation layers, including Windows
registry installations. Building missing validation layers from source is opt-in
with `-DARDASHIR_PROVISION_VALIDATION=ON`; the default is OFF. Existing CMake
caches keep their selected value. Use `SetupGraphicsSDK.py` for SDK installation.
Unchanged reconfigures reuse completed validation setup instead of repeating it.

After setup at the default location, the `Scripts/Examples` launchers automatically
load `build/graphics-sdk/GraphicsSdkDefaults.cmake`. Empty SDK cache entries receive
the verified paths; existing nonempty paths and explicit launcher CMake arguments
take precedence. For a custom setup root, configure the example build with the
printed `cmake -C` command first.

On Windows, setup automatically installs validation under
`Program Files/Ardashir/VulkanValidation/<content-hash>` and registers the manifest
in the 64-bit `HKLM/SOFTWARE/Khronos/Vulkan/ExplicitLayers` key with DWORD value 0.
Run setup from an Administrator terminal. The installed files grant ordinary users
read/execute access; administrators and SYSTEM can update them. Rerunning setup
reuses identical files and replaces only Ardashir's previous registry entries;
other SDK registrations remain intact. Old installation directories remain available
for applications still using them. The script verifies registration by creating a
Vulkan instance with validation in a fresh process with local layer paths removed;
verification failure restores Ardashir's prior registrations.

Use `--local-only` for project-local setup without registration. Local layer paths
work in non-elevated applications; Vulkan ignores `VK_LAYER_PATH` and
`VK_ADD_LAYER_PATH` in elevated applications. The default machine-wide install
therefore also supports the built-in Administrator account. See the
[Khronos loader restriction](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md#exception-for-elevated-privileges).

Downloads and generated configuration go under `build/graphics-sdk` by default;
the script does not change global environment variables. `--root` changes that
directory, `--build-dir` adds an existing build cache to discovery, and
`--backend d3d12|vulkan` limits setup. `--check` makes no setup changes and returns
1 when a component or the default Windows system registration is missing; it also
verifies that the system Vulkan loader can load validation. `--check --local-only`
checks only local SDK components. Normal setup also returns 1 on download, build,
load, permission, or registration failure. A validation JSON without a loadable library is rejected. An explicit
`VK_LAYER_PATH` remains authoritative; correct or unset a broken override before
running setup. Dot-source the generated PowerShell file for Vulkan applications;
on Linux, source `build/graphics-sdk/activate-graphics-sdk.sh`. D3D12 applications
receive their local runtime/debug DLLs through the normal CMake target deployment.

Run the setup tool's offline regression checks with
`python -m unittest discover -s Scripts/Tests -p test_setup_graphics_sdk.py`.

### Examples and tests without GPU validation

`ARDASHIR_ENABLE_GPU_VALIDATION` defaults to `ON`, independently of Debug/Release.
Normal runs of ARDGExample, CornellBox, RHITest and PixelSort leave native GPU
validation disabled. Add `--validation` to an example launch to request it;
PixelSort `--verify` independently enables CPU/GPU result comparisons; ARDGExample
`--verify` checks terrain geometry, gradients, and sampled heights against CPU
noise/erosion results, detecting missing generation and flat heightmaps. Example
targets do not depend on validation-layer provisioning. The examples' existing
GPU CTest cases run without native validation; separate `Validation` cases request
it explicitly in ON builds. Dedicated GPU test executables retain their strict
validation policy.

To diagnose a red or flat Vulkan terrain on a machine without validation layers,
run `ARDGExample --backend vulkan --frames 3 --hidden --verify`. A height mismatch
reports its grid coordinate and expected/actual values and exits with failure.

Build a separate version of every enabled example and GPU test with native
Vulkan/D3D12 validation disabled at compile time using:

```powershell
cmake -S . -B build/no-validation -DARDASHIR_ENABLE_GPU_VALIDATION=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/no-validation --config Release
ctest --test-dir build/no-validation -C Release --output-on-failure
python Scripts/Examples/RunCornellBox.py vulkan build/no-validation Release
```

The launchers preserve this cached option. Configure another build directory with
`-DARDASHIR_ENABLE_GPU_VALIDATION=ON` to keep both versions. The option also applies
when `ARDASHIR_BUILD_TESTS=OFF`. Disabled builds omit layer provisioning and local
layer discovery, and all examples and GPU fixtures request devices without debug
layers, including the external Vulkan host fixture. Assertions, resource checks,
GPU execution and result checks remain active. PixelSort and ARDGExample `--verify`
continue checking output; all four examples reject `--validation` in an OFF
build. The D3D12 debug-layer initialization test skips when validation is OFF.
This controls project examples/tests; the public backend's application configuration
and any layers forced externally by driver tools remain independent.

## Building

Initialize the git submodules before configuring either platform:

```sh
git submodule update --init --recursive
```

### Windows

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

### Linux

Install a compiler, CMake, Ninja, Git, and the X11 development packages. On
Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build git pkg-config \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libvulkan1 mesa-vulkan-drivers

cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-linux
ctest --test-dir build-linux --output-on-failure
```

Set `ARDASHIR_BUILD_TESTS=OFF` to omit GoogleTest and the module test targets.
Set `ARDASHIR_BUILD_RHI_TEST=OFF` to omit the graphics integration test.
Set `ARDASHIR_BACKEND_VULKAN=OFF` or
`ARDASHIR_BACKEND_D3D12=OFF` to omit either native provider. Turning both
off leaves ArdaBackend ready for a host-supplied provider library.
Set `ARDASHIR_ENABLE_TRACE=OFF` to compile trace instrumentation to no-ops.

## Trace captures

Include `ArdaScopeTimer.h` and `ArdaTrace.h`, then bracket the work to capture:

```cpp
arda::StartTraceCapture("frame.ardatrace");
{
    ARDA_NAMED_SCOPE_TIMER("Rendering");
    ARDA_TRACE_COUNTER("Visible Objects", VisibleObjectCount);
    RenderFrame();
}
arda::StopTraceCapture();
```

Install Flask and launch the local offline viewer:

```powershell
python -m pip install -r Tools\ArdaTraceViewer\requirements.txt
python Scripts\RunTraceViewer.py frame.ardatrace
```

The viewer binds to loopback by default. Open the displayed URL to inspect
thread timelines, nested scopes, counters, markers, and aggregate statistics.

## Native RHI triangle

`RHITest` is a minimal indexed, vertex-colored triangle application. It
supports D3D12 and Vulkan on Windows and Vulkan on Linux. D3D12 is the Windows
default:

```powershell
.\build\Source\ArdaTests\RHITest\Debug\RHITest.exe
.\build\Source\ArdaTests\RHITest\Debug\RHITest.exe --backend vulkan
```

Linux uses Vulkan by default:

```sh
./build-linux/Source/ArdaTests/RHITest/RHITest
```

Use `--frames N` to close after a fixed number of frames and `--hidden` for a
non-interactive smoke run. The available one-frame GPU tests are registered
with CTest:

```sh
ctest --test-dir build -C Debug -L gpu --output-on-failure
```

A missing graphics backend returns CTest's skip code; rendering, presentation,
shader, native API validation, or presentation failures fail the test.

## Documentation deployment

Changes under `Docs` on `master` are validated and deployed to the canonical
GitHub Pages URL by GitHub Actions using `.github/workflows/pages.yml`.

[Self-contained node recipes](Docs/ArdaRDG/node-recipes.html) cover compute shaders, CUDA, ray tracing, mesh shaders, rasterization and work graphs, including automatic bindings and range-aware bindless dependencies.

## Source conventions

First-party C++, CUDA and HLSL use the root `.clang-format` with clang-format 19:

```sh
python Scripts/FormatCode.py
python Scripts/FormatCode.py --check
```

The inventory includes new untracked source files and compiled documentation examples;
vendors, build outputs and generated documentation snapshots are excluded. Regenerate
snapshots with `python Scripts/Docs/SyncGraphNodeRecipes.py` after source edits.
Use blank lines between validation, setup, work and result handling where these are
separate phases, and explain major blocks with comments describing their purpose.

All graph operations derive from `TArdaDependencyNode` or a domain specialization.
Built-ins and CUDA operand adapters use that same contract. There are no
callback-definition registration or compatibility overloads.
