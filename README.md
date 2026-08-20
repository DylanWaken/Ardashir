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

## Dependencies

- [GoogleTest](https://github.com/google/googletest) for module tests
- [GLFW](https://github.com/glfw/glfw) for cross-platform windows and surfaces
- [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) 1.4.357
- Direct3D 12 Agility SDK 1.619.5 on Windows
- CMake 3.16 or newer
- A C++17 compiler

The root build downloads pinned GLFW, DirectX Shader Compiler, Vulkan-Headers,
and Direct3D 12 Agility releases. DXC compiles shaders to DXIL and SPIR-V. The
Vulkan backend dynamically loads the platform Vulkan loader, so a separately
installed Vulkan SDK is not required.

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
arda::trace::StartTraceCapture("frame.ardatrace");
{
    ARDA_NAMED_SCOPE_TIMER("Rendering");
    ARDA_TRACE_COUNTER("Visible Objects", VisibleObjectCount);
    RenderFrame();
}
arda::trace::StopTraceCapture();
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
