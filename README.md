# Ardashir

*ARithmetic and DAta-driven SHading and Inferencing Runtime*

Ardashir is a modular C++ research and development runtime for real-time
rendering, ray-traced global illumination, GPU physics, and deep-learning
systems. Its GPU-facing modules use ArdaBackend's provider-neutral RHI. Native
Vulkan 1.4 and Direct3D 12 Agility SDK providers are shipped as separate,
linkable backend libraries; an engine RHI provider can replace them without
changing renderer code.

**Documentation:** [Canonical GitHub Pages URL](https://dylanwaken.github.io/Ardashir/) · [Documentation source](Docs/index.html)

## Modules

- **[ArdaBackend](Source/ArdaBackend)** — The graphics backend and RHI layer for
  devices, resources, shaders, pipelines, commands, and presentation. See the
  [provider module contract](Docs/ArdaBackend/BackendModules.md).

- **[ArdaRenderGraph](Source/ArdaRenderGraph)** — A foundational render
  dependency graph implementation on top of ArdaBackend. It schedules rendering work
  and manages resource dependencies, transitions, queues, and lifetimes.

- **[ArdaScene](Docs/ArdaScene/README.md)** — Planned engine-neutral scene
  representation for standalone and hosted ray-traced rendering.

- **ArdaGI** — A library for real-time, ray-tracing-based global illumination.

- **ArdaPhys** — A GPU physics library for AVBD, acceleration structures,
  solvers, and related simulation systems.

- **ArdaDL** — Deep-learning subsystems designed to integrate with the
  rendering and simulation runtime.

- **[ArdaTrace](Docs/ArdaTrace/README.md)** — Low-overhead CPU scope, counter,
  and marker recording for offline performance analysis.

## Quick user guides

- **[ArdaBackend quick user's guide](Docs/ArdaBackend/quick-guide.html)**
- **[ArdaRenderGraph quick user's guide](Docs/ArdaRDG/quick-guide.html)**

Both guides demonstrate startup, compute, raster, submission, and presentation.
Their capability-gated hardware ray-tracing path is synthesized from implemented
and tested API components; it is not a checked-in end-to-end RT sample.

## Public API naming

Public APIs share one `arda` namespace: use `arda::FArdaBackendConfiguration`,
`arda::FArdaRHIDeviceRef`, and `arda::FARDGBuilder`, or place `using namespace arda;`
in application code. Module names remain in descriptive type/function names,
following Unreal's RHI/RenderGraph convention. This is a source/ABI change: remove
the former `backend`, `rhi`, `render_graph`, and `trace` namespace qualifiers and
rebuild consumers. Provider APIs, shared implementation helpers, tests, samples, and
generated-source templates now follow the same flat namespace. Module-name helpers are now `GetBackendModuleName`,
`GetRenderGraphModuleName`, etc. Provider types use descriptive names such as
`arda::IArdaRHIProviderDevice`; pipeline-cache helpers include
`arda::ReadArdaPipelineCacheBlob`. The `ArdaNamespaceBoundary` build/CTest check
rejects nested project namespaces and Arda namespace aliases.

Host-owned Vulkan devices are supported by `native-vulkan`; see the
[enabled-feature and lifetime contract](Docs/ArdaBackend/external-interop.html#vulkan).
CUDA supports automatic fallback to serialized ordinary-context execution; see
[execution modes and qualification limits](Docs/ArdaBackend/cuda-interop.html#context-switch).
Read [How CUDA works with graphics](Docs/ArdaBackend/cuda-graphics.html) for the
general memory and synchronization model, and compare
[registered native kernels and ordinary CUDA C++ launches](Docs/ArdaBackend/cuda-interop.html#launch-methods)
in the operand guide.

## Examples

**[Pixel Sort](Source/ArdaTests/Examples/PixelSort/README.md)** is a self-contained Windows CUDA
example that links only ArdaBackend and its compiled kernels. An HLSL compute
shader generates animated noise, a CUDA radix kernel sorts its pixels, and a
fullscreen graphics pass presents the result through D3D12 or Vulkan. Resizing
switches between row and column kernel variants. See the
[CUDA walkthrough](Docs/ArdaBackend/cuda-interop.html#pixel-sort).

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
python SetupGraphicsSDK.py
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

Downloads and generated configuration go under `build/graphics-sdk` by default,
without changing the registry or global environment. `--root` changes that
directory, `--build-dir` adds an existing build cache to discovery, and
`--backend d3d12|vulkan` limits setup. `--check` makes no setup changes and returns
1 when a component is missing; normal setup also returns 1 on download/build/load
failure. A validation JSON without a loadable library is rejected. An explicit
`VK_LAYER_PATH` remains authoritative; correct or unset a broken override before
running setup. Dot-source the generated PowerShell file for Vulkan applications;
on Linux, source `build/graphics-sdk/activate-graphics-sdk.sh`. D3D12 applications
receive their local runtime/debug DLLs through the normal CMake target deployment.

Run the setup tool's offline regression checks with
`python -m unittest discover -s Scripts/Tests -p test_setup_graphics_sdk.py`.

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
