# GPU test validation layers

Test builds provision validation automatically. Missing validation is reported as
a skipped test, with the native backend diagnostic, rather than an assertion
failure. Tests which do not request validation continue to run.

## Build and run

```sh
cmake -S . -B build -DARDASHIR_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

- **D3D12:** the existing pinned Agility SDK NuGet dependency supplies the matching
  `D3D12Core.dll` and `d3d12SDKLayers.dll`. Executables deploy them into their
  `D3D12` subdirectory. Failure to deploy the optional debug layer produces a
  warning; the core runtime remains a required dependency.
- **Vulkan:** `ArdaValidationLayers` first looks for a supplied layer directory,
  a previous local installation, SDK/environment paths, and common Linux install
  paths. Otherwise it downloads pinned Khronos validation sources and builds
  their pinned dependencies, then installs under `<build>/validation-layers`.
  This requires Git, Python 3.10+, a C++ compiler, and CMake 3.22.1+ for the
  upstream layer build. The first build can take several minutes.
- Network, toolchain, or optional-layer build failures produce warnings and logs
  in `<build>/validation-layers`; they do not fail the Ardashir build. Reconfigure
  to retry provisioning. Incremental builds reuse the previous attempt.

Nothing is installed into system directories, the registry, or Windows optional
features. Cross-compiling does not automatically build host validation layers.

## Options and runtime discovery

- `ARDASHIR_PROVISION_VALIDATION=OFF` disables automatic Vulkan downloading and
  building. Already supplied/discoverable layers still work. Agility runtime
  deployment remains part of the D3D12 build.
- `ARDASHIR_VULKAN_VALIDATION_DIR=/path/to/layers` supplies the directory containing
  `VkLayer_khronos_validation.json` and its loadable library. This explicit
  selection suppresses automatic downloads, even if the directory is invalid.
- `ARDASHIR_VULKAN_VALIDATION_SOURCE_DIR=/path/to/Vulkan-ValidationLayers` uses a
  local source checkout when no installed layer was found. Upstream dependencies
  may still require downloads.

Backend, RHI, RDG, and presentation test executables read the generated layer
directory and add it to `VK_ADD_LAYER_PATH`. Direct executable runs and CTest
therefore use the same setup. An explicit `VK_LAYER_PATH` takes precedence;
existing `VK_ADD_LAYER_PATH` entries are retained.

## Test semantics

Native D3D12 and Vulkan providers return
`EArdaInitializeResult::ValidationUnavailable` when requested validation cannot
be enabled. Vulkan also handles a discovered manifest whose library cannot load.
`GetBackendInitializeResult()` exposes the most recent headless initialization
outcome; presentation initialization returns the enum directly.

GoogleTest initialization assertions use `ARDA_REQUIRE_BACKEND()` from
`Source/TestSupport/ArdaTestBackend.h`. It skips only the missing-validation
result and retains assertion failures for other initialization errors. Like a
fatal assertion, it returns from the surrounding function; callers of helpers
must not continue using an uninitialized device. Presentation smoke programs
return the existing CTest skip code, 77, for missing validation.

To reproduce absence without changing the machine, copy a test executable and
`D3D12/D3D12Core.dll` to a separate deployment directory, omit
`D3D12/d3d12SDKLayers.dll`, and set `VK_LAYER_PATH` to an empty directory. A stale
Vulkan manifest pointing to a missing library exercises failed layer loading.

References: [Khronos build instructions](https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/e4786f7ce8f1319215eff0d938f4be4651cbb85d/BUILD.md),
[Vulkan loader discovery](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md),
and [Microsoft Agility deployment](https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/).
