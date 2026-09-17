# GPU test validation layers

With the default `ARDASHIR_ENABLE_GPU_VALIDATION=ON`, dedicated GPU test
executables request installed validation layers. ARDGExample, CornellBox, RHITest
and PixelSort enable native GPU validation only when launched with `--validation`.
PixelSort `--verify` independently checks CPU/GPU results; ARDGExample `--verify`
checks terrain geometry and gradients through readback. Example targets do not
depend on validation-layer provisioning. Ordinary builds do not download or
compile Vulkan validation; run `python SetupGraphicsSDK.py` to install missing
SDK components. Missing requested validation is reported as a skipped test, with
the native backend diagnostic, rather than an assertion failure. Tests which do
not request validation continue to run.

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
- **Vulkan:** `ArdaValidationLayers` looks for a supplied layer directory, enabled
  Windows registrations in the x64 registry, a previous local installation,
  SDK/environment paths, and common Linux install paths. With the explicit
  `ARDASHIR_PROVISION_VALIDATION=ON` option, missing layers are downloaded from
  pinned Khronos sources and built under `<build>/validation-layers`.
  This requires Git, Python 3.10+, a C++ compiler, and CMake 3.22.1+ for the
  upstream layer build. The first build can take several minutes.
- Network, toolchain, or optional-layer build failures produce warnings and logs
  in `<build>/validation-layers`; they do not fail the Ardashir build. Unchanged
  reconfigures and incremental builds reuse the previous attempt. Changing setup
  options or removing `<build>/validation-layers/attempt.stamp` retries it.
  Interrupted attempts and missing generated path files are retried automatically.

CMake provisioning does not install into system directories, the registry, or
Windows optional features. Cross-compiling does not automatically build host
validation layers. The separate root SDK script has a system-install policy
described below.

## Options and runtime discovery

- `ARDASHIR_ENABLE_GPU_VALIDATION=OFF` builds all examples and GPU fixtures without
  requesting Vulkan validation or the D3D12 debug layer. This works independently
  of Debug/Release and `ARDASHIR_BUILD_TESTS`. The external Vulkan host fixture also
  omits its validation layer/debug messenger. No validation provisioning target or
  local validation-layer discovery is attached to these builds. Keep ON/OFF
  versions in separate build directories; launch scripts preserve the cache value.
  Assertions and result verification remain enabled, including PixelSort and
  ARDGExample `--verify`.
  All four examples reject `--validation` when compiled OFF. The D3D12
  validation-initialization test skips in OFF builds; other tests continue
  exercising their GPU workloads.
  This policy is private to examples/tests; applications retain the public backend
  configuration, and externally forced layers are outside this option's control.
- `ARDASHIR_PROVISION_VALIDATION=OFF` is the default and disables automatic Vulkan
  downloading and building. Set it explicitly when reusing a build tree that
  previously cached ON. Already supplied/discoverable layers still work. Agility
  runtime deployment remains part of the D3D12 build.
- `ARDASHIR_VULKAN_VALIDATION_DIR=/path/to/layers` supplies the directory containing
  `VkLayer_khronos_validation.json` and its loadable library. This explicit
  selection suppresses automatic downloads, even if the directory is invalid.
- `ARDASHIR_VULKAN_VALIDATION_SOURCE_DIR=/path/to/Vulkan-ValidationLayers` uses a
  local source checkout when provisioning is ON and no installed layer was found.
  Upstream dependencies may still require downloads.

When validation is requested, backend, RHI, graph, and presentation executables
read any configured generated layer directory and add it to `VK_ADD_LAYER_PATH`
only when Khronos validation is not already discoverable. This avoids duplicate manifests when a
system SDK is installed. Direct executable runs and CTest use the same setup.
An explicit `VK_LAYER_PATH` takes precedence; existing `VK_ADD_LAYER_PATH`
entries are retained. Use `VK_LAYER_PATH` when selecting a particular layer
version instead of the installed layer.

Vulkan-enabled examples and tests also disable RTSS and OBS Vulkan capture
overlays in their own process by default. Older overlay layers advertise lower
Vulkan API versions and cause loader warnings for these Vulkan 1.4 applications.
Set `ARDASHIR_ENABLE_VULKAN_OVERLAYS=1` before launching to retain those overlays
for capture. Existing `DISABLE_RTSS_LAYER` and `DISABLE_VULKAN_OBS_CAPTURE` values
remain authoritative. This applies with validation ON or OFF, does not disable
validation or other layers, and changes neither system settings nor the public
backend's layer policy.

These CMake helpers install locally. Separately, the root `SetupGraphicsSDK.py`
now installs/registers validation machine-wide by default on Windows, with
Administrator privileges; `--local-only` opts out. That system install supports
elevated applications, which ignore local Vulkan layer-path overrides.

## Test semantics

Native D3D12 and Vulkan providers return
`EArdaInitializeResult::ValidationUnavailable` when requested validation cannot
be enabled. Vulkan also handles a discovered manifest whose library cannot load.
`GetBackendInitializeResult()` exposes the most recent headless initialization
outcome; presentation initialization returns the enum directly.

GoogleTest initialization assertions use `ARDA_REQUIRE_BACKEND()` from
`Source/TestSupport/ArdaTestBackend.h`. It skips only the missing-validation
result in validation-enabled builds and retains assertion failures for other
initialization errors (including unexpected validation requests in OFF builds). Like a
fatal assertion, it returns from the surrounding function; callers of helpers
must not continue using an uninitialized device. Presentation smoke programs
return the existing CTest skip code, 77, for missing validation.

Native CUDA and graph recipe fixtures retain an atomic diagnostic callback through
device shutdown and fail on validation errors, including deferred retirement
errors. Malformed kernel registrations and unexpected initialization failures are
test failures, not missing-hardware skips. Warnings remain visible in test output.
Only a confirmed `ValidationUnavailable` initialization skip bypasses the shutdown
error assertion, because a broken layer installation can itself emit loader errors.
Hardware-feature skips after successful initialization still check diagnostics.

PixelSort includes both CPU-oracle verification runs and `AsyncReplay` tests.
Verification waits for each readback; asynchronous replay submits twelve frames
without that per-frame wait, then retires work and checks shutdown diagnostics.
Bounded runs fail if the window closes before the requested frames complete.
The examples' existing GPU CTest cases run without native validation. Separate
`Validation` cases pass `--validation` in ON builds, including an asynchronous
PixelSort case. Dedicated GPU test executables keep their strict validation
requirements.
`ARDGExample.D3D12.Verify` and `ARDGExample.Vulkan.Verify` opt into terrain
geometry and gradient readback checks. The normal terrain viewer omits those
diagnostics. Ordinary Vulkan example CTest cases point `VK_LAYER_PATH` to an
empty directory and clear `VK_ADD_LAYER_PATH` to hide explicit validation layers.

To reproduce absence without changing the machine, copy a test executable and
`D3D12/D3D12Core.dll` to a separate deployment directory, omit
`D3D12/d3d12SDKLayers.dll`, and set `VK_LAYER_PATH` to an empty directory. A stale
Vulkan manifest pointing to a missing library exercises failed layer loading.

## Release verification and rollback

Run full suites in separate Debug/validation-ON, Debug/validation-OFF, and
Release/validation-ON build directories. Build CUDA kernels when validating CUDA;
record the toolkit, driver and GPU used. A successful external-call adapter test
does not certify a cuBLAS or cuDNN library that was not linked and executed.
Keep capability skips in the report and inspect build output and runtime
diagnostics as well as the CTest exit code. Shader compilation and native API
validation are separate checks.

Shader-compilation fixtures explicitly enable the compilation behavior they test.
The source-distributed examples likewise select a shared compilation policy in
both Debug and Release; `load-only` still requires cooked artifacts. This does not
change the backend's Release default. See the
[example startup policy](../../Source/ArdaTests/Examples/README.md).

Before publishing application binaries, retain the previous known-good deployment
as a complete versioned directory: executable, backend modules, shader artifacts,
and required runtime DLLs. Isolate writable pipeline caches by release version.
This repository's checks do not publish or replace a deployment automatically.

Rollback is triggered by new GPU validation errors, device loss, incorrect output,
or a reproducible scheduling regression on a supported configuration. Stop the
affected application, select the retained deployment directory in the launcher,
restore that version's cache location, and run its D3D12/Vulkan smoke checks before
reopening normal workloads. Preserve failure logs and the rejected release for
diagnosis. Target recovery is one application restart plus smoke-test time; verify
that procedure on the actual deployment host before release. No source reset or
in-place DLL replacement is part of rollback.

References: [Khronos build instructions](https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/e4786f7ce8f1319215eff0d938f4be4651cbb85d/BUILD.md),
[Vulkan loader discovery](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md),
and [Microsoft Agility deployment](https://devblogs.microsoft.com/directx/gettingstarted-dx12agility/).

## RHI audit implementation validation — 2026-09-17

The accepted RHI audit implementation was validated on NVIDIA RTX PRO 6000
Blackwell Workstation Edition, driver 610.62:

- Full Debug build with D3D12/Vulkan and validation enabled: **799 passed,
  146 skipped, zero failures** across 945 CTest cases.
- CUDA 13.4.59 Release build with D3D12 and validation enabled: **615 passed,
  112 skipped, zero failures** across all 727 cases from the rebuilt
  backend, RHI and graph executables. Vulkan was disabled in that configuration.

Tests cover command-generation errors, required binding sets, repeated push-only
binding reuse, descriptor/native limits, selected framebuffer views, sparse
remapping and prefix preservation, failed retirement signals/waits, shutdown
quarantine and later reclamation. GPU byte/pixel readbacks supplement ownership
counters. Capability and environment skips remain distinct from passing tests.
Configuration-specific fixtures omit/skip explicitly disabled providers and
continue to fail on unexpected initialization errors for enabled providers.

The skip audit identified unresolved coverage gaps: two symlink fixtures in each
run reported already-existing destinations, and two CUDA Release surface tests
in D3D12 graphics-queue mode skipped after a `CUDA_ERROR_UNKNOWN` capability
probe. Their normal CUDA-context counterparts passed. These are not evidence of
ordinary hardware limitations. Vulkan+CUDA was not exercised in either build;
the audit records all skip categories and counts.

Full commands, logs, documentation checks and hardware qualifications are in the
[current audit](Unreal-RHI-Audit.md#validation-and-remaining-coverage). No physical
GPU removal, real GPU-hang recovery, Vulkan CUDA run or separate validation-off
run was attempted for this implementation pass.
