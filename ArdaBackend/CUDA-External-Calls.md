# External CUDA calls

Compute operands can select an external CUDA operation, including cuBLAS and cuDNN
functions, through the same API used for a compiled kernel. An external operation
may enqueue several kernels. It works with `Dispatch`, `DispatchDeferred`,
`RegisterArdaCudaOperandNode`, `FArdaCudaSequence`, and `FArdaDependencyGraph`. Mixing library calls
and compiled kernels in one sequence preserves one native batch and one stream.

The framework supplies the CUDA context, stream, typed resource bindings and
completion lifetime. The application supplies its library dependency and adapter;
ArdaBackend does not link or distribute cuBLAS or cuDNN. Include their headers only
in the adapter's implementation and link the application against the matching
library. The public framework headers remain usable without the CUDA SDK.

## Binding and execution contract

Include `Compute/ArdaCudaExternalCall.h`. Derive an adapter from
`TArdaCudaExternalCall<FParameters>`, where `FParameters` is an ordinary
`ARDA_CUDA_PARAMETER_STRUCT` schema. Implement:

```cpp
TArdaRHIResult<eastl::unique_ptr<IArdaCudaPreparedCall>> PrepareCall(
    const FArdaCudaExternalCallContext& Context,
    const FParameters::FCuda& Parameters) const override;
```

`Context` exposes opaque `mContext` and `mStream` handles, `mLaunchMode`,
`mComputeCapability`, and optional factory-specific `mState`. The provider makes
this context current before calling the adapter. `Parameters` contains copied
values and resolved addresses, including
declared buffer-view offsets. Store any needed values in the returned prepared
call; the reference itself is borrowed. Its `Enqueue(void* Stream)` method issues
library work on the supplied stream. The prepared call's destructor runs with the
same CUDA context current after completion, or during cleanup of a failed or
discarded recording.

There are two different preparation stages:

1. `BindKernelVariants`, `SelectKernel`, and operand `PrepareDispatch` operate on
   host metadata without calling CUDA. Binding retains the adapter; the plan freezes
   values, resources and selection.
2. Native recording resolves addresses and calls the adapter's `PrepareCall` for
   every operation on a retained-graph cache miss or uncaptured dispatch, before
   capture or enqueue begins. This is the place to create
   context-specific handles, validate library support, configure descriptors, and
   prepare plans. Do not read or write graph inputs here: earlier graphics work may
   still be pending. `Enqueue` runs at submission, during CUDA Graph construction,
   or inside D3D12 CiG capture. A retained graph hit bypasses both hooks and keeps
   the original prepared state alive. Adapters must explicitly opt into this via
   `SupportsGraphCapture`; see [retained CUDA Graphs](CUDA-Sequences.md#retained-cuda-graphs).

The registered variant is selected through the existing operand hooks:

```cpp
// In the operand's BindKernelVariants(FRegistry& Registry):
eastl::shared_ptr<const IArdaCudaExternalCall> Call =
    eastl::make_shared<FArdaAxpyCall>();
Registry.Add(BindArdaCudaExternalCall("cublas.saxpy", uint32_t{0}, Call));

// In SelectKernel, after checking the requested shapes and resource-view sizes:
FArdaCudaKernelSelection Selection;
Selection.mVariantId = Candidates.front().mId;
return {Selection, {}};
```

The example uses `uint32_t` as the operand's variant payload. External variants
require the exact same typed parameter signature as the operand. They do not
require an Arda-compiled binary manifest, block/grid dimensions, or compiled-kernel
limits. Optional `FArdaCudaKernelRequirements` still restrict compute capability
and launch mode. At the low-level descriptor boundary, exactly one of `mEntry` and
`mExternalCall` must be set.

## cuBLAS adapter

This adapter computes `Y = Alpha * X + Y` for contiguous FP32 vectors. Its schema
declares X as read-only, Y as read/write, and workspace as read/write. Host selection
must verify that the declared X/Y views contain at least `mCount * sizeof(float)`
bytes and that the workspace view contains at least `mWorkspaceBytes`. Return the
operand's explicit `NoWork` selection for a mathematically empty operation.

```cpp
#include "Compute/ArdaCudaExternalCall.h"
#include <cublas_v2.h>

namespace arda
{
#define ARDA_AXPY_FIELDS(VALUE, BUFFER, SURFACE)                  \
    VALUE(int32_t, mCount)                                      \
    VALUE(float, mAlpha)                                        \
    VALUE(uint64_t, mWorkspaceBytes)                            \
    BUFFER(float, mX, EArdaComputeAccess::Read)                  \
    BUFFER(float, mY, EArdaComputeAccess::ReadWrite)             \
    BUFFER(uint8_t, mWorkspace, EArdaComputeAccess::ReadWrite)
ARDA_CUDA_PARAMETER_STRUCT(FArdaAxpyParameters, ARDA_AXPY_FIELDS)
#undef ARDA_AXPY_FIELDS

static FArdaRHIStatus CheckBlas(cublasStatus_t Status)
{
    return Status == CUBLAS_STATUS_SUCCESS ? FArdaRHIStatus{} :
        FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
            cublasGetStatusString(Status));
}

class FArdaAxpyPrepared final : public IArdaCudaPreparedCall
{
public:
    explicit FArdaAxpyPrepared(FArdaAxpyParameters::FCuda Parameters)
        : mParameters(Parameters) {}

    ~FArdaAxpyPrepared() override
    {
        if (mHandle)
        {
            cublasDestroy(mHandle);
        }
    }

    FArdaRHIStatus Enqueue(void* Stream) override
    {
        // Setting a stream resets cuBLAS workspace, even for the same stream.
        if (auto S = CheckBlas(cublasSetStream(mHandle,
                static_cast<cudaStream_t>(Stream))); !S)
        {
            return S;
        }
        if (auto S = CheckBlas(cublasSetWorkspace(mHandle,
                mParameters.mWorkspace, size_t(mParameters.mWorkspaceBytes))); !S)
        {
            return S;
        }
        return CheckBlas(cublasSaxpy(mHandle, mParameters.mCount,
            &mParameters.mAlpha, mParameters.mX, 1, mParameters.mY, 1));
    }

    cublasHandle_t mHandle = nullptr;
    FArdaAxpyParameters::FCuda mParameters;
};

class FArdaAxpyCall final : public TArdaCudaExternalCall<FArdaAxpyParameters>
{
public:
    TArdaRHIResult<eastl::unique_ptr<IArdaCudaPreparedCall>> PrepareCall(
        const FArdaCudaExternalCallContext&,
        const FArdaAxpyParameters::FCuda& Parameters) const override
    {
        if (Parameters.mCount <= 0 || !Parameters.mX || !Parameters.mY ||
            !Parameters.mWorkspace || !Parameters.mWorkspaceBytes ||
            reinterpret_cast<uintptr_t>(Parameters.mWorkspace) % 256)
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                "SAXPY requires positive count and a 256-byte-aligned workspace.")};
        }
        eastl::unique_ptr<IArdaCudaPreparedCall> Prepared(
            new FArdaAxpyPrepared(Parameters));
        auto& Call = static_cast<FArdaAxpyPrepared&>(*Prepared);
        if (auto S = CheckBlas(cublasCreate(&Call.mHandle)); !S)
        {
            return {{}, S};
        }
        if (auto S = CheckBlas(cublasSetPointerMode(Call.mHandle,
                CUBLAS_POINTER_MODE_HOST)); !S)
        {
            return {{}, S};
        }
        return {eastl::move(Prepared), {}};
    }
};
}
```

Use CUDA-shared RHI buffers for X, Y and workspace, then dispatch this operand
normally or add it between compiled-kernel operands in a sequence. cuBLAS requires
handles to belong to the current CUDA context. The explicit stream and workspace
ordering above follows its API contract. Workspace capacity is algorithm-dependent;
insufficient workspace can fail or reduce performance. [cuBLAS context](https://docs.nvidia.com/cuda/cublas/index.html#cublas-context),
[stream and workspace](https://docs.nvidia.com/cuda/cublas/index.html#cublassetstream)

The example owns one handle per prepared call to make isolation and cleanup clear.
This is a simple adapter, with potentially expensive setup and retirement:
`cublasDestroy` synchronizes. For frequent submissions, use the optional context
state described next to lease and reuse handles. Never concurrently reconfigure a
leased handle or reuse its workspace for overlapping submissions.
[cuBLAS lifetime and threading](https://docs.nvidia.com/cuda/cublas/index.html#thread-safety)

## Reusing handles and plans

Override the factory's optional `CreateContextState` to return a class derived from
`IArdaCudaExternalCallState`. The default returns null successfully, so the simple
adapters do not create cached state. A non-null state is cached per factory and CUDA
context, with the factory retained. The provider supplies it in `Context.mState`
when preparing subsequent calls and destroys it with that context current before
context teardown. Keep the same factory instance across recordings to reuse its
state. Factories remain host-only; native handles and plans belong to this state
or a prepared call.

For example, a state type can own a free list of cuBLAS handles:

```cpp
class FArdaBlasPool final : public IArdaCudaExternalCallState
{
public:
    ~FArdaBlasPool() override
    {
        for (auto Handle : mAvailable)
        {
            cublasDestroy(Handle);
        }
    }

    TArdaRHIResult<cublasHandle_t> Acquire()
    {
        if (!mAvailable.empty())
        {
            auto Handle = mAvailable.back();
            mAvailable.pop_back();
            return {Handle, {}};
        }
        // Reserve a return slot before creating another lease.
        mAvailable.reserve(mAvailable.capacity() + 1);
        cublasHandle_t Handle = nullptr;
        auto Status = CheckBlas(cublasCreate(&Handle));
        return {Handle, Status};
    }

    void Release(cublasHandle_t Handle) noexcept { mAvailable.push_back(Handle); }
    eastl::vector<cublasHandle_t> mAvailable;
};

// Override in the external-call factory:
TArdaRHIResult<eastl::unique_ptr<IArdaCudaExternalCallState>> CreateContextState(
    const FArdaCudaExternalCallContext&) const override
{
    return {eastl::unique_ptr<IArdaCudaExternalCallState>(new FArdaBlasPool), {}};
}
```

In `PrepareCall`, cast the factory's `Context.mState` to `FArdaBlasPool`, acquire a
handle, and store both in the prepared call. Replace that prepared call's
`cublasDestroy` with returning its handle to the pool. The state outlives every
prepared call. Reapply pointer mode and any other handle configuration needed by
each lease; bind stream followed by workspace at enqueue as before. The same pattern
can retain cuDNN handles and compatible prepared plans.

Provider calls into a context state are serialized by its context mutex. GPU
submissions can remain outstanding concurrently, so CPU serialization alone does
not make one workspace or handle safe to reuse: keep each lease exclusive until
its prepared-call destructor runs. Pool creation, acquisition and cleanup must not
re-enter the RHI or switch the borrowed CUDA context. Resources exposed through RHI
remain declared in the operand schema even when native library state is cached.

## cuDNN adapter

The same operation can use `cudnnAddTensor`: with an FP32 descriptor covering
`[1, 1, 1, mCount]`, alpha is `mAlpha` and beta is one. This descriptor-based example
needs no workspace. It creates and configures a handle and descriptor in
`PrepareCall`, then binds the provider's stream in `Enqueue`. `cudnnAddTensor` is a
legacy API deprecated in cuDNN 9; it is shown for existing integrations. For new
cuDNN workloads, use the execution-plan pattern following the example.
[cuDNN API overview](https://docs.nvidia.com/deeplearning/cudnn/backend/latest/api/overview.html)

```cpp
#include "Compute/ArdaCudaExternalCall.h"
#include <cudnn.h>

namespace arda
{
#define ARDA_DNN_ADD_FIELDS(VALUE, BUFFER, SURFACE)               \
    VALUE(int32_t, mCount)                                      \
    VALUE(float, mAlpha)                                        \
    BUFFER(float, mX, EArdaComputeAccess::Read)                  \
    BUFFER(float, mY, EArdaComputeAccess::ReadWrite)
ARDA_CUDA_PARAMETER_STRUCT(FArdaDnnParameters, ARDA_DNN_ADD_FIELDS)
#undef ARDA_DNN_ADD_FIELDS

static FArdaRHIStatus CheckDnn(cudnnStatus_t Status)
{
    return Status == CUDNN_STATUS_SUCCESS ? FArdaRHIStatus{} :
        FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure,
            cudnnGetErrorString(Status));
}

class FArdaDnnAddPrepared final : public IArdaCudaPreparedCall
{
public:
    explicit FArdaDnnAddPrepared(FArdaDnnParameters::FCuda Parameters)
        : mParameters(Parameters) {}

    ~FArdaDnnAddPrepared() override
    {
        if (mTensor) { cudnnDestroyTensorDescriptor(mTensor); }
        if (mHandle) { cudnnDestroy(mHandle); }
    }

    FArdaRHIStatus Enqueue(void* Stream) override
    {
        if (auto S = CheckDnn(cudnnSetStream(mHandle,
                static_cast<cudaStream_t>(Stream))); !S)
        {
            return S;
        }
        const float Beta = 1.0f;
        return CheckDnn(cudnnAddTensor(mHandle, &mParameters.mAlpha,
            mTensor, mParameters.mX, &Beta, mTensor, mParameters.mY));
    }

    cudnnHandle_t mHandle = nullptr;
    cudnnTensorDescriptor_t mTensor = nullptr;
    FArdaDnnParameters::FCuda mParameters;
};

class FArdaDnnAddCall final : public TArdaCudaExternalCall<FArdaDnnParameters>
{
public:
    TArdaRHIResult<eastl::unique_ptr<IArdaCudaPreparedCall>> PrepareCall(
        const FArdaCudaExternalCallContext&,
        const FArdaDnnParameters::FCuda& Parameters) const override
    {
        if (Parameters.mCount <= 0 || !Parameters.mX || !Parameters.mY)
        {
            return {{}, FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
                "cuDNN add requires positive count and both tensor views.")};
        }
        eastl::unique_ptr<IArdaCudaPreparedCall> Prepared(
            new FArdaDnnAddPrepared(Parameters));
        auto& Call = static_cast<FArdaDnnAddPrepared&>(*Prepared);
        if (auto S = CheckDnn(cudnnCreate(&Call.mHandle)); !S)
        {
            return {{}, S};
        }
        if (auto S = CheckDnn(cudnnCreateTensorDescriptor(&Call.mTensor)); !S)
        {
            return {{}, S};
        }
        if (auto S = CheckDnn(cudnnSetTensor4dDescriptor(Call.mTensor,
                CUDNN_TENSOR_NCHW, CUDNN_DATA_FLOAT, 1, 1, 1,
                Parameters.mCount)); !S)
        {
            return {{}, S};
        }
        return {eastl::move(Prepared), {}};
    }
};
}
```

Bind `FArdaDnnAddCall` to an operand over `FArdaDnnParameters` with
`BindArdaCudaExternalCall`, as in the cuBLAS example. Host selection checks positive
supported tensor dimensions, FP32 view capacity, and the operation's aliasing rules.
Use a separate prepared state or exclusive lease for every concurrent invocation;
cuDNN does not permit simultaneous use of one handle by several host threads.
`cudnnSetStream` orders the call with other work in that stream, including when
cuDNN uses internal streams. [cuDNN stream API](https://docs.nvidia.com/deeplearning/cudnn/backend/latest/api/cudnn-graph-library.html#cudnnsetstream),
[thread safety](https://docs.nvidia.com/deeplearning/cudnn/latest/developer/misc.html#thread-safety)

For convolution, attention, or another cuDNN execution plan, replace this prepared
state with one owning the plan and its variant pack. Finalize the plan before
enqueue, query `CUDNN_ATTR_EXECUTION_PLAN_WORKSPACE_SIZE`, and validate a declared
read/write workspace view against that size. Put its resolved pointer and the
declared input/output pointers in the variant pack. The enqueue body is then:

```cpp
if (auto S = CheckDnn(cudnnSetStream(mHandle,
        static_cast<cudaStream_t>(Stream))); !S)
{
    return S;
}
return CheckDnn(cudnnBackendExecute(mHandle, mExecutionPlan, mVariantPack));
```

Plan authoring and algorithm choice remain application-specific. All tensor data
and scratch referenced by the variant pack must remain valid through GPU completion.
The prepared object can retain descriptors/plans; RHI buffers stay declared in the
operand schema so the render graph sees their accesses. [cuDNN graph execution](https://docs.nvidia.com/deeplearning/cudnn/backend/latest/api/cudnn-graph-library.html#cudnnbackendexecute)

## Execution modes and limitations

`IArdaCudaExternalCall::GetSupport` defaults to ordinary CUDA and Vulkan CiG.
D3D12 CiG requires an adapter override admitting that mode after its exact library,
operation, algorithm and workspace policy have been qualified. A selected external
call does not silently change the device's execution mode. Request a ContextSwitch
device when a required adapter does not support the selected graphics-queue mode.

CUDA Graph capture support and D3D12 CiG support are different contracts. CiG capture
forbids host callbacks, stream synchronization, nested capture, and asynchronous
allocation/free, including indirectly through a graph launch. Enqueue must preserve
the supplied stream and context, use asynchronous library operations, and avoid
re-entering Arda recording/submission APIs. Internal library streams must rejoin the
supplied stream's dependency chain and obey the selected mode. [CUDA CiG restrictions](https://docs.nvidia.com/cuda/cuda-driver-api/group__CUDA__STREAM.html)

cuBLAS supports graphics-context shared-memory limits from version 12.8, but its
operations that rely on `cudaMallocAsync` need user-provided workspace in CiG.
Host-result routines that synchronize are also unsuitable for capture. [cuBLAS capture support](https://docs.nvidia.com/cuda/cublas/index.html#cuda-graphs-support)

Do not assume all cuDNN engines support CiG. Preload needed cuDNN sublibraries and
complete any required plan/code warmup before capture. NVIDIA notes that resource
queries can require warmup for graph capture; such warmup must use separately valid
data, outside native preparation of a graph whose inputs have not executed yet.
[cuDNN capture guidance](https://docs.nvidia.com/deeplearning/cudnn/backend/v9.16.0/release-notes.html)

An adapter preparation error prevents every operation in that native batch from
enqueuing. An error returned after enqueue starts can leave partial GPU work; the
framework cannot roll back an arbitrary library call. Return meaningful library
errors as `FArdaRHIStatus`, and never report success after enqueueing work on an
untracked stream. The framework retains prepared state for cleanup, but cannot
verify hidden library resource accesses, tensor sizes, or algorithm safety.

Provider interface version **7** introduced the external-call descriptors and
lifecycle. The current interface version is **12**, including retained CUDA Graph
metadata, submission completion, allocation residency queries and queue-qualified
timestamp capabilities. Rebuild external providers and consumers together. See
[CUDA texture buffers](CUDA-Texture-Buffers.md) for explicit graphics texture staging
into declared linear buffers suitable for library adapters.
