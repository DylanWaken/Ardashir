# Pixel Sort

A Windows example built from registered dependency-graph nodes: HLSL compute noise → CUDA radix sort → fullscreen
graphics draw → swap-chain presentation. The palette combines ink, indigo,
lavender and coral. Resize the window to change sorting direction and kernel
selection while the noise keeps evolving.

The example links **ArdaBackend**, **ArdaRenderGraph**, and its own
**PixelSortKernels** target. It uses native Win32 for the window, WIC for optional
PNG captures, and Vulkan headers for surface creation. Build it through the
repository's root CMake project.

## Follow the modular implementation

1. [CMakeLists.txt](CMakeLists.txt) builds the native CUDA profile and deploys node shader assets.
2. [ArdaPixelSortNodes.h](Public/Nodes/ArdaPixelSortNodes.h) includes individual
   upload, noise, sort, present and readback classes. Each has a matching `.cpp`
   under `Private/Nodes` implementing its hooks on `TArdaDependencyNode`. The base
   owns registration, attachment and state lifetime; each node prepares only its own operation. The
   [noise node](Private/Nodes/ArdaPixelSortNoiseNode.cpp) owns its HLSL entry,
   layout and pipeline settings; the [sort node](Private/Nodes/ArdaPixelSortSortNode.cpp)
   owns its CUDA operand. Shared shader-file loading is only a private utility.
   Graph parameters retain prepared device state; the renderer needs no library
   initialization and attaches classes through `Graph.AttachOrFind<Node>(...)`.
3. [ArdaPixelSortOperand.h](Public/Nodes/ArdaPixelSortOperand.h) and
   [ArdaPixelSortOperand.cpp](Private/Nodes/ArdaPixelSortOperand.cpp) define the CUDA
   parameter schema, bind compiled entries, and select a launch configuration.
4. [ArdaPixelSortKernels.cu](Private/Nodes/ArdaPixelSortKernels.cu) implements the
   compiled radix variants. [ArdaPixelSort.hlsl](Private/Nodes/Shaders/ArdaPixelSort.hlsl)
   supplies the graphics producer and presentation stages.
5. [PixelSortRenderer.cpp](PixelSortRenderer.cpp) creates/imports graph resources,
   attaches registered nodes, updates frame inputs, submits, and presents. It also
   owns the independent CPU sort oracle and PNG output.
6. [PixelSortMain.cpp](PixelSortMain.cpp) and [PixelSortWindow.cpp](PixelSortWindow.cpp)
   connect the backend to the native window and release graph references before resize.

The [shared example layout](../README.md) explains adding more registered nodes.

## Build

Use a Visual Studio developer PowerShell with CMake 3.24+, Ninja and an NVIDIA
CUDA toolkit. Select a native architecture for the GPU that will run the
application; `120` below targets SM 12.0. CUDA-in-graphics needs CUDA 13.3+ headers
and a supporting driver. Ordinary context mode uses the backend's CUDA 12+
external-memory path. The selected nvcc must support the requested targets.

```powershell
cmake -S . -B build/pixel-sort -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DARDASHIR_ENABLE_CUDA=ON -DARDASHIR_BUILD_CUDA_KERNELS=ON `
  -DARDASHIR_BUILD_PIXEL_SORT=ON -DARDASHIR_BUILD_TESTS=OFF `
  -DARDASHIR_BUILD_RHI_TEST=OFF -DARDASHIR_BUILD_ARDG_EXAMPLE=OFF `
  -DARDASHIR_BUILD_CORNELL_BOX=OFF `
  -DCMAKE_CUDA_COMPILER="C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.3/bin/nvcc.exe" `
  -DARDASHIR_CUDA_ARCHITECTURES="120"
cmake --build build/pixel-sort --target PixelSort
build/pixel-sort/Source/ArdaTests/Examples/PixelSort/PixelSort.exe --backend d3d12
```

`CMAKE_CUDA_COMPILER` pins nvcc for this build directory. Alternatively set
`CUDACXX` before the first Ninja configure, or use an installed Visual Studio
CUDA toolset. Use a fresh build directory when changing toolchains. The root
build finds nvcc only when `ARDASHIR_BUILD_CUDA_KERNELS` is enabled; disabling
that option also requires `ARDASHIR_BUILD_PIXEL_SORT=OFF` in an existing cache.
See the [compiler discovery guide](../../../../Docs/ArdaBackend/cuda-interop.html#nvcc-build).

The example defaults on for Windows builds that compile CUDA kernels. Normal
CUDA-disabled builds omit it. `ARDASHIR_BACKEND_D3D12` and
`ARDASHIR_BACKEND_VULKAN` choose which native providers to link.

For several GPUs use, for example, `-DARDASHIR_CUDA_ARCHITECTURES="80;120"`.
`ardashir_add_cuda_kernels` compiles native images with `-real`, generates the
coverage manifest and links static cudart. Both entries inherit that profile's
coverage. To give additional kernels distinct architectures or compiler flags,
create separate named profiles and explicitly call each profile's binding
export, as in the [profile guide](../../../../Docs/ArdaBackend/cuda-interop.html#offline-ptx).

## Run and interact

Use Python 3.10+ for the launcher in `Scripts/Examples`. It follows the ARDG and Cornell Box launcher
convention: backend, build directory, then configuration. It configures, builds
and launches by default. Run it from ordinary PowerShell or Command Prompt and
select the toolkit and native GPU architectures:

```powershell
python Scripts/Examples/RunPixelSort.py vulkan build/pixel-sort Release --generator Ninja `
  --nvcc "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.3/bin/nvcc.exe" `
  --architectures "120"
python Scripts/Examples/RunPixelSort.py vulkan build/pixel-sort Release --run-only
```

For a new build the launcher selects Ninja unless `--generator` or
`CMAKE_GENERATOR` specifies another generator. Existing build directories keep
their generator. With nvcc on the terminal's `PATH`, no `--nvcc` option is needed:

```powershell
python Scripts/Examples/RunPixelSort.py vulkan build/pixel-sort-ninja Release --architectures "120"
```

All three example launchers automatically locate Visual Studio or Build Tools,
prepare an x64 developer environment for configure/build subprocesses, and find
bundled CMake/Ninja when those tools are absent from `PATH`. Install **Desktop
development with C++**, including MSVC x64 tools and a Windows SDK. Install
**C++ CMake tools for Windows** for bundled CMake/Ninja, or provide CMake 3.24+
and Ninja separately on `PATH`. An existing usable x64 developer environment
is reused; cached MSVC installations/toolsets are preserved. Setup changes
neither the parent terminal nor the launched application's environment.
The manual CMake commands above still require a developer shell.

Visual Studio generators require CUDA MSBuild integration as well as nvcc;
an existing Visual Studio build cannot be converted to Ninja in place.

`--nvcc` also sets the backend's include directory to the toolkit's `include`
folder; `--cuda-include-dir` overrides that location. Existing builds reuse
their compiler and architecture cache unless overridden. New builds disable
other examples and tests; add `--cmake-arg=-DARDASHIR_BUILD_TESTS=ON` to provision
validation for verification. Repeated `--cmake-arg=-DNAME=VALUE` options pass
additional settings to CMake. Use a fresh directory when changing generators
or CUDA compilers.

A previous `CMAKE_CUDA_COMPILER=NOTFOUND` is retried on the next configure.
Successful compiler selections stay cached. CMake now reports the underlying
host compiler/toolset error directly if CUDA setup fails. Automatic provider
header discovery selects the directory containing `cuda.h`, including toolkits
that also report separate CCCL include directories.

`--run-only` launches the existing executable without Visual Studio setup,
invoking CMake, or looking for nvcc. Both single-configuration and Visual Studio configuration directories
are supported. The launcher forwards the frame, extent, channel, threshold,
CUDA mode, capture and validation options below and preserves the executable's
exit code. Use `--help` for its options. Direct invocation is also supported:

```powershell
build/pixel-sort/Source/ArdaTests/Examples/PixelSort/PixelSort.exe --backend vulkan --cuda-mode auto
build/pixel-sort/Source/ArdaTests/Examples/PixelSort/PixelSort.exe --backend d3d12 --cuda-mode context
```

- **Resize:** portrait selects columns, landscape or square selects rows.
- **C:** cycle the red, green and blue sort keys; the default is red.
- **Space:** pause/resume the continuous animation clock.
- **Tab:** toggle the original compute-shader texture.
- **Up / Down:** change the dark-pixel boundary threshold by eight.
- **Esc:** close the window.

The title displays the actual CUDA execution mode, direction, thread count,
channel and threshold. Minimized windows stop rendering; restoring recreates
the swap chain and size-dependent textures. Resize and shutdown wait for GPU
completion before replacing resources. Ordinary animation uses GPU handoffs,
with no CPU readback or device-idle wait in the rendering loop.

`--cuda-mode auto` uses the backend's capability-qualified choice. `context`
forces the ordinary CUDA context; `graphics` requires CUDA-in-graphics.
An unavailable backend, validation layer, CUDA mode or surface capability
returns code **77** with a reason; workload errors return **1**.

## One frame and two compiled variants

`FindOrCreateFrameGraph` assembles a persistent graph per swap-chain color texture
and verification/capture configuration. It attaches consumers first to demonstrate
that resource dependencies determine the execution order:

1. `UploadFrame` writes time/extent/display constants.
2. `Noise` reads those constants and writes an **RGBA8UInt** texture with `NoiseCS`.
3. `Sort` reads noise and writes a separate sorted texture. Its registered CUDA
   callback resolves logical handles and appends the retained operand to the
   compiler-provided sequence; the renderer never launches a kernel or creates a stream.
4. `Present` reads the constants and both images and draws the fullscreen triangle.
5. Optional registered readback nodes copy source/output/captured pixels to CPU staging.

ArdaInductor creates the compute and graphics PSOs, lowers barriers and ownership
transfers, and submits the graphics/CUDA sequence. Graphs and per-graph binding sets
are reused across frames. Constants, noise and output are graph-owned transients,
so different cached frame graphs do not share writable intermediate storage.
Frame input values are sampled before submission; changing time, channel, threshold,
or the original-image toggle does not require an edit.

Ordinary animation uses `Submit`; diagnostic readbacks use `Wait(ticket)` before
mapping. Recycling an occupied frame slot can wait. Resize releases graphs before
replacing the swap chain, and graph destruction retires its own outstanding work.
[CUDA in graphics](../../../../Docs/ArdaBackend/cuda-graphics.html) explains the
underlying CUDA-in-graphics and ordinary-context handoff contracts.

`BindKernelVariants` calls the nvcc-built registration function. A C++ integer
sequence instantiates two typed `__global__` entries, each taking only
`FArdaPixelSortParameters::FCuda` by value. Their host payloads describe threads,
tile length and direction; the generated build manifest describes native
architecture support. `SelectKernel` chooses among compatible entries:

- **Width ≥ height:** `RadixSort<128, false>`, 128 threads, four pixels per
  thread, 512-pixel row tiles. Grid = `(ceil(width/512), height, 1)`.
- **Height > width:** `RadixSort<256, true>`, 256 threads, four pixels per
  thread, 1024-pixel column tiles. Grid = `(ceil(height/1024), width, 1)`.

This is an explicit visual selection policy, not a claim of optimal tuning.
Each operand dispatch launches exactly one kernel. Registration and all
resource/schema/launch validation use the existing framework implementations.

## What the radix kernel sorts

This is **segmented pixel sorting**, an image effect rather than a whole-image
global ordering. A tile's pixels below the integer luminance threshold
`(54*R + 183*G + 19*B) >> 8` stay fixed and delimit bright runs. Every bright
run is stably sorted in ascending order by the selected RGB channel. The whole
RGBA pixel moves together, preserving the palette; channels are not sorted
independently into unrelated colors. At threshold zero an entire tile is sorted.

A prefix count of dark boundaries assigns run IDs. The composite key places
the run ID above a nine-bit channel field (zero for a boundary, `channel+1`
for bright pixels). Stable least-significant-bit radix passes partition zeros
and ones using warp ballots, population counts and block-wide synchronization.
Nineteen passes cover a 512-pixel tile and twenty cover a 1024-pixel tile.
Padding sorts to the end and never writes beyond the image. Surface X addresses
are byte offsets, so an RGBA8 pixel at X uses `X*4`.

Tiles bound shared storage to roughly 8/16 KiB plus prefix counters and make
every block independent. Runs stop at tile edges, which can create deliberate
seams. This favors a compact, single-launch teaching example over a global
multi-pass sorter. Large windows increase work with pixel count; column access
is less contiguous than row access. Measure before extending the tuning policy.

The HLSL UAV has the Vulkan `rgba8ui` image-format annotation so SPIR-V matches
the physical storage. CUDA surface conversion preserves integer bytes. The
presentation shader explicitly divides by 255 for display; importing a surface
does not normalize or color-convert it automatically.

## Verify and capture

Enable `ARDASHIR_BUILD_TESTS=ON` when configuring to register eight native GPU
tests and provision optional validation layers. The test configuration also
builds the existing repository test targets/profiles at configure time; use an
nvcc version that supports their architecture settings.

```powershell
ctest --test-dir build/pixel-sort -R '^PixelSort\.' --output-on-failure
build/pixel-sort/Source/ArdaTests/Examples/PixelSort/PixelSort.exe --backend vulkan `
  --hidden --verify --resize-test --frames 6
build/pixel-sort/Source/ArdaTests/Examples/PixelSort/PixelSort.exe --backend d3d12 `
  --width 900 --height 1200 --time 4 --frames 1 --capture portrait.png
```

`--verify` enables native validation, reads back both compute/CUDA textures after
completion and compares **every RGBA pixel** with an independent CPU
`std::stable_sort` reference. The six real Win32 resizes cover both variants,
all RGB keys, multiple thresholds, partial tiles and multi-tile lines. Two additional
`PixelSort.d3d12.Replay` and `PixelSort.vulkan.Replay` tests execute eight same-size
frames to exercise cached graph reuse.
`--validation` enables graphics validation without readback. Ordinary runs do
not require development validation layers. `--frames 0` (the default) runs
until the window closes. `--time` freezes the clock for reproducible captures.
PNG capture reads the rendered back buffer, on the final bounded frame or the
first frame when running indefinitely; its parent directory must already exist.

Local qualification: Windows, RTX PRO 6000 Blackwell (SM 120), driver 610.62,
nvcc 13.3.73. D3D12 automatic/ordinary mode and Vulkan
automatic/ordinary/CUDA-in-graphics pass native validation and exact CPU
comparisons. D3D12 CUDA-in-graphics surface qualification fails on this driver;
that forced mode skips with its capability reason. Other GPUs are unqualified.

## Runtime package

Keep the executable and its deployed `D3D12`, `Nodes/Shaders`, and `ShaderCompiler`
directories together (`D3D12` is only needed by that provider).
This development example compiles/caches its **HLSL graphics shaders** on first
use in `.arda-cache/PixelSort` beside the executable, so that directory must be
writable. A distribution can prebuild and ship those graphics artifacts.

**CUDA kernels are compiled and statically linked during the project build.**
The runtime never searches for nvcc, loads PTX, invokes NVRTC or compiles CUDA
source. Users need a compatible NVIDIA driver and a package containing a native
image for their GPU; installing nvcc cannot repair missing deployed code.

For the underlying primitives see NVIDIA's
[warp intrinsics](https://developer.nvidia.com/blog/using-cuda-warp-level-primitives/)
and Microsoft's
[SPIR-V image formats](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#vulkan-specific-image-formats).
