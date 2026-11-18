# Unreal integration validation

## Runtime integration on 2026-11-18

Verified on Windows x64 with Unreal 5.8.1, D3D12 and a native `sm_120`
CUDA kernel. The plugin built its C++20 shared library from runtime commit
`02d7445c129365e528230a9658a4ec20a3f0f3c7`.

| Check | Observed result |
| --- | --- |
| Shared-library ABI | Passed feature preflight, graph identity and callback revision reuse |
| UnrealBuildTool | Built the Development Editor target, both plugin modules and the test project |
| Unreal automation | `Ardashir.Unreal.ProviderContract` passed |
| Viewport GPU handoff | Two registered nodes, eight graph submissions, six completed Unreal → Arda → CUDA → Unreal frames, 64 RGBA pixel checks per frame |

The bootstrap used `--test` with the version in `ArdaRevision.json`. The
fixture commands are in the [test guide](Tests/README.md).

## Earlier validation on 2026-09-24

| Component | Tested configuration |
| --- | --- |
| Engine | Unreal 5.8.1 source checkout, `D:/UnrealEngine` |
| Project | `D:/UnrealProjects/ArdashirUnrealTest` |
| Compiler | MSVC 19.44.35228, toolset 14.44.35207 |
| Windows SDK | 10.0.26100.0 |
| CUDA | Toolkit 13.4, native `sm_120` CUBIN |
| GPU | NVIDIA RTX PRO 6000 Blackwell Workstation Edition |
| Driver | 610.62 |
| RHI | D3D12, single GPU, legacy barriers |
| Published Ardashir core | `e5b40a519082c39054e12d9db7cf96719cd8a6aa` |

Core validation, the plugin dependency rebuild, and the Unreal build and runtime
checks below cover this published revision. The earlier isolated CUDA surface
test is labeled separately.

## Dependency provenance

The plugin dependency is a separate, clean upstream Git checkout. Core fixes
were authored in the development repository's isolated worktree, compiled with
MSVC, committed, and pushed to upstream `master` before consumption by the plugin.
[Commit 384d914](https://github.com/DylanWaken/Ardashir/commit/384d914b51e15555921ce8b8f660c68d2822af9f)
centralizes graph identity allocation and construction-key thread-local state
across DLL boundaries, and fixes C++20 construction of the node callables.
[Commit e5b40a5](https://github.com/DylanWaken/Ardashir/commit/e5b40a519082c39054e12d9db7cf96719cd8a6aa)
adds required-feature checks and optional CUDA compute alternatives, including
compiler revalidation and separate prepared-state caches for the two routes.
No source files inside the dependency checkout were patched locally.

## Commands and results

Run these from `D:/Projects/Ardashader` with Python, Visual Studio, CMake, and
CUDA available. The cached-dependency option is specific to this machine.

```powershell
python Integrations/Unreal/ArdashirUnreal/Scripts/ArdaBuild.py --dependency-cache D:/Projects/Ardashader/build/_deps --update --test
& ./Integrations/Unreal/ArdashirUnreal/Tests/ArdaSetupUnrealTest.ps1 -Build
& ./Integrations/Unreal/ArdashirUnreal/Tests/ArdaRunUnrealTest.ps1
& ./Integrations/Unreal/ArdashirUnreal/Tests/ArdaRunUnrealTest.ps1 -Contracts
```

## Core verification for e5b40a5

- **PASS:** CUDA-disabled published core graph suite: **467 passed**, one skip
  for unsupported work graphs. Published core operand checks: **10 passed**,
  two skips requiring a qualified CUDA device. Logs:
  `build/feature-publish/graph-final.log` and
  `build/feature-publish/backend-final.log`.
- **PASS:** real D3D12 and Vulkan graph compute alternatives. Each backend
  executes three frames and verifies 257 integer results per frame, using a
  node-owned buffer without CUDA interop storage. The graph performs ordinary
  shader dispatch and readback; its CUDA callback is never entered. Log:
  `build/feature-no-cuda/graph-alternative.log`; both cases also run in the
  published core suite above.
- **PASS:** development working-tree graph suite: **512 passed**, three skips
  (two CUDA-disabled-only cases exercised separately, one unsupported work graph).
  The focused feature/route-cache checks pass **30/30**, and public/private graph
  headers compile independently. Logs: `build/feature-route-cache-full.log`,
  `build/feature-route-cache-focused.log`, and
  `build/feature-route-cache-build.log`.
- **PASS:** CUDA-enabled development regression: **162 passed**, two skips for
  unavailable D3D12 CiG surface mapping. Final operand checks after error-status
  propagation fixes pass **12/12**. Logs:
  `build/feature-backend-cuda-regression.log` and
  `build/feature-backend-status-focused.log`.
- **PASS:** isolated C++20 `ArdaUnrealCore.dll` build and executable ABI test
  against the published core. The test crosses the actual DLL boundary and checks
  required-feature preflight, unique graph identities, and revision reuse through
  caller-owned node callbacks. It exits zero with all checks passed. Build log:
  `build/feature-unreal-abi/build-final.log`; output directory:
  `build/feature-unreal-abi/bin/RelWithDebInfo`.

## Unreal verification for e5b40a5

- **PASS:** the actual plugin dependency was updated to the published revision,
  rebuilt, and passed its shared-library ABI test, including feature preflight,
  unique identities, and callback revision reuse. The dependency checkout remains
  clean. Log: `build/feature-unreal-dependency.log`.
- **PASS:** UnrealBuildTool Development Editor build, including both plugin
  modules and the external test project. Setup also compiled the native CUDA
  CUBIN. Log: `build/feature-unreal-build.log`.
- **PASS:** real Unreal viewport GPU handoff, six completed frames, seven
  submissions of the two-node graph, 64 RGBA pixel assertions per frame. Expected
  final values are `(0.375, 0.25, 0.625, 1)`. The process exited successfully.
  Log: `build/feature-unreal-handoff.log`.
- **PASS:** `Ardashir.Unreal.ProviderContract` in Unreal automation, covering
  scope ownership, rejected unsupported states/queues, deferred replay order,
  partial replay failure, and rejected duplicate submission. The process exited
  successfully and exported its automation report. Log:
  `build/feature-unreal-contracts.log`.

An earlier isolated GPU test under `384d914` also verified 256 CUDA surface pixels
after loading the generated CUBIN through the driver API. That isolated test was
not repeated for this revision; the current viewport test above exercises the
actual Unreal/CUDA handoff.

Runtime evidence is written to the external project's `Saved` directory:

- `ArdaHandoffResult.json`: completed frames and two-node graph submissions.
- `Logs/ArdaHandoff.log`: postprocess, graph, shader, and CUDA diagnostics.
- `Logs/ArdaProviderContracts.log`: provider-contract assertions.
- `Automation/ArdaProviderContracts`: Unreal automation reports; future runs use
  separate invocation subdirectories.

## Qualification limits

This first provider qualifies graphics-queue compute and the operations exercised
by the fixture. Independent queues, graphics/ray-tracing pipelines, CUDA graph
capture/timing, CPU mapping, and shader buffer bindings are unsupported. Resource
imports require explicit supported states; `Common` is not silently mapped to
shader-read access. The test CUDA bridge supports one mapped RGBA32F surface per
batch and rejects unmatched adapters and enhanced barriers.

Arda and Unreal graphics calls append to the same borrowed Unreal list without
CPU waits. CUDA uses a separate context and nonblocking stream with explicit
D3D12/CUDA fence handoffs in both directions. Unreal's portable RHI has no CUDA
context/capture API, so that native handoff is required. Readiness polling gates
frame-slot reuse; explicit GPU idle is reserved for `OnEnginePreExit` teardown,
before graph callbacks, mappings, and plugin modules are released.
