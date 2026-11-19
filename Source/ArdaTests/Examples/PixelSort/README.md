# Pixel Sort

A Windows example that generates HLSL noise, sorts pixels through a registered
native compute operand, and draws the result to the swap chain. The sort uses a
qualified CUDA radix kernel or an HLSL compute fallback. Resize the window to
change sorting direction while the noise keeps evolving.

The example uses **ArdaBackend**, **ArdaDependencyGraph**, **ArdaNativeCompute**,
and the optional **PixelSortKernels** target. It uses Win32 for the window, WIC for optional
PNG captures, and Vulkan headers for surface creation. Build it through the
repository's root CMake project.

## Follow the modular implementation

1. [CMakeLists.txt](CMakeLists.txt) builds the native CUDA profile and deploys node shader assets.
2. [ArdaPixelSortNodes.h](Public/Nodes/ArdaPixelSortNodes.h) includes individual
   upload, noise, sort, present and readback classes. Each has a matching `.cpp`
   under `Private/Nodes` implementing its hooks on `TArdaNodeDefinition`. Declarative
   node parameter schemas map resources to their shader or CUDA parameter members.
   The base owns registration, invocation and state lifetime; each node prepares its own operation. The
   [noise node](Private/Nodes/ArdaPixelSortNoiseNode.cpp) owns its HLSL entry,
   layout and pipeline settings; the [sort node](Private/Nodes/ArdaPixelSortSortNode.cpp)
   invokes the registered operand. Its `ARDA_NODE_CUDA_TEXTURE` fields inherit the shared parameter schema's
   resource access and `ResolveNodeCudaParameters` resolves the same mappings before
   dispatch. Shared shader-file loading is a private utility.
   Graph parameters retain prepared device state; the renderer needs no library
   initialization and invokes exported callables such as `PixelSortSort(Edit, Parameters)`.
   Each header declares its callable with `ARDA_DECLARE_GRAPH_NODE`; its `.cpp`
   supplies the matching `ARDA_DEFINE_GRAPH_NODE` exactly once.
3. [ArdaPixelSortOperand.h](Public/Nodes/ArdaPixelSortOperand.h) and
   [ArdaPixelSortOperand.cpp](Private/Nodes/ArdaPixelSortOperand.cpp) define the shared
   parameter schema, register both routes, and select launch geometry. Resource slot access,
   CUDA ABI patches and marshaling come from the same schema as the node's parameter mappings.
4. [ArdaPixelSortKernels.cu](Private/Nodes/ArdaPixelSortKernels.cu) implements the
   compiled radix variants. [ArdaPixelSort.hlsl](Private/Nodes/Shaders/ArdaPixelSort.hlsl)
   supplies the producer, compute fallback, and presentation stages.
5. [PixelSortRenderer.cpp](PixelSortRenderer.cpp) allocates/binds typed graph resources through
   `FArdaGraphEdit`, invokes operations, updates frame inputs, submits, and presents. It also
   owns the independent CPU sort oracle and PNG output.
6. [PixelSortMain.cpp](PixelSortMain.cpp) and [PixelSortWindow.cpp](PixelSortWindow.cpp)
   connect the backend to the native window and release graph references before resize.

The [shared example layout](../README.md) explains adding more registered nodes.

## Build

Use a Visual Studio developer PowerShell with CMake 3.24+ and Ninja. For native
CUDA kernels, install the NVIDIA CUDA toolkit and select an architecture for the GPU that will run the
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
build finds nvcc when `ARDASHIR_BUILD_CUDA_KERNELS` is enabled.
See the [compiler discovery guide](../../../../Docs/ArdaBackend/cuda-interop.html#nvcc-build).

For a shader-only executable, configure with `ARDASHIR_BUILD_PIXEL_SORT=ON`,
`ARDASHIR_ENABLE_CUDA=OFF`, and `ARDASHIR_BUILD_CUDA_KERNELS=OFF`.
`ARDASHIR_BACKEND_D3D12` and
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
python Scripts/Examples/RunPixelSort.py d3d12 build/pixel-sort-shader Release --no-cuda --verify --frames 1
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
other examples and tests; add `--cmake-arg=-DARDASHIR_BUILD_TESTS=ON` to register
the GPU tests. Verification does not require validation layers. Repeated
`--cmake-arg=-DNAME=VALUE` options pass additional settings to CMake. Use a fresh
directory when changing generators or CUDA compilers.

A previous `CMAKE_CUDA_COMPILER=NOTFOUND` is retried on the next configure.
Successful compiler selections stay cached. CMake reports the underlying
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

The title displays the selected route, direction, thread count,
channel and threshold. Minimized windows stop rendering; restoring recreates
the swap chain and size-dependent textures. Resize and shutdown wait for GPU
completion before replacing resources. Ordinary animation uses GPU handoffs,
with no CPU readback or device-idle wait in the rendering loop.

`--cuda-mode auto` uses a qualified CUDA implementation when available and
otherwise uses the compute shader. `context` requires ordinary CUDA, and
`graphics` requires CUDA-in-graphics. Add `--require-cuda` to require native
execution in automatic mode. Unavailable required capabilities return code
**77** with a reason. Workload errors return **1**.

## One frame and two compiled variants

`FindOrCreateFrameGraph` assembles a persistent graph per swap-chain color texture
and verification/capture configuration. Each node accepts its own parameter struct;
the renderer supplies prefilled structs or inline aggregates and connects output
handles explicitly. Resource dependencies determine the execution order:

1. `UploadFrame` writes time/extent/display constants.
2. `Noise` reads those constants and writes an **RGBA8UInt** texture with `NoiseCS`.
3. `Sort` reads noise and writes a separate sorted texture. The graph chooses
   the route before allocating the textures. The native callback resolves graph
   handles and adds prepared work to the compiler-provided CUDA sequence. The
   shader fallback records on the graph's command list using ordinary textures.
4. `Present` reads the constants and both images and draws the fullscreen triangle.
5. Optional registered readback nodes copy source/output/captured pixels to CPU staging.

ArdaInductor creates the compute and graphics PSOs, lowers barriers and ownership
transfers, and submits the graphics/CUDA sequence. Graphs and per-graph binding sets
are reused across frames. Constants, noise and output are graph-owned transients,
so different cached frame graphs do not share writable intermediate storage.
Upload and sort retain separate dynamic input structs within each cached frame graph.
The renderer updates time and the original-image toggle in the upload input, and
channel and threshold in the sort input before submission. Recording samples those
values without requiring a graph edit.

Ordinary animation uses `Submit`; diagnostic readbacks use `Wait(ticket)` before
mapping. Recycling an occupied frame slot can wait. Resize releases graphs before
replacing the swap chain, and graph destruction retires its own outstanding work.
PixelSort registers both `cuda-in-graphics` and `cuda`. For each compiled entry,
`AddArdaNativeCudaVariants` gives both providers the same source and orientation
check. The renderer tries CiG first. An ordinary CUDA context or the registered
HLSL shader can run the same sort when the requested mode allows it.
[Provider selection](../../../../Docs/ArdaNativeCompute/providers.html#cig) ·
[Resource handoffs](../../../../Docs/ArdaBackend/cuda-graphics.html)

The nvcc translation unit exports two typed `__global__` entries, each taking
`FArdaPixelSortParameters::FArdaCuda` by value. The generated manifest describes
native architecture support. The registry checks support and each variant's
selection callback accepts its orientation:

- **Width ≥ height:** `RadixSort<128, false>`, 128 threads, four pixels per
  thread, 512-pixel row tiles. Grid = `(ceil(width/512), height, 1)`.
- **Height > width:** `RadixSort<256, true>`, 256 threads, four pixels per
  thread, 1024-pixel column tiles. Grid = `(ceil(height/1024), width, 1)`.

Each native dispatch launches one kernel. The shader fallback preserves the same
tile boundaries and stable ordering with a group-shared sorting network.

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

Enable `ARDASHIR_BUILD_TESTS=ON` when configuring to register the native GPU
tests. Building PixelSort does not depend on validation-layer provisioning.
The test configuration also builds the existing repository test targets/profiles
at configure time; use an nvcc version that supports their architecture settings.

```powershell
ctest --test-dir build/pixel-sort -R '^PixelSort\.' --output-on-failure
build/pixel-sort/Source/ArdaTests/Examples/PixelSort/PixelSort.exe --backend vulkan `
  --hidden --verify --resize-test --frames 6
build/pixel-sort/Source/ArdaTests/Examples/PixelSort/PixelSort.exe --backend d3d12 `
  --width 900 --height 1200 --time 4 --frames 1 --capture portrait.png
```

`--verify` reads back the source and sorted textures after completion and compares
**every RGBA pixel** with an independent CPU
`std::stable_sort` reference. The six real Win32 resizes cover both variants,
all RGB keys, multiple thresholds, partial tiles and multi-tile lines. Two additional
`PixelSort.d3d12.Replay` and `PixelSort.vulkan.Replay` tests execute eight same-size
frames to exercise cached graph reuse. Shader-only builds register the same
resize and pixel comparison under `PixelSort.<backend>.Shader`.
`--validation` independently enables native GPU validation and can be combined
with `--verify`. It requires an `ARDASHIR_ENABLE_GPU_VALIDATION=ON` build;
OFF builds reject the flag. Ordinary runs and `--verify` do not require development
validation layers. Existing GPU CTest cases cover ordinary runs; separate
`Validation` cases add `--validation` in ON builds, including asynchronous replay.
`--frames 0` (the default) runs until the window closes. `--time` freezes the
clock for reproducible captures.
PNG capture reads the rendered back buffer, on the final bounded frame or the
first frame when running indefinitely; its parent directory must already exist.

The Windows qualification run used an RTX PRO 6000 Blackwell (SM 120),
driver 610.62, and nvcc 13.3.73. D3D12 automatic and ordinary modes passed
native validation and exact CPU comparisons, as did Vulkan automatic, ordinary,
and CUDA-in-graphics modes. On that driver, forced D3D12 CUDA-in-graphics
skipped because the surface mapping failed qualification.

## Runtime package

Keep the executable and its deployed `D3D12`, `Nodes/Shaders`, and `ShaderCompiler`
directories together (`D3D12` is only needed by that provider).
This development example compiles/caches its **HLSL graphics shaders** on first
use in `.arda-cache/PixelSort` beside the executable, so that directory must be
writable. A distribution can prebuild and ship those graphics artifacts.

The optional CUDA kernels are compiled and statically linked during the build.
Native execution requires a compatible NVIDIA driver and a compiled image for
the GPU. The compute shader uses the regular shader compiler and artifact cache.

For the underlying primitives see NVIDIA's
[warp intrinsics](https://developer.nvidia.com/blog/using-cuda-warp-level-primitives/)
and Microsoft's
[SPIR-V image formats](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst#vulkan-specific-image-formats).
