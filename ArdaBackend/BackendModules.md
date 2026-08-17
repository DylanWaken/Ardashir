# ArdaBackend provider modules

ArdaBackend is the project-owned declaration layer. It no longer selects or
constructs NVRHI devices directly. A linked provider module supplies the
implementation behind the public RHI, presentation, external-device, native
resource, and shader-target contracts.

## Application-facing selection

Applications continue to include `ArdaBackend.h` and use `arda::rhi` resources.
They can select a specific implementation by its stable module name:

```cpp
using namespace arda::backend;

FArdaBackendConfiguration Configuration;
Configuration.mBackendName = "nvrhi-vulkan";
Configuration.mBackend = EArdaBackendType::Vulkan;
ConfigureBackend(Configuration);
InitializeBackend();
```

`ConfigureBackend(EArdaBackendType)` remains supported. An empty
`mBackendName` selects the highest-priority linked provider compatible with the
requested API. `EnumerateBackendModules()` exposes the choices for tools and
launchers. The shipped sample modules are `nvrhi-vulkan` and, on Windows,
`nvrhi-d3d12`.

## Provider contract

A provider library implements `IArdaBackendModule` from
`ArdaBackendProvider.h`. Its descriptor declares a globally stable name,
legacy/API compatibility class, shader binary family and extension, supported
device sources, and default-selection priority.

The module must:

1. Return a stable `FArdaBackendModuleDescriptor`.
2. Allocate an `IArdaBackendDevice` for every advertised device source.
3. Return an `IArdaRHIDevice` facade implementing the full public Arda RHI.
4. Implement swap-chain creation if it provides presentation.
5. Accept or rewrite `FArdaBackendShaderCompileInvocation`. A
   `BackendDefined` shader format either supplies a compatible process command
   or handles `InvokeShaderCompiler` through its engine/toolchain service.
6. Interpret `BackendDefined` native resource descriptors it advertises.
7. Retain external device/resource lifetime tokens while native objects can be used.

Registration is explicit and deterministic:

```cpp
bool RegisterMyBackend()
{
    static FMyBackendModule Module;
    return arda::backend::RegisterBackendModule(Module);
}
```

Static libraries should expose a registration entry point and ensure the final
link references it. Ardashir's built-in linkage shim does this for the NVRHI
sample libraries so static-link dead stripping cannot remove their registrars.
Shared-library hosts may call the same entry point after loading a module.

## External devices

`IArdaExternalDeviceProvider::GetExternalDeviceDesc` is the universal path.
Standard native APIs use `mInstance`, `mAdapter`, `mDevice`, and `mQueues`.
Engine integrations use stable entries in `mAdditionalObjects`; for example an
Unreal provider can expose its `IDynamicRHI` and RHI device objects under names
documented by the Unreal backend module. `mBackendData` carries copied,
immutable module-specific metadata without exposing engine headers publicly.

The D3D12 and Vulkan descriptor methods remain as strongly described
compatibility paths. The NVRHI sample adapter also translates universal
`d3d12` and `vulkan` descriptors into its native creation descriptors.

## External resources

Resource providers still resolve stable IDs to
`FArdaRHINativeTextureImportDesc` or `FArdaRHINativeBufferImportDesc`. Native
D3D12 and Vulkan types remain standardized. Other RHIs use
`EArdaRHINativeResourceType::BackendDefined`, a stable `mNativeTypeName`, and
copied `mBackendData`. The active `IArdaRHIDevice` validates and imports that
payload; ArdaBackend core never casts the handle.

## Build selection

The sample implementations are separate targets:

- `Ardashir::ArdaBackendNvrhiVulkan`
- `Ardashir::ArdaBackendNvrhiD3D12`
- `Ardashir::ArdaBackendNvrhiCommon`

Control their linkage with `ARDASHIR_BACKEND_NVRHI_VULKAN` and
`ARDASHIR_BACKEND_NVRHI_D3D12`. Turning both off removes NVRHI from the
ArdaBackend core dependency graph, allowing a host to link and register only
its own provider library. A custom provider must use the same C++ runtime and
compatible compiler ABI as ArdaBackend; the provider surface is a C++ module
contract, not a stable C ABI for arbitrary prebuilt binaries.

An in-tree or parent-project provider can participate in the same dead-strip-safe
linkage without editing ArdaBackend sources:

```sh
cmake -S . -B build \
  -DARDASHIR_BACKEND_MODULE_TARGETS=MyEngineBackend \
  -DARDASHIR_BACKEND_REGISTRATION_FUNCTIONS=RegisterMyEngineBackend
```

Every listed registration function must be an unqualified function in
`arda::backend`, and the listed targets must already exist when the
`Source/ArdaBackend` directory is configured. Multiple entries are separated by
semicolons.
