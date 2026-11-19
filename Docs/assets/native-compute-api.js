/* Generated public source inventory for ArdaNativeCompute. */
window.ArdaNativeComputeApi = {
  "module": {
    "id": "arda-native-compute",
    "name": "ArdaNativeCompute",
    "namespace": "arda",
    "summary": "Select and record native GPU kernels or equivalent shader implementations."
  },
  "generatedFrom": "Source/ArdaInfra/ArdaNativeCompute/Public",
  "headerProvenance": [
    "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
    "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaParameters.h",
    "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
    "Source/ArdaInfra/ArdaNativeCompute/Public/NVAPI/ArdaNativeNvapiProvider.h"
  ],
  "components": [
    {
      "id": "core",
      "name": "Registration, selection, and recording",
      "page": "api-reference.html",
      "summary": "Define an operation once and prepare a route for a device and command queue."
    },
    {
      "id": "cuda",
      "name": "CUDA and CUDA in Graphics providers",
      "page": "api-reference.html",
      "summary": "Share a kernel source across CUDA launch modes and prepare it for a graphics or compute queue."
    },
    {
      "id": "nvapi",
      "name": "NVAPI provider",
      "page": "api-reference.html",
      "summary": "Register D3D12 compute shaders that call NVIDIA shader extension intrinsics."
    }
  ],
  "symbols": [
    {
      "id": "api-arda-eardanativecomputepreference-d0128340",
      "name": "EArdaNativeComputePreference",
      "qualifiedName": "arda::EArdaNativeComputePreference",
      "kind": "enum",
      "component": "core",
      "page": "api-reference.html",
      "signature": "enum class EArdaNativeComputePreference : uint8_t",
      "summary": "Choose which implementation routes the resolver may try.",
      "details": "Choose which implementation routes the resolver may try.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 14
    },
    {
      "id": "api-arda-eardanativecomputepreference-prefernative-6e8b8624",
      "name": "PreferNative",
      "qualifiedName": "arda::EArdaNativeComputePreference::PreferNative",
      "kind": "enumerator",
      "component": "core",
      "page": "api-reference.html",
      "signature": "PreferNative",
      "summary": "Try native variants in declaration order, then the shader fallback.",
      "details": "Try native variants in declaration order, then the shader fallback.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::EArdaNativeComputePreference"
      ],
      "sourceLine": 17
    },
    {
      "id": "api-arda-eardanativecomputepreference-nativeonly-1b5219dc",
      "name": "NativeOnly",
      "qualifiedName": "arda::EArdaNativeComputePreference::NativeOnly",
      "kind": "enumerator",
      "component": "core",
      "page": "api-reference.html",
      "signature": "NativeOnly",
      "summary": "Require an eligible native provider and variant.",
      "details": "Require an eligible native provider and variant.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::EArdaNativeComputePreference"
      ],
      "sourceLine": 19
    },
    {
      "id": "api-arda-eardanativecomputepreference-shaderonly-709efc2b",
      "name": "ShaderOnly",
      "qualifiedName": "arda::EArdaNativeComputePreference::ShaderOnly",
      "kind": "enumerator",
      "component": "core",
      "page": "api-reference.html",
      "signature": "ShaderOnly",
      "summary": "Use the registered shader implementation.",
      "details": "Use the registered shader implementation.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::EArdaNativeComputePreference"
      ],
      "sourceLine": 21
    },
    {
      "id": "api-arda-fardanativecomputecontext-8755b900",
      "name": "FArdaNativeComputeContext",
      "qualifiedName": "arda::FArdaNativeComputeContext",
      "kind": "struct",
      "component": "core",
      "page": "api-reference.html",
      "signature": "struct FArdaNativeComputeContext",
      "summary": "Device, queue, and route preference for support checks and preparation.",
      "details": "Device, queue, and route preference for support checks and preparation.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 25
    },
    {
      "id": "api-arda-fardanativecomputecontext-mdevice-972ba489",
      "name": "mDevice",
      "qualifiedName": "arda::FArdaNativeComputeContext::mDevice",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "FArdaRHIDeviceRef mDevice",
      "summary": "Device on which the prepared work will record.",
      "details": "Device on which the prepared work will record.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputeContext"
      ],
      "sourceLine": 28
    },
    {
      "id": "api-arda-fardanativecomputecontext-mqueue-d8ab00bd",
      "name": "mQueue",
      "qualifiedName": "arda::FArdaNativeComputeContext::mQueue",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics",
      "summary": "Queue the command list must use when it records the prepared plan.",
      "details": "Queue the command list must use when it records the prepared plan.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputeContext"
      ],
      "sourceLine": 30
    },
    {
      "id": "api-arda-fardanativecomputecontext-mpreference-c48cf7fc",
      "name": "mPreference",
      "qualifiedName": "arda::FArdaNativeComputeContext::mPreference",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "EArdaNativeComputePreference mPreference = EArdaNativeComputePreference::PreferNative",
      "summary": "Routes the resolver may try.",
      "details": "Routes the resolver may try.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputeContext"
      ],
      "sourceLine": 32
    },
    {
      "id": "api-arda-fardanativecomputecontext-mrequiredproviderid-45e7d622",
      "name": "mRequiredProviderId",
      "qualifiedName": "arda::FArdaNativeComputeContext::mRequiredProviderId",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::string mRequiredProviderId",
      "summary": "Require a particular native provider ID. Empty leaves the choice to the operand.",
      "details": "Require a particular native provider ID. Empty leaves the choice to the operand.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputeContext"
      ],
      "sourceLine": 34
    },
    {
      "id": "api-arda-fardanativecomputecontext-mallowedproviderids-37cb8f2d",
      "name": "mAllowedProviderIds",
      "qualifiedName": "arda::FArdaNativeComputeContext::mAllowedProviderIds",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::vector<eastl::string> mAllowedProviderIds",
      "summary": "Optional scheduler restriction. Admit only these provider IDs, preserving the operand's variant order.",
      "details": "Optional scheduler restriction. Admit only these provider IDs, preserving the operand's variant order.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputeContext"
      ],
      "sourceLine": 36
    },
    {
      "id": "api-arda-fardanativeresourceslot-bc618999",
      "name": "FArdaNativeResourceSlot",
      "qualifiedName": "arda::FArdaNativeResourceSlot",
      "kind": "struct",
      "component": "core",
      "page": "api-reference.html",
      "signature": "struct FArdaNativeResourceSlot",
      "summary": "All implementations of an operand consume these slots in declaration order.",
      "details": "All implementations of an operand consume these slots in declaration order.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 40
    },
    {
      "id": "api-arda-fardanativeresourceslot-mname-1b65f21e",
      "name": "mName",
      "qualifiedName": "arda::FArdaNativeResourceSlot::mName",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::string mName",
      "summary": "Nonempty name unique within the operand.",
      "details": "Nonempty name unique within the operand.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeResourceSlot"
      ],
      "sourceLine": 43
    },
    {
      "id": "api-arda-fardanativeresourceslot-mtype-b1ab8942",
      "name": "mType",
      "qualifiedName": "arda::FArdaNativeResourceSlot::mType",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "EArdaRHIResourceType mType = EArdaRHIResourceType::Buffer",
      "summary": "RHI resource kind required by this slot.",
      "details": "RHI resource kind required by this slot.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeResourceSlot"
      ],
      "sourceLine": 45
    },
    {
      "id": "api-arda-fardanativeresourceslot-maccess-e1acee09",
      "name": "mAccess",
      "qualifiedName": "arda::FArdaNativeResourceSlot::mAccess",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "EArdaComputeAccess mAccess = EArdaComputeAccess::Read",
      "summary": "Read, write, or read/write access required by every route.",
      "details": "Read, write, or read/write access required by every route.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeResourceSlot"
      ],
      "sourceLine": 47
    },
    {
      "id": "api-arda-fardanativeresourceslot-mformat-0f9ce331",
      "name": "mFormat",
      "qualifiedName": "arda::FArdaNativeResourceSlot::mFormat",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown",
      "summary": "Required texture storage format. Unknown leaves the format unconstrained.",
      "details": "Required texture storage format. Unknown leaves the format unconstrained.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeResourceSlot"
      ],
      "sourceLine": 49
    },
    {
      "id": "api-arda-fardanativeresourceview-0f854574",
      "name": "FArdaNativeResourceView",
      "qualifiedName": "arda::FArdaNativeResourceView",
      "kind": "struct",
      "component": "core",
      "page": "api-reference.html",
      "signature": "struct FArdaNativeResourceView",
      "summary": "Physical resource bound to one declared operand slot.",
      "details": "Physical resource bound to one declared operand slot.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 53
    },
    {
      "id": "api-arda-fardanativeresourceview-mresource-a8139edd",
      "name": "mResource",
      "qualifiedName": "arda::FArdaNativeResourceView::mResource",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "FArdaRHIResourceRef mResource",
      "summary": "Retained RHI resource. Its kind must match the slot.",
      "details": "Retained RHI resource. Its kind must match the slot.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeResourceView"
      ],
      "sourceLine": 56
    },
    {
      "id": "api-arda-fardanativeresourceview-mbufferrange-6ea338b8",
      "name": "mBufferRange",
      "qualifiedName": "arda::FArdaNativeResourceView::mBufferRange",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "FArdaRHIBufferRange mBufferRange",
      "summary": "Nonempty range inside a bound buffer.",
      "details": "Nonempty range inside a bound buffer.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeResourceView"
      ],
      "sourceLine": 58
    },
    {
      "id": "api-arda-fardanativeresourceview-mmiplevel-9fe0776a",
      "name": "mMipLevel",
      "qualifiedName": "arda::FArdaNativeResourceView::mMipLevel",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "uint32_t mMipLevel = 0",
      "summary": "Mip index for a bound texture.",
      "details": "Mip index for a bound texture.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeResourceView"
      ],
      "sourceLine": 60
    },
    {
      "id": "api-arda-fardanativelaunch-e57ef7db",
      "name": "FArdaNativeLaunch",
      "qualifiedName": "arda::FArdaNativeLaunch",
      "kind": "struct",
      "component": "core",
      "page": "api-reference.html",
      "signature": "struct FArdaNativeLaunch",
      "summary": "Three-dimensional group count and group size for one native dispatch.",
      "details": "Three-dimensional group count and group size for one native dispatch.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 64
    },
    {
      "id": "api-arda-fardanativelaunch-mgridsize-ccf5a35c",
      "name": "mGridSize",
      "qualifiedName": "arda::FArdaNativeLaunch::mGridSize",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "uint32_t mGridSize[3] = {1, 1, 1}",
      "summary": "Work-group counts in X, Y, and Z.",
      "details": "Work-group counts in X, Y, and Z.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeLaunch"
      ],
      "sourceLine": 67
    },
    {
      "id": "api-arda-fardanativelaunch-mblocksize-25e37fbb",
      "name": "mBlockSize",
      "qualifiedName": "arda::FArdaNativeLaunch::mBlockSize",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "uint32_t mBlockSize[3] = {1, 1, 1}",
      "summary": "Threads per group in X, Y, and Z.",
      "details": "Threads per group in X, Y, and Z.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeLaunch"
      ],
      "sourceLine": 69
    },
    {
      "id": "api-arda-fardanativelaunch-msharedmemorybytes-872d4479",
      "name": "mSharedMemoryBytes",
      "qualifiedName": "arda::FArdaNativeLaunch::mSharedMemoryBytes",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "uint32_t mSharedMemoryBytes = 0",
      "summary": "Dynamic shared memory requested per group, in bytes.",
      "details": "Dynamic shared memory requested per group, in bytes.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeLaunch"
      ],
      "sourceLine": 71
    },
    {
      "id": "api-arda-fardanativeinvocation-c4a8346d",
      "name": "FArdaNativeInvocation",
      "qualifiedName": "arda::FArdaNativeInvocation",
      "kind": "struct",
      "component": "core",
      "page": "api-reference.html",
      "signature": "struct FArdaNativeInvocation",
      "summary": "Resource bindings and parameter bytes for one invocation.",
      "details": "Resource bindings and parameter bytes for one invocation.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 75
    },
    {
      "id": "api-arda-fardanativeinvocation-mresources-9fe63af6",
      "name": "mResources",
      "qualifiedName": "arda::FArdaNativeInvocation::mResources",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::vector<FArdaNativeResourceView> mResources",
      "summary": "Physical views in the operand's slot order.",
      "details": "Physical views in the operand's slot order.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeInvocation"
      ],
      "sourceLine": 78
    },
    {
      "id": "api-arda-fardanativeinvocation-mparameters-a02d7caf",
      "name": "mParameters",
      "qualifiedName": "arda::FArdaNativeInvocation::mParameters",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::vector<uint8_t> mParameters",
      "summary": "Parameter block with exactly the operand's declared byte count.",
      "details": "Parameter block with exactly the operand's declared byte count.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeInvocation"
      ],
      "sourceLine": 80
    },
    {
      "id": "api-arda-fardanativeinvocation-mlaunch-b34f1c85",
      "name": "mLaunch",
      "qualifiedName": "arda::FArdaNativeInvocation::mLaunch",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "FArdaNativeLaunch mLaunch",
      "summary": "Launch geometry for a variant without a custom selection policy.",
      "details": "Launch geometry for a variant without a custom selection policy.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeInvocation"
      ],
      "sourceLine": 82
    },
    {
      "id": "api-arda-iardanativekernelsource-6625cdc7",
      "name": "IArdaNativeKernelSource",
      "qualifiedName": "arda::IArdaNativeKernelSource",
      "kind": "class",
      "component": "core",
      "page": "api-reference.html",
      "signature": "class IArdaNativeKernelSource",
      "summary": "Source objects own their artifact or linked entry and its provider-specific ABI.",
      "details": "Source objects own their artifact or linked entry and its provider-specific ABI.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 86
    },
    {
      "id": "api-arda-iardanativekernelsource-iardanativekernelsource-29c7a42c",
      "name": "~IArdaNativeKernelSource",
      "qualifiedName": "arda::IArdaNativeKernelSource::~IArdaNativeKernelSource",
      "kind": "destructor",
      "component": "core",
      "page": "api-reference.html",
      "signature": "virtual ~IArdaNativeKernelSource() = default",
      "summary": "Release ownership of the source artifact or linked entry.",
      "details": "Release ownership of the source artifact or linked entry.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::IArdaNativeKernelSource"
      ],
      "sourceLine": 90
    },
    {
      "id": "api-arda-iardanativekernelsource-getproviderid-9d57f12a",
      "name": "GetProviderId",
      "qualifiedName": "arda::IArdaNativeKernelSource::GetProviderId",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "virtual const char* GetProviderId() const noexcept = 0",
      "summary": "Return the registry ID of the default provider for this source.",
      "details": "Return the registry ID of the default provider for this source.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::IArdaNativeKernelSource"
      ],
      "sourceLine": 92
    },
    {
      "id": "api-arda-iardanativekernelsource-getformat-34d07e8d",
      "name": "GetFormat",
      "qualifiedName": "arda::IArdaNativeKernelSource::GetFormat",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "virtual const char* GetFormat() const noexcept = 0",
      "summary": "Return the format ID used to match the provider's supported formats.",
      "details": "Return the format ID used to match the provider's supported formats.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::IArdaNativeKernelSource"
      ],
      "sourceLine": 94
    },
    {
      "id": "api-arda-iardanativepreparedwork-c5301881",
      "name": "IArdaNativePreparedWork",
      "qualifiedName": "arda::IArdaNativePreparedWork",
      "kind": "class",
      "component": "core",
      "page": "api-reference.html",
      "signature": "class IArdaNativePreparedWork",
      "summary": "Prepared, immutable work for one implementation route.",
      "details": "Prepared, immutable work for one implementation route.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 98
    },
    {
      "id": "api-arda-iardanativepreparedwork-iardanativepreparedwork-f76c21d8",
      "name": "~IArdaNativePreparedWork",
      "qualifiedName": "arda::IArdaNativePreparedWork::~IArdaNativePreparedWork",
      "kind": "destructor",
      "component": "core",
      "page": "api-reference.html",
      "signature": "virtual ~IArdaNativePreparedWork() = default",
      "summary": "Release the captured state held by this work.",
      "details": "Release the captured state held by this work.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::IArdaNativePreparedWork"
      ],
      "sourceLine": 102
    },
    {
      "id": "api-arda-iardanativepreparedwork-record-bf1dfc61",
      "name": "Record",
      "qualifiedName": "arda::IArdaNativePreparedWork::Record",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "virtual FArdaRHIStatus Record(IArdaRHICommandList& Commands) const = 0",
      "summary": "Append work to an open command list without submitting it.",
      "details": "Append work to an open command list without submitting it.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [
        {
          "name": "Commands",
          "description": "Open list for the prepared device and queue."
        }
      ],
      "returns": "Recording status.",
      "ownership": "The RHI retains submitted dependencies through GPU completion.",
      "errors": "",
      "threading": "",
      "related": [
        "arda::IArdaNativePreparedWork"
      ],
      "sourceLine": 109
    },
    {
      "id": "api-arda-fardanativeworkref-478ca5f0",
      "name": "FArdaNativeWorkRef",
      "qualifiedName": "arda::FArdaNativeWorkRef",
      "kind": "alias",
      "component": "core",
      "page": "api-reference.html",
      "signature": "using FArdaNativeWorkRef = eastl::shared_ptr<const IArdaNativePreparedWork>",
      "summary": "Shared ownership of immutable work prepared for a specific invocation.",
      "details": "Shared ownership of immutable work prepared for a specific invocation.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 113
    },
    {
      "id": "api-arda-makeardanativework-0bc256b8",
      "name": "MakeArdaNativeWork",
      "qualifiedName": "arda::MakeArdaNativeWork",
      "kind": "function",
      "component": "core",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] FArdaNativeWorkRef MakeArdaNativeWork(eastl::function<FArdaRHIStatus(IArdaRHICommandList&)> Record)",
      "summary": "Wrap a command-list recording callback as prepared work.",
      "details": "Wrap a command-list recording callback as prepared work.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [
        {
          "name": "Record",
          "description": "Callback that appends commands. Capture pipeline state, bindings, and constants by value."
        }
      ],
      "returns": "Prepared work, or an empty reference for an empty callback.",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 119
    },
    {
      "id": "api-arda-fardanativekernelvariant-32df6848",
      "name": "FArdaNativeKernelVariant",
      "qualifiedName": "arda::FArdaNativeKernelVariant",
      "kind": "struct",
      "component": "core",
      "page": "api-reference.html",
      "signature": "struct FArdaNativeKernelVariant",
      "summary": "One native implementation and its optional launch policy.",
      "details": "One native implementation and its optional launch policy.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 122
    },
    {
      "id": "api-arda-fardanativekernelvariant-mname-fc7f6b40",
      "name": "mName",
      "qualifiedName": "arda::FArdaNativeKernelVariant::mName",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::string mName",
      "summary": "Variant name unique within the operand, returned by a native plan.",
      "details": "Variant name unique within the operand, returned by a native plan.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeKernelVariant"
      ],
      "sourceLine": 125
    },
    {
      "id": "api-arda-fardanativekernelvariant-msource-c8c9faac",
      "name": "mSource",
      "qualifiedName": "arda::FArdaNativeKernelVariant::mSource",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::shared_ptr<const IArdaNativeKernelSource> mSource",
      "summary": "Source retained by the registration. Its provider and format IDs must be nonempty.",
      "details": "Source retained by the registration. Its provider and format IDs must be nonempty.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeKernelVariant"
      ],
      "sourceLine": 127
    },
    {
      "id": "api-arda-fardanativekernelvariant-mselect-d39fb58e",
      "name": "mSelect",
      "qualifiedName": "arda::FArdaNativeKernelVariant::mSelect",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::function<TArdaRHIResult<FArdaNativeLaunch>(const FArdaNativeComputeContext&, const FArdaNativeInvocation&)> mSelect",
      "summary": "Return geometry, or Unsupported when another variant should handle this input.",
      "details": "Return geometry, or Unsupported when another variant should handle this input.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeKernelVariant"
      ],
      "sourceLine": 129
    },
    {
      "id": "api-arda-fardanativekernelvariant-mproviderid-e5813250",
      "name": "mProviderId",
      "qualifiedName": "arda::FArdaNativeKernelVariant::mProviderId",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::string mProviderId",
      "summary": "Optional provider override. Allows compatible providers to share the same compiled source and ABI.",
      "details": "Optional provider override. Allows compatible providers to share the same compiled source and ABI.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeKernelVariant"
      ],
      "sourceLine": 131
    },
    {
      "id": "api-arda-eardanativecomputeroute-21d3c2b7",
      "name": "EArdaNativeComputeRoute",
      "qualifiedName": "arda::EArdaNativeComputeRoute",
      "kind": "enum",
      "component": "core",
      "page": "api-reference.html",
      "signature": "enum class EArdaNativeComputeRoute : uint8_t",
      "summary": "Implementation selected for a prepared plan.",
      "details": "Implementation selected for a prepared plan.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 135
    },
    {
      "id": "api-arda-eardanativecomputeroute-native-54839cc2",
      "name": "Native",
      "qualifiedName": "arda::EArdaNativeComputeRoute::Native",
      "kind": "enumerator",
      "component": "core",
      "page": "api-reference.html",
      "signature": "Native",
      "summary": "Kernel or external call prepared by a registered native provider.",
      "details": "Kernel or external call prepared by a registered native provider.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::EArdaNativeComputeRoute"
      ],
      "sourceLine": 138
    },
    {
      "id": "api-arda-eardanativecomputeroute-computeshader-cf52cae7",
      "name": "ComputeShader",
      "qualifiedName": "arda::EArdaNativeComputeRoute::ComputeShader",
      "kind": "enumerator",
      "component": "core",
      "page": "api-reference.html",
      "signature": "ComputeShader",
      "summary": "Fallback that records a compute shader.",
      "details": "Fallback that records a compute shader.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::EArdaNativeComputeRoute"
      ],
      "sourceLine": 140
    },
    {
      "id": "api-arda-eardanativecomputeroute-graphicsshader-8972ab9a",
      "name": "GraphicsShader",
      "qualifiedName": "arda::EArdaNativeComputeRoute::GraphicsShader",
      "kind": "enumerator",
      "component": "core",
      "page": "api-reference.html",
      "signature": "GraphicsShader",
      "summary": "Fallback that records a graphics shader on a graphics list.",
      "details": "Fallback that records a graphics shader on a graphics list.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::EArdaNativeComputeRoute"
      ],
      "sourceLine": 142
    },
    {
      "id": "api-arda-fardanativeshaderfallback-9df605e1",
      "name": "FArdaNativeShaderFallback",
      "qualifiedName": "arda::FArdaNativeShaderFallback",
      "kind": "struct",
      "component": "core",
      "page": "api-reference.html",
      "signature": "struct FArdaNativeShaderFallback",
      "summary": "Optional shader implementation of the same operation and resource access contract.",
      "details": "Optional shader implementation of the same operation and resource access contract.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 146
    },
    {
      "id": "api-arda-fardanativeshaderfallback-mroute-708ed555",
      "name": "mRoute",
      "qualifiedName": "arda::FArdaNativeShaderFallback::mRoute",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "EArdaNativeComputeRoute mRoute = EArdaNativeComputeRoute::ComputeShader",
      "summary": "Compute or graphics shader route. A graphics route requires a graphics queue.",
      "details": "Compute or graphics shader route. A graphics route requires a graphics queue.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeShaderFallback"
      ],
      "sourceLine": 149
    },
    {
      "id": "api-arda-fardanativeshaderfallback-mprepare-a1b70070",
      "name": "mPrepare",
      "qualifiedName": "arda::FArdaNativeShaderFallback::mPrepare",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::function<TArdaRHIResult<FArdaNativeWorkRef>(const FArdaNativeComputeContext&, const FArdaNativeInvocation&)> mPrepare",
      "summary": "Freeze the resources, bindings, and arguments into work without submitting it.",
      "details": "Freeze the resources, bindings, and arguments into work without submitting it.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeShaderFallback"
      ],
      "sourceLine": 151
    },
    {
      "id": "api-arda-fardanativeshaderfallback-mrequiredfeatures-abff4798",
      "name": "mRequiredFeatures",
      "qualifiedName": "arda::FArdaNativeShaderFallback::mRequiredFeatures",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "FArdaRHIFeatureRequirements mRequiredFeatures",
      "summary": "Device features required by this shader implementation.",
      "details": "Device features required by this shader implementation.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeShaderFallback"
      ],
      "sourceLine": 153
    },
    {
      "id": "api-arda-fardanativeoperanddesc-f82ee605",
      "name": "FArdaNativeOperandDesc",
      "qualifiedName": "arda::FArdaNativeOperandDesc",
      "kind": "struct",
      "component": "core",
      "page": "api-reference.html",
      "signature": "struct FArdaNativeOperandDesc",
      "summary": "Register one resource and parameter contract with at least one implementation.",
      "details": "Register one resource and parameter contract with at least one implementation.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 157
    },
    {
      "id": "api-arda-fardanativeoperanddesc-mid-fc548502",
      "name": "mId",
      "qualifiedName": "arda::FArdaNativeOperandDesc::mId",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::string mId",
      "summary": "Nonempty ID unique in the registry.",
      "details": "Nonempty ID unique in the registry.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeOperandDesc"
      ],
      "sourceLine": 160
    },
    {
      "id": "api-arda-fardanativeoperanddesc-mresources-365ab390",
      "name": "mResources",
      "qualifiedName": "arda::FArdaNativeOperandDesc::mResources",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::vector<FArdaNativeResourceSlot> mResources",
      "summary": "Ordered resource slots shared by every implementation.",
      "details": "Ordered resource slots shared by every implementation.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeOperandDesc"
      ],
      "sourceLine": 162
    },
    {
      "id": "api-arda-fardanativeoperanddesc-mparametersize-ef2d0929",
      "name": "mParameterSize",
      "qualifiedName": "arda::FArdaNativeOperandDesc::mParameterSize",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "size_t mParameterSize = 0",
      "summary": "Exact invocation parameter size in bytes.",
      "details": "Exact invocation parameter size in bytes.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeOperandDesc"
      ],
      "sourceLine": 164
    },
    {
      "id": "api-arda-fardanativeoperanddesc-mvariants-0db21388",
      "name": "mVariants",
      "qualifiedName": "arda::FArdaNativeOperandDesc::mVariants",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::vector<FArdaNativeKernelVariant> mVariants",
      "summary": "First eligible variant wins. Different variants can use different providers.",
      "details": "First eligible variant wins. Different variants can use different providers.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeOperandDesc"
      ],
      "sourceLine": 166
    },
    {
      "id": "api-arda-fardanativeoperanddesc-mfallback-5bfb5f82",
      "name": "mFallback",
      "qualifiedName": "arda::FArdaNativeOperandDesc::mFallback",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "FArdaNativeShaderFallback mFallback",
      "summary": "Shader implementation eligible when the preference permits a fallback.",
      "details": "Shader implementation eligible when the preference permits a fallback.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeOperandDesc"
      ],
      "sourceLine": 168
    },
    {
      "id": "api-arda-fardanativeoperanddesc-mvalidate-7130ac45",
      "name": "mValidate",
      "qualifiedName": "arda::FArdaNativeOperandDesc::mValidate",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::function<FArdaRHIStatus(const FArdaNativeInvocation&)> mValidate",
      "summary": "Check algorithm invariants shared by every implementation before selection.",
      "details": "Check algorithm invariants shared by every implementation before selection.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeOperandDesc"
      ],
      "sourceLine": 170
    },
    {
      "id": "api-arda-fardanativeproviderdesc-ef67eb62",
      "name": "FArdaNativeProviderDesc",
      "qualifiedName": "arda::FArdaNativeProviderDesc",
      "kind": "struct",
      "component": "core",
      "page": "api-reference.html",
      "signature": "struct FArdaNativeProviderDesc",
      "summary": "Registry identity and compatibility boundaries of a native provider.",
      "details": "Registry identity and compatibility boundaries of a native provider.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 174
    },
    {
      "id": "api-arda-fardanativeproviderdesc-mid-5f57f531",
      "name": "mId",
      "qualifiedName": "arda::FArdaNativeProviderDesc::mId",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::string mId",
      "summary": "Nonempty provider ID unique in the registry.",
      "details": "Nonempty provider ID unique in the registry.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeProviderDesc"
      ],
      "sourceLine": 177
    },
    {
      "id": "api-arda-fardanativeproviderdesc-mgraphicsapis-61073541",
      "name": "mGraphicsApis",
      "qualifiedName": "arda::FArdaNativeProviderDesc::mGraphicsApis",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::vector<eastl::string> mGraphicsApis",
      "summary": "Registered graphics module names, such as native-vulkan.",
      "details": "Registered graphics module names, such as native-vulkan.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeProviderDesc"
      ],
      "sourceLine": 179
    },
    {
      "id": "api-arda-fardanativeproviderdesc-msourceformats-9843a5e4",
      "name": "mSourceFormats",
      "qualifiedName": "arda::FArdaNativeProviderDesc::mSourceFormats",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "eastl::vector<eastl::string> mSourceFormats",
      "summary": "Source format IDs the provider recognizes.",
      "details": "Source format IDs the provider recognizes.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeProviderDesc"
      ],
      "sourceLine": 181
    },
    {
      "id": "api-arda-eardanativehandoff-4c69eb11",
      "name": "EArdaNativeHandoff",
      "qualifiedName": "arda::EArdaNativeHandoff",
      "kind": "enum",
      "component": "core",
      "page": "api-reference.html",
      "signature": "enum class EArdaNativeHandoff : uint8_t",
      "summary": "Whether a provider can use the bound resource as supplied.",
      "details": "Whether a provider can use the bound resource as supplied.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 185
    },
    {
      "id": "api-arda-eardanativehandoff-direct-5dfa2d46",
      "name": "Direct",
      "qualifiedName": "arda::EArdaNativeHandoff::Direct",
      "kind": "enumerator",
      "component": "core",
      "page": "api-reference.html",
      "signature": "Direct",
      "summary": "The provider can access the selected view directly.",
      "details": "The provider can access the selected view directly.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::EArdaNativeHandoff"
      ],
      "sourceLine": 188
    },
    {
      "id": "api-arda-eardanativehandoff-conversionrequired-c33bd6cf",
      "name": "ConversionRequired",
      "qualifiedName": "arda::EArdaNativeHandoff::ConversionRequired",
      "kind": "enumerator",
      "component": "core",
      "page": "api-reference.html",
      "signature": "ConversionRequired",
      "summary": "Add a conversion/copy pass and bind its output to use this provider.",
      "details": "Add a conversion/copy pass and bind its output to use this provider.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::EArdaNativeHandoff"
      ],
      "sourceLine": 190
    },
    {
      "id": "api-arda-fardanativeresourcehandoff-fe3d28ca",
      "name": "FArdaNativeResourceHandoff",
      "qualifiedName": "arda::FArdaNativeResourceHandoff",
      "kind": "struct",
      "component": "core",
      "page": "api-reference.html",
      "signature": "struct FArdaNativeResourceHandoff",
      "summary": "Representation and graphics resource state that the provider expects at a handoff.",
      "details": "Representation and graphics resource state that the provider expects at a handoff.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 194
    },
    {
      "id": "api-arda-fardanativeresourcehandoff-mkind-1fb06e3d",
      "name": "mKind",
      "qualifiedName": "arda::FArdaNativeResourceHandoff::mKind",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "EArdaNativeHandoff mKind = EArdaNativeHandoff::Direct",
      "summary": "Direct access or a request for an explicit copy/conversion pass.",
      "details": "Direct access or a request for an explicit copy/conversion pass.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeResourceHandoff"
      ],
      "sourceLine": 197
    },
    {
      "id": "api-arda-fardanativeresourcehandoff-mrequiredstate-22438afc",
      "name": "mRequiredState",
      "qualifiedName": "arda::FArdaNativeResourceHandoff::mRequiredState",
      "kind": "member variable",
      "component": "core",
      "page": "api-reference.html",
      "signature": "EArdaRHIResourceState mRequiredState = EArdaRHIResourceState::Common",
      "summary": "State required on entry to a direct handoff. The provider records its native synchronization.",
      "details": "State required on entry to a direct handoff. The provider records its native synchronization.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeResourceHandoff"
      ],
      "sourceLine": 199
    },
    {
      "id": "api-arda-fardanativeroutecheck-722cfeeb",
      "name": "FArdaNativeRouteCheck",
      "qualifiedName": "arda::FArdaNativeRouteCheck",
      "kind": "alias",
      "component": "core",
      "page": "api-reference.html",
      "signature": "using FArdaNativeRouteCheck = eastl::function<FArdaRHIStatus(EArdaNativeComputeRoute, const IArdaNativeKernelSource*, const eastl::vector<EArdaRHIResourceState>&)>",
      "summary": "A scheduler may decline a route whose resource states conflict with the pass it compiled. Native routes supply the source and one required state per operand slot. Shader routes supply a null source and no native states. Return Unsupported to continue selection.",
      "details": "A scheduler may decline a route whose resource states conflict with the pass it compiled. Native routes supply the source and one required state per operand slot. Shader routes supply a null source and no native states. Return Unsupported to continue selection.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 206
    },
    {
      "id": "api-arda-iardanativecomputeprovider-4cfb5709",
      "name": "IArdaNativeComputeProvider",
      "qualifiedName": "arda::IArdaNativeComputeProvider",
      "kind": "class",
      "component": "core",
      "page": "api-reference.html",
      "signature": "class IArdaNativeComputeProvider",
      "summary": "Validate and prepare native work for one provider and its supported sources.",
      "details": "Validate and prepare native work for one provider and its supported sources.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 210
    },
    {
      "id": "api-arda-iardanativecomputeprovider-iardanativecomputeprovider-1bacca56",
      "name": "~IArdaNativeComputeProvider",
      "qualifiedName": "arda::IArdaNativeComputeProvider::~IArdaNativeComputeProvider",
      "kind": "destructor",
      "component": "core",
      "page": "api-reference.html",
      "signature": "virtual ~IArdaNativeComputeProvider() = default",
      "summary": "Release provider-owned state.",
      "details": "Release provider-owned state.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::IArdaNativeComputeProvider"
      ],
      "sourceLine": 214
    },
    {
      "id": "api-arda-iardanativecomputeprovider-getdesc-ae81ff1d",
      "name": "GetDesc",
      "qualifiedName": "arda::IArdaNativeComputeProvider::GetDesc",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "virtual const FArdaNativeProviderDesc& GetDesc() const noexcept = 0",
      "summary": "Return the provider metadata, valid for the lifetime of the provider.",
      "details": "Return the provider metadata, valid for the lifetime of the provider.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::IArdaNativeComputeProvider"
      ],
      "sourceLine": 216
    },
    {
      "id": "api-arda-iardanativecomputeprovider-checksupport-c806e25b",
      "name": "CheckSupport",
      "qualifiedName": "arda::IArdaNativeComputeProvider::CheckSupport",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "virtual FArdaRHIStatus CheckSupport(const FArdaNativeComputeContext&, const FArdaNativeOperandDesc&, const IArdaNativeKernelSource&) const = 0",
      "summary": "Check architecture, source ABI, queue and schema without allocating or recording work.",
      "details": "Check architecture, source ABI, queue and schema without allocating or recording work.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::IArdaNativeComputeProvider"
      ],
      "sourceLine": 218
    },
    {
      "id": "api-arda-iardanativecomputeprovider-queryhandoff-0a7750c9",
      "name": "QueryHandoff",
      "qualifiedName": "arda::IArdaNativeComputeProvider::QueryHandoff",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "virtual TArdaRHIResult<FArdaNativeResourceHandoff> QueryHandoff(const FArdaNativeComputeContext&, const FArdaNativeResourceSlot&, const FArdaNativeResourceView&) const = 0",
      "summary": "Inspect the physical view and report whether the caller must add a conversion pass.",
      "details": "Inspect the physical view and report whether the caller must add a conversion pass.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::IArdaNativeComputeProvider"
      ],
      "sourceLine": 221
    },
    {
      "id": "api-arda-iardanativecomputeprovider-prepare-c82d0f90",
      "name": "Prepare",
      "qualifiedName": "arda::IArdaNativeComputeProvider::Prepare",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "virtual TArdaRHIResult<FArdaNativeWorkRef> Prepare(const FArdaNativeComputeContext&, const FArdaNativeOperandDesc&, const IArdaNativeKernelSource&, const FArdaNativeInvocation&, const FArdaNativeLaunch&) const = 0",
      "summary": "Freeze arguments and resolve the source. Preparation must never enqueue work.",
      "details": "Freeze arguments and resolve the source. Preparation must never enqueue work.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::IArdaNativeComputeProvider"
      ],
      "sourceLine": 224
    },
    {
      "id": "api-arda-fardanativecomputeplan-e7e68aab",
      "name": "FArdaNativeComputePlan",
      "qualifiedName": "arda::FArdaNativeComputePlan",
      "kind": "class",
      "component": "core",
      "page": "api-reference.html",
      "signature": "class FArdaNativeComputePlan final : public IArdaRHIComputeCommand",
      "summary": "A resolved implementation and frozen work tied to its preparation device and queue.",
      "details": "A resolved implementation and frozen work tied to its preparation device and queue.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 229
    },
    {
      "id": "api-arda-fardanativecomputeplan-getroute-e00d35af",
      "name": "GetRoute",
      "qualifiedName": "arda::FArdaNativeComputePlan::GetRoute",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] EArdaNativeComputeRoute GetRoute() const noexcept",
      "summary": "Return the selected native, compute shader, or graphics shader route.",
      "details": "Return the selected native, compute shader, or graphics shader route.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputePlan"
      ],
      "sourceLine": 233
    },
    {
      "id": "api-arda-fardanativecomputeplan-getvariantname-efb38020",
      "name": "GetVariantName",
      "qualifiedName": "arda::FArdaNativeComputePlan::GetVariantName",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] const eastl::string& GetVariantName() const noexcept",
      "summary": "Return the chosen native variant name, or an empty string for a shader route.",
      "details": "Return the chosen native variant name, or an empty string for a shader route.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputePlan"
      ],
      "sourceLine": 235
    },
    {
      "id": "api-arda-fardanativecomputeplan-getwork-36c16e79",
      "name": "GetWork",
      "qualifiedName": "arda::FArdaNativeComputePlan::GetWork",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] const FArdaNativeWorkRef& GetWork() const noexcept",
      "summary": "Borrow the shared work reference for low-level integration. Copy it to retain ownership.",
      "details": "Borrow the shared work reference for low-level integration. Copy it to retain ownership.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputePlan"
      ],
      "sourceLine": 237
    },
    {
      "id": "api-arda-fardanativecomputeplan-record-d7cc26dd",
      "name": "Record",
      "qualifiedName": "arda::FArdaNativeComputePlan::Record",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] FArdaRHIStatus Record(IArdaRHICommandList& Commands) const",
      "summary": "Record the frozen work without submitting it.",
      "details": "Record the frozen work without submitting it.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [
        {
          "name": "Commands",
          "description": "Open command list on the preparation device and queue."
        }
      ],
      "returns": "Recording status.",
      "ownership": "",
      "errors": "An empty plan returns InvalidState. Another device returns WrongDevice. A different queue returns InvalidArgument.",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputePlan"
      ],
      "sourceLine": 244
    },
    {
      "id": "api-arda-fardanativecomputeregistry-9539f736",
      "name": "FArdaNativeComputeRegistry",
      "qualifiedName": "arda::FArdaNativeComputeRegistry",
      "kind": "class",
      "component": "core",
      "page": "api-reference.html",
      "signature": "class FArdaNativeComputeRegistry",
      "summary": "Own native providers and immutable operand definitions.",
      "details": "Own native providers and immutable operand definitions.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "Registry reads and insertions use a shared mutex. Callbacks can query the registry.",
      "related": [
        "arda"
      ],
      "sourceLine": 259
    },
    {
      "id": "api-arda-fardanativecomputeregistry-registerprovider-4cccd1a9",
      "name": "RegisterProvider",
      "qualifiedName": "arda::FArdaNativeComputeRegistry::RegisterProvider",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] FArdaRHIStatus RegisterProvider(eastl::shared_ptr<const IArdaNativeComputeProvider> Provider)",
      "summary": "Register a provider with a unique ID and nonempty, distinct compatibility names.",
      "details": "Register a provider with a unique ID and nonempty, distinct compatibility names.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [
        {
          "name": "Provider",
          "description": "Immutable provider to retain on success."
        }
      ],
      "returns": "Registration status.",
      "ownership": "",
      "errors": "InvalidArgument for an empty provider, invalid metadata, or duplicate ID.",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputeRegistry"
      ],
      "sourceLine": 268
    },
    {
      "id": "api-arda-fardanativecomputeregistry-registeroperand-e335ce82",
      "name": "RegisterOperand",
      "qualifiedName": "arda::FArdaNativeComputeRegistry::RegisterOperand",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] FArdaRHIStatus RegisterOperand(FArdaNativeOperandDesc Operand)",
      "summary": "Publish an operand with at least one native variant or shader implementation.",
      "details": "Publish an operand with at least one native variant or shader implementation.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [
        {
          "name": "Operand",
          "description": "Resource slots, parameter size, implementations, and optional validator."
        }
      ],
      "returns": "Registration status.",
      "ownership": "",
      "errors": "InvalidArgument for a duplicate ID or invalid operand metadata.",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputeRegistry"
      ],
      "sourceLine": 275
    },
    {
      "id": "api-arda-fardanativecomputeregistry-findprovider-43e38b75",
      "name": "FindProvider",
      "qualifiedName": "arda::FArdaNativeComputeRegistry::FindProvider",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] eastl::shared_ptr<const IArdaNativeComputeProvider> FindProvider(const char* Id) const",
      "summary": "Look up a registered provider by ID.",
      "details": "Look up a registered provider by ID.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [
        {
          "name": "Id",
          "description": "Provider registry ID."
        }
      ],
      "returns": "Shared provider, or an empty reference for an unknown or null ID.",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputeRegistry"
      ],
      "sourceLine": 281
    },
    {
      "id": "api-arda-fardanativecomputeregistry-findoperand-a2e7b912",
      "name": "FindOperand",
      "qualifiedName": "arda::FArdaNativeComputeRegistry::FindOperand",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] eastl::shared_ptr<const FArdaNativeOperandDesc> FindOperand(const char* Id) const",
      "summary": "Look up a registered operand by ID.",
      "details": "Look up a registered operand by ID.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [
        {
          "name": "Id",
          "description": "Operand registry ID."
        }
      ],
      "returns": "Shared immutable definition, or an empty reference for an unknown or null ID.",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputeRegistry"
      ],
      "sourceLine": 287
    },
    {
      "id": "api-arda-fardanativecomputeregistry-checksupport-d8bb205b",
      "name": "CheckSupport",
      "qualifiedName": "arda::FArdaNativeComputeRegistry::CheckSupport",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] FArdaRHIStatus CheckSupport(const char* Id, const FArdaNativeComputeContext& Context) const",
      "summary": "Check device, queue, and provider or shader eligibility before supplying frame inputs.",
      "details": "Check device, queue, and provider or shader eligibility before supplying frame inputs.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [
        {
          "name": "Id",
          "description": "Registered operand ID."
        },
        {
          "name": "Context",
          "description": "Device, queue, and allowed routes."
        }
      ],
      "returns": "Support status. Prepare checks actual resource handoffs and input policy.",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputeRegistry"
      ],
      "sourceLine": 294
    },
    {
      "id": "api-arda-fardanativecomputeregistry-prepare-35172c0f",
      "name": "Prepare",
      "qualifiedName": "arda::FArdaNativeComputeRegistry::Prepare",
      "kind": "method",
      "component": "core",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] TArdaRHIResult<FArdaNativeComputePlan> Prepare(const char* Id, const FArdaNativeComputeContext& Context, const FArdaNativeInvocation& Invocation, const FArdaNativeRouteCheck& CheckRoute = {}) const",
      "summary": "Validate bindings, choose an implementation, and freeze one invocation. A native Unsupported or BackendFailure can advance to another eligible variant or the shader fallback. An invalid invocation fails before any implementation runs. Preparation does not submit work.",
      "details": "Validate bindings, choose an implementation, and freeze one invocation. A native Unsupported or BackendFailure can advance to another eligible variant or the shader fallback. An invalid invocation fails before any implementation runs. Preparation does not submit work.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/ArdaNativeCompute.h",
      "params": [
        {
          "name": "Id",
          "description": "Registered operand ID."
        },
        {
          "name": "Context",
          "description": "Device, queue, and allowed routes."
        },
        {
          "name": "Invocation",
          "description": "Bindings, parameter bytes, and default launch geometry."
        },
        {
          "name": "CheckRoute",
          "description": "Optional scheduler callback run before preparing each eligible route."
        }
      ],
      "returns": "Prepared plan or failure status.",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeComputeRegistry"
      ],
      "sourceLine": 307
    },
    {
      "id": "api-arda-describeardanativecudaoperand-10b4efd6",
      "name": "DescribeArdaNativeCudaOperand",
      "qualifiedName": "arda::DescribeArdaNativeCudaOperand",
      "kind": "function",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] TArdaRHIResult<FArdaNativeOperandDesc> DescribeArdaNativeCudaOperand(const char* Id, const FArdaCudaParameterMetadata& Metadata)",
      "summary": "Start an operand with the resource slots and parameter ABI of an ARDA_CUDA_PARAMETER_STRUCT.",
      "details": "Start an operand with the resource slots and parameter ABI of an ARDA_CUDA_PARAMETER_STRUCT.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaParameters.h",
      "params": [
        {
          "name": "Id",
          "description": "Nonempty operand registry ID."
        },
        {
          "name": "Metadata",
          "description": "CUDA parameter metadata for the shared host type."
        }
      ],
      "returns": "Operand descriptor. Add variants or a shader fallback before registering it.",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 13
    },
    {
      "id": "api-arda-createardanativecudalinkedsource-77d4c6f1",
      "name": "CreateArdaNativeCudaLinkedSource",
      "qualifiedName": "arda::CreateArdaNativeCudaLinkedSource",
      "kind": "function",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] TArdaRHIResult<eastl::shared_ptr<const IArdaNativeKernelSource>> CreateArdaNativeCudaLinkedSource( const FArdaCudaParameterMetadata& Metadata, eastl::shared_ptr<const IArdaCudaKernelEntry> Entry)",
      "summary": "Bind a linked CUDA entry and derive its resource patches from the same parameter schema.",
      "details": "Bind a linked CUDA entry and derive its resource patches from the same parameter schema.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaParameters.h",
      "params": [
        {
          "name": "Metadata",
          "description": "CUDA parameter metadata used by the kernel."
        },
        {
          "name": "Entry",
          "description": "Retained linked kernel entry."
        }
      ],
      "returns": "Native source for a registered variant, or a validation error.",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 22
    },
    {
      "id": "api-arda-makeardanativecudainvocation-131fabec",
      "name": "MakeArdaNativeCudaInvocation",
      "qualifiedName": "arda::MakeArdaNativeCudaInvocation",
      "kind": "function",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "template <class Parameters> [[nodiscard]] TArdaRHIResult<FArdaNativeInvocation> MakeArdaNativeCudaInvocation(const Parameters& Host)",
      "summary": "Pack the host values and retain the active resource views for route selection.",
      "details": "Pack the host values and retain the active resource views for route selection.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaParameters.h",
      "params": [
        {
          "name": "Host",
          "description": "Host parameter object declared by ARDA_CUDA_PARAMETER_STRUCT."
        }
      ],
      "returns": "Invocation with scalar bytes and RHI views. Shader routes can use views without CUDA sharing.",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 32
    },
    {
      "id": "api-arda-ardanativecudaproviderid-dbd20dae",
      "name": "ArdaNativeCudaProviderId",
      "qualifiedName": "arda::ArdaNativeCudaProviderId",
      "kind": "constant",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "inline constexpr char ArdaNativeCudaProviderId[] = \"cuda\"",
      "summary": "Provider identity used when constraining execution to a CUDA batch.",
      "details": "Provider identity used when constraining execution to a CUDA batch.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 10
    },
    {
      "id": "api-arda-ardanativecudaingraphicsproviderid-452b1e8b",
      "name": "ArdaNativeCudaInGraphicsProviderId",
      "qualifiedName": "arda::ArdaNativeCudaInGraphicsProviderId",
      "kind": "constant",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "inline constexpr char ArdaNativeCudaInGraphicsProviderId[] = \"cuda-in-graphics\"",
      "summary": "Requires CUDA in Graphics for the active graphics API.",
      "details": "Requires CUDA in Graphics for the active graphics API.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 12
    },
    {
      "id": "api-arda-fardanativecudasourcedesc-249e3e98",
      "name": "FArdaNativeCudaSourceDesc",
      "qualifiedName": "arda::FArdaNativeCudaSourceDesc",
      "kind": "struct",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "struct FArdaNativeCudaSourceDesc",
      "summary": "Exactly one trusted linked entry, external call, module image, or CUDA C++ translation unit.",
      "details": "Exactly one trusted linked entry, external call, module image, or CUDA C++ translation unit.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 15
    },
    {
      "id": "api-arda-fardanativecudasourcedesc-mentry-27ac7521",
      "name": "mEntry",
      "qualifiedName": "arda::FArdaNativeCudaSourceDesc::mEntry",
      "kind": "member variable",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "eastl::shared_ptr<const IArdaCudaKernelEntry> mEntry",
      "summary": "Linked entry with build-profile metadata and architecture coverage.",
      "details": "Linked entry with build-profile metadata and architecture coverage.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeCudaSourceDesc"
      ],
      "sourceLine": 18
    },
    {
      "id": "api-arda-fardanativecudasourcedesc-mexternalcall-b912e2eb",
      "name": "mExternalCall",
      "qualifiedName": "arda::FArdaNativeCudaSourceDesc::mExternalCall",
      "kind": "member variable",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "eastl::shared_ptr<const IArdaCudaExternalCall> mExternalCall",
      "summary": "External library call that runs on the framework's CUDA stream.",
      "details": "External library call that runs on the framework's CUDA stream.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeCudaSourceDesc"
      ],
      "sourceLine": 20
    },
    {
      "id": "api-arda-fardanativecudasourcedesc-mmodule-02faef96",
      "name": "mModule",
      "qualifiedName": "arda::FArdaNativeCudaSourceDesc::mModule",
      "kind": "member variable",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "FArdaCudaModuleDesc mModule",
      "summary": "Owned PTX or cubin image and its target metadata.",
      "details": "Owned PTX or cubin image and its target metadata.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeCudaSourceDesc"
      ],
      "sourceLine": 22
    },
    {
      "id": "api-arda-fardanativecudasourcedesc-mcudacpp-d18eb660",
      "name": "mCudaCpp",
      "qualifiedName": "arda::FArdaNativeCudaSourceDesc::mCudaCpp",
      "kind": "member variable",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "eastl::string mCudaCpp",
      "summary": "CUDA C++ translation unit to compile with NVRTC during preparation.",
      "details": "CUDA C++ translation unit to compile with NVRTC during preparation.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeCudaSourceDesc"
      ],
      "sourceLine": 24
    },
    {
      "id": "api-arda-fardanativecudasourcedesc-mmodulekernel-7df0e61d",
      "name": "mModuleKernel",
      "qualifiedName": "arda::FArdaNativeCudaSourceDesc::mModuleKernel",
      "kind": "member variable",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "FArdaCudaModuleKernelDesc mModuleKernel",
      "summary": "ABI for a module image or NVRTC source. Use an extern \"C\" entry for a stable source symbol.",
      "details": "ABI for a module image or NVRTC source. Use an extern \"C\" entry for a stable source symbol.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeCudaSourceDesc"
      ],
      "sourceLine": 26
    },
    {
      "id": "api-arda-fardanativecudasourcedesc-mcomputecapability-86590f80",
      "name": "mComputeCapability",
      "qualifiedName": "arda::FArdaNativeCudaSourceDesc::mComputeCapability",
      "kind": "member variable",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "uint32_t mComputeCapability = 0",
      "summary": "CUDA C++ native target. Zero compiles for the selected device.",
      "details": "CUDA C++ native target. Zero compiles for the selected device.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeCudaSourceDesc"
      ],
      "sourceLine": 28
    },
    {
      "id": "api-arda-fardanativecudasourcedesc-mpatches-cfd3ae0a",
      "name": "mPatches",
      "qualifiedName": "arda::FArdaNativeCudaSourceDesc::mPatches",
      "kind": "member variable",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "eastl::vector<FArdaCudaParameterPatch> mPatches",
      "summary": "Maps neutral resource slots to CUDA parameter locations.",
      "details": "Maps neutral resource slots to CUDA parameter locations.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeCudaSourceDesc"
      ],
      "sourceLine": 30
    },
    {
      "id": "api-arda-createardanativecudaprovider-41db9033",
      "name": "CreateArdaNativeCudaProvider",
      "qualifiedName": "arda::CreateArdaNativeCudaProvider",
      "kind": "function",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] eastl::shared_ptr<const IArdaNativeComputeProvider> CreateArdaNativeCudaProvider( eastl::vector<eastl::string> GraphicsApis = {})",
      "summary": "Construct a CUDA provider for the named graphics modules.",
      "details": "Construct a CUDA provider for the named graphics modules.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [
        {
          "name": "GraphicsApis",
          "description": "Exact registered backend names. An empty list selects native-d3d12 and native-vulkan."
        }
      ],
      "returns": "Provider for devices with CUDA capabilities and CUDA command recording.",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 38
    },
    {
      "id": "api-arda-createardanativecudaingraphicsprovider-90c985c8",
      "name": "CreateArdaNativeCudaInGraphicsProvider",
      "qualifiedName": "arda::CreateArdaNativeCudaInGraphicsProvider",
      "kind": "function",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] eastl::shared_ptr<const IArdaNativeComputeProvider> CreateArdaNativeCudaInGraphicsProvider()",
      "summary": "Require D3D12 CiG on a graphics list or Vulkan CiG on a graphics/compute list.",
      "details": "Require D3D12 CiG on a graphics list or Vulkan CiG on a graphics/compute list.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [],
      "returns": "Provider for the native-d3d12 and native-vulkan modules. A context-switch device is ineligible.",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 45
    },
    {
      "id": "api-arda-addardanativecudavariants-83cbba3b",
      "name": "AddArdaNativeCudaVariants",
      "qualifiedName": "arda::AddArdaNativeCudaVariants",
      "kind": "function",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "void AddArdaNativeCudaVariants(FArdaNativeOperandDesc& Operand, FArdaNativeKernelVariant Variant)",
      "summary": "Add the same CUDA kernel and launch policy for two execution routes, trying CiG first.",
      "details": "Add the same CUDA kernel and launch policy for two execution routes, trying CiG first.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [
        {
          "name": "Operand",
          "description": "Operand that will own both variants."
        },
        {
          "name": "Variant",
          "description": "CUDA source, unique name and optional launch policy. The CiG variant adds \".cig\" to the name."
        }
      ],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 52
    },
    {
      "id": "api-arda-createardanativecudasource-c4b31319",
      "name": "CreateArdaNativeCudaSource",
      "qualifiedName": "arda::CreateArdaNativeCudaSource",
      "kind": "function",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] TArdaRHIResult<eastl::shared_ptr<const IArdaNativeKernelSource>> CreateArdaNativeCudaSource( FArdaNativeCudaSourceDesc Desc)",
      "summary": "Validate one CUDA source descriptor and keep ownership of the source.",
      "details": "Validate one CUDA source descriptor and keep ownership of the source.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [
        {
          "name": "Desc",
          "description": "Exactly one linked entry, external call, module image, or CUDA C++ translation unit, plus its patches."
        }
      ],
      "returns": "Source for a native variant or failure status.",
      "ownership": "",
      "errors": "InvalidArgument for multiple or missing source choices, embedded null bytes, or malformed CUDA argument metadata.",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 60
    },
    {
      "id": "api-arda-getardanativecudaworkplan-60fb3b87",
      "name": "GetArdaNativeCudaWorkPlan",
      "qualifiedName": "arda::GetArdaNativeCudaWorkPlan",
      "kind": "function",
      "component": "cuda",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] eastl::shared_ptr<const FArdaCudaDispatchPlan> GetArdaNativeCudaWorkPlan( const FArdaNativeWorkRef& Work)",
      "summary": "Get the frozen dispatch plan for use by a CUDA sequence scheduler.",
      "details": "Get the frozen dispatch plan for use by a CUDA sequence scheduler.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/CUDA/ArdaNativeCudaProvider.h",
      "params": [
        {
          "name": "Work",
          "description": "Work from a prepared native compute plan."
        }
      ],
      "returns": "Shared CUDA dispatch plan, or an empty reference for other work.",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 68
    },
    {
      "id": "api-arda-ardanativenvapiproviderid-0c6a4d9b",
      "name": "ArdaNativeNvapiProviderId",
      "qualifiedName": "arda::ArdaNativeNvapiProviderId",
      "kind": "constant",
      "component": "nvapi",
      "page": "api-reference.html",
      "signature": "inline constexpr char ArdaNativeNvapiProviderId[] = \"nvapi\"",
      "summary": "Registry ID for native D3D12 NVAPI shader extensions.",
      "details": "Registry ID for native D3D12 NVAPI shader extensions.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/NVAPI/ArdaNativeNvapiProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 9
    },
    {
      "id": "api-arda-fardanativenvapisourcedesc-b5d5a5d0",
      "name": "FArdaNativeNvapiSourceDesc",
      "qualifiedName": "arda::FArdaNativeNvapiSourceDesc",
      "kind": "struct",
      "component": "nvapi",
      "page": "api-reference.html",
      "signature": "struct FArdaNativeNvapiSourceDesc",
      "summary": "Compile HLSL with DXC and supply the resulting compute shader container.",
      "details": "Compile HLSL with DXC and supply the resulting compute shader container.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/NVAPI/ArdaNativeNvapiProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 12
    },
    {
      "id": "api-arda-fardanativenvapisourcedesc-mdxil-f67142ab",
      "name": "mDxil",
      "qualifiedName": "arda::FArdaNativeNvapiSourceDesc::mDxil",
      "kind": "member variable",
      "component": "nvapi",
      "page": "api-reference.html",
      "signature": "eastl::vector<uint8_t> mDxil",
      "summary": "Owned DXIL compiled with the NVIDIA nvHLSLExtns.h shader header.",
      "details": "Owned DXIL compiled with the NVIDIA nvHLSLExtns.h shader header.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/NVAPI/ArdaNativeNvapiProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeNvapiSourceDesc"
      ],
      "sourceLine": 15
    },
    {
      "id": "api-arda-fardanativenvapisourcedesc-mrequiredopcodes-d0a7cda9",
      "name": "mRequiredOpcodes",
      "qualifiedName": "arda::FArdaNativeNvapiSourceDesc::mRequiredOpcodes",
      "kind": "member variable",
      "component": "nvapi",
      "page": "api-reference.html",
      "signature": "eastl::vector<uint32_t> mRequiredOpcodes",
      "summary": "Required NV_EXTN_OP_* numbers from nvShaderExtnEnums.h.",
      "details": "Required NV_EXTN_OP_* numbers from nvShaderExtnEnums.h.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/NVAPI/ArdaNativeNvapiProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeNvapiSourceDesc"
      ],
      "sourceLine": 17
    },
    {
      "id": "api-arda-fardanativenvapisourcedesc-mextensionuavslot-f6f49b84",
      "name": "mExtensionUavSlot",
      "qualifiedName": "arda::FArdaNativeNvapiSourceDesc::mExtensionUavSlot",
      "kind": "member variable",
      "component": "nvapi",
      "page": "api-reference.html",
      "signature": "uint32_t mExtensionUavSlot = 0",
      "summary": "The u register declared by NV_SHADER_EXTN_SLOT in HLSL.",
      "details": "The u register declared by NV_SHADER_EXTN_SLOT in HLSL.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/NVAPI/ArdaNativeNvapiProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeNvapiSourceDesc"
      ],
      "sourceLine": 19
    },
    {
      "id": "api-arda-fardanativenvapisourcedesc-mextensionregisterspace-26840261",
      "name": "mExtensionRegisterSpace",
      "qualifiedName": "arda::FArdaNativeNvapiSourceDesc::mExtensionRegisterSpace",
      "kind": "member variable",
      "component": "nvapi",
      "page": "api-reference.html",
      "signature": "uint32_t mExtensionRegisterSpace = 1",
      "summary": "The nonzero space declared by NV_SHADER_EXTN_REGISTER_SPACE in HLSL. Space zero holds operand resources.",
      "details": "The nonzero space declared by NV_SHADER_EXTN_REGISTER_SPACE in HLSL. Space zero holds operand resources.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/NVAPI/ArdaNativeNvapiProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda::FArdaNativeNvapiSourceDesc"
      ],
      "sourceLine": 21
    },
    {
      "id": "api-arda-createardanativenvapisource-9d14c5f4",
      "name": "CreateArdaNativeNvapiSource",
      "qualifiedName": "arda::CreateArdaNativeNvapiSource",
      "kind": "function",
      "component": "nvapi",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] TArdaRHIResult<eastl::shared_ptr<const IArdaNativeKernelSource>> CreateArdaNativeNvapiSource(FArdaNativeNvapiSourceDesc Desc)",
      "summary": "Retain an NVAPI shader and its driver requirements. In space zero, operand slot i maps to ti for reads or ui for writes and read/write access. Buffers use ByteAddressBuffer views. Parameters are 32-bit root constants at b0. The compiled shader fixes numthreads.",
      "details": "Retain an NVAPI shader and its driver requirements. In space zero, operand slot i maps to ti for reads or ui for writes and read/write access. Buffers use ByteAddressBuffer views. Parameters are 32-bit root constants at b0. The compiled shader fixes numthreads.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/NVAPI/ArdaNativeNvapiProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 30
    },
    {
      "id": "api-arda-createardanativenvapiprovider-99703ff8",
      "name": "CreateArdaNativeNvapiProvider",
      "qualifiedName": "arda::CreateArdaNativeNvapiProvider",
      "kind": "function",
      "component": "nvapi",
      "page": "api-reference.html",
      "signature": "[[nodiscard]] eastl::shared_ptr<const IArdaNativeComputeProvider> CreateArdaNativeNvapiProvider()",
      "summary": "Register the \"nvapi\" provider for \"native-d3d12\". Opcode support is checked on the selected device.",
      "details": "Register the \"nvapi\" provider for \"native-d3d12\". Opcode support is checked on the selected device.",
      "source": "Source/ArdaInfra/ArdaNativeCompute/Public/NVAPI/ArdaNativeNvapiProvider.h",
      "params": [],
      "returns": "",
      "ownership": "",
      "errors": "",
      "threading": "",
      "related": [
        "arda"
      ],
      "sourceLine": 33
    }
  ]
};
