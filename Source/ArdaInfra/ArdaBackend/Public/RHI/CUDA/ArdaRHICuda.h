/** @file ArdaRHICuda.h
 * CUDA resource bindings, compiled kernel entries and launch validation.
 */
#pragma once

#include "RHI/Resources/ArdaRHIBuffer.h"
#include "RHI/Resources/ArdaRHITexture.h"
#include "RHI/Config/ArdaCudaConfig.h"
#include "RHI/Context/ArdaCudaExternalCallContext.h"
#include "RHI/Scheduling/ArdaCudaGraph.h"
#include <EASTL/unique_ptr.h>
#include <typeinfo>

namespace arda
{
	/** Declared resource access used to order a CUDA dispatch with graphics work. */
	enum class EArdaComputeAccess : uint8_t
	{
		/** The kernel only loads from the binding. */
		Read,
		/** The kernel stores to the binding without consuming its previous contents. */
		Write,
		/** The kernel may both load and store, including in-place updates. */
		ReadWrite
	};
	/** Shader-independent kind of a CUDA resource binding. */
	enum class EArdaComputeBindingType : uint8_t
	{
		/** A byte range of an IArdaRHIBuffer. */
		Buffer,
		/** One texture mip exposed as a CUDA surface, including its layers/depth. */
		Surface
	};
	/** Raw channel interpretation; normalization and color conversion are not implicit. */
	enum class EArdaCudaScalarType : uint8_t
	{
		/** Unsigned integer channel. */
		UInt,
		/** Signed integer channel. */
		SInt,
		/** IEEE floating-point channel. */
		Float
	};

	/** Native channel layout for a CUDA-compatible storage format. */
	struct FArdaCudaFormatInfo
	{
		/** Arithmetic interpretation of each stored channel. */
		EArdaCudaScalarType mScalarType = EArdaCudaScalarType::UInt;
		/** Bits per channel, not bits per pixel. */
		uint32_t mBits = 0;
		/** Channel count; zero means the RHI format cannot be used as a CUDA surface. */
		uint32_t mChannels = 0;
	};

	/** Returns the shared format admission table entry; unsupported formats have zero channels. */
	[[nodiscard]] FArdaCudaFormatInfo GetArdaCudaFormatInfo(EArdaRHIFormat Format) noexcept;

	/** A retained resource view; CUDA addresses remain private to the native provider. */
	struct FArdaCudaBinding
	{
		/** Retains the source buffer or texture; it must belong to the command-list device. */
		FArdaRHIResourceRef mResource;
		/** Access declaration supplied by the dispatch implementation, matching its parameter contract. */
		EArdaComputeAccess mAccess = EArdaComputeAccess::Read;
		/** Buffer-only byte range; the byte offset is incorporated into the kernel address. */
		FArdaRHIBufferRange mBufferRange;
		/** Texture-only mip index. All layers or depth slices of that mip remain addressable. */
		uint32_t mMipLevel = 0;
	};

	/** Native binary target emitted by the build; accelerated targets require an exact match. */
	struct FArdaCudaArchitecture
	{
		/** Major times ten plus minor. */
		uint32_t mComputeCapability = 0;
		/** Architecture/family-specific code is admitted only on its exact build target. */
		bool mbExact = false;

		/** Tests binary compatibility conservatively, without requesting driver compilation. */
		bool Supports(uint32_t DeviceCapability) const noexcept;
	};

	/** Build-generated identity and native-code coverage for a compilation profile. */
	struct FArdaCudaBuildInfo
	{
		/** Unique profile name; also isolates symbols compiled with different flags. */
		eastl::string mName;
		/** Compiler/options/source-build fingerprint for diagnostics and caches. */
		eastl::string mIdentity;
		/** Native targets actually requested by the build. */
		eastl::vector<FArdaCudaArchitecture> mArchitectures;
		/** True when the build opts into approximate floating-point operations. */
		bool mbFastMath = false;
	};

	/** Standard launch geometry. Streams, contexts and submission remain framework-owned. */
	struct FArdaCudaLaunchConfig
	{
		/** Number of blocks in each axis; each dimension must be nonzero. */
		uint32_t mGridSize[3] = {1, 1, 1};
		/** Threads per block in each axis; per-axis and product limits both apply. */
		uint32_t mBlockSize[3] = {1, 1, 1};
		/** Dynamic shared-memory request for each block, in bytes. */
		uint32_t mSharedMemoryBytes = 0;
	};

	/** Runtime signature of the plain parameter accepted by a kernel or library adapter. */
	struct FArdaCudaKernelSignature
	{
		/** C++ type identity, compared at registration; null denotes an unsupported signature. */
		const std::type_info* mType = nullptr;
		/** Size of the CUDA argument object. */
		size_t mSize = 0;
		/** Alignment of the CUDA argument object. */
		size_t mAlignment = 1;
		/** Runtime eligibility; no facade static assertion is required. */
		bool mbSupported = false;
	};

	/** Kernel-specific native limits queried under the execution context. */
	struct FArdaCudaKernelLimits
	{
		/** Maximum threads for this compiled function. */
		uint32_t mMaxThreadsPerBlock = 0;
		/** Static shared memory used by the function. */
		uint32_t mStaticSharedMemoryBytes = 0;
		/** Maximum dynamic shared memory accepted by the function. */
		uint32_t mMaxDynamicSharedMemoryBytes = 0;
	};

	/** Provider-facing compiled launch adapter. Implemented by the nvcc registration helper.
     * The current CUDA context is supplied by the provider; the adapter never selects a device.
     * Retain this object and its owning code module through GPU completion.
     */
	class IArdaCudaKernelEntry
	{
	public:
		virtual ~IArdaCudaKernelEntry() = default;

		/** Reports the registered signature without initializing CUDA. */
		virtual FArdaCudaKernelSignature GetSignature() const noexcept = 0;

		/** Returns the immutable build manifest. */
		virtual const FArdaCudaBuildInfo& GetBuildInfo() const noexcept = 0;

		/** Queries limits in the provider's current context; no kernel is launched. */
		virtual TArdaRHIResult<FArdaCudaKernelLimits> GetLimits() const = 0;

		/** Enqueues exactly one compiled kernel on the borrowed opaque CUDA stream. */
		virtual FArdaRHIStatus Launch(void* Stream,
		    const FArdaCudaLaunchConfig& Config,
		    const void* Parameters,
		    size_t ParameterSize) const = 0;
	};

	/** Library state for an uncaptured recording or retained graph variant, such as handles,
	 * descriptors and a frozen execution plan. Graph hits reuse the original prepared object.
     * The provider retains this state through GPU completion and destroys it with the
     * preparation context current, before destroying the borrowed stream. Own all native
     * state here; do not share mutable handles or workspace across concurrent recordings.
     */
	class IArdaCudaPreparedCall
	{
	public:
		/** Releases native state under the owning CUDA context after retirement or abandoned recording. */
		virtual ~IArdaCudaPreparedCall() = default;

		/** Enqueues one library operation, which may launch multiple internal kernels.
         * Use only the supplied stream (the same stream as Prepare), preserve its ordering,
         * and return library errors as status. Do not synchronize, select a device, begin
         * capture, or call back into the RHI. GPU-visible state must survive in this object.
         */
		virtual FArdaRHIStatus Enqueue(void* Stream) = 0;
	};

	/** Reusable factory for externally defined CUDA calls; requires no compiled kernel manifest.
     * Library headers and link dependencies belong to the adapter, not the public RHI.
     * Factories must be thread-safe and must not own context-specific native state.
     */
	class IArdaCudaExternalCall
	{
	public:
		/** Destroys the host-side factory; native state belongs in IArdaCudaPreparedCall. */
		virtual ~IArdaCudaExternalCall() = default;

		/** Exact plain parameter schema; addresses are resolved by the provider before Prepare. */
		virtual FArdaCudaKernelSignature GetSignature() const noexcept = 0;

		/** Host-only capability check. Default admits ContextSwitch and VulkanCiG, rejecting
         * D3D12CiG until an adapter explicitly qualifies its library/version/operation.
         * Must be deterministic, thread-safe, and must not initialize or call CUDA.
         */
		virtual FArdaRHIStatus GetSupport(const FArdaCudaCapabilities& Capabilities) const;

		/** Explicit opt-in to capture and repeated replay. Enqueue must use only the supplied
		 * stream, create no host callbacks or allocations, and leave all GPU-visible state alive
		 * in the prepared call. Hidden configuration must be immutable or change the revision.
		 * CiG adapters must also satisfy GetSupport's stricter restrictions. Defaults to false.
		 */
		virtual bool SupportsGraphCapture(const FArdaCudaCapabilities&) const noexcept
		{
			return false;
		}

		/** Change this value whenever hidden immutable configuration affects captured work. */
		virtual uint64_t GetGraphCaptureRevision() const noexcept
		{
			return 0;
		}

		/** Calls the host support hook, translating adapter exceptions into an InvalidArgument status. */
		[[nodiscard]] FArdaRHIStatus CheckSupport(const FArdaCudaCapabilities& Capabilities) const;

		/** Optionally initializes a cache before first preparation on this context. Default returns
         * null success for no cache. A non-null state retains this factory until device teardown;
         * reuse stable factory instances to reuse caches. No declared resources may be accessed.
         * The provider owns and destroys native state under this context, after all recordings retire.
         */
		virtual TArdaRHIResult<eastl::unique_ptr<IArdaCudaExternalCallState>> CreateContextState(
		    const FArdaCudaExternalCallContext& Context) const;

		/** Creates exclusive state under the supplied current context before any batch enqueue
         * or CiG capture. Validate parameters and create handles/descriptors/plans here.
         * Do not access declared resources or enqueue algorithm work: graphics may still own
         * them. Copy Parameters; its storage is borrowed. Preparation may fail without
         * executing earlier operations. A successful result must contain a non-null object.
         */
		virtual TArdaRHIResult<eastl::unique_ptr<IArdaCudaPreparedCall>> Prepare(
		    const FArdaCudaExternalCallContext& Context,
		    const void* Parameters,
		    size_t ParameterSize) const = 0;
	};

	/** Patches one resource address/surface into the owned CUDA parameter object. */
	struct FArdaCudaParameterPatch
	{
		/** Retained resource index in the dispatch. */
		uint32_t mBindingIndex = 0;
		/** Byte offset of the 64-bit CUDA resource representation. */
		size_t mOffset = 0;
		/** Required native buffer address alignment. Surfaces use one. */
		size_t mAlignment = 1;
	};

	/** One CUDA operation and an owned, unresolved parameter object. Geometry applies only to compiled kernels. */
	struct FArdaCudaKernel : FArdaCudaLaunchConfig
	{
		/** Retained compiled entry; source compilation and string entry lookup are unavailable. */
		eastl::shared_ptr<const IArdaCudaKernelEntry> mEntry;
		/** Frozen values; resource representations are patched by the provider. */
		eastl::vector<uint8_t> mParameters;
		/** Native resource locations inside the parameter object. */
		eastl::vector<FArdaCudaParameterPatch> mPatches;
		/** External library factory, mutually exclusive with mEntry. May enqueue multiple internal kernels. */
		eastl::shared_ptr<const IArdaCudaExternalCall> mExternalCall;
		/** Optional common retained-graph metadata; ordinary raw dispatches leave this empty. */
		eastl::shared_ptr<const FArdaCudaGraphBatch> mGraphBatch;
	};

	/** A single operation dispatch; use DispatchCudaSequence to group kernels and library calls. */
	struct FArdaCudaDispatch
	{
		/** Resource views retained by the kernel in this dispatch. */
		eastl::vector<FArdaCudaBinding> mBindings;
		/** Exactly one compiled kernel or external call, validated and submitted through the shared batch path. */
		eastl::vector<FArdaCudaKernel> mKernels;
	};

	/** Rejects unsupported texture layouts/formats before native allocation; does not test device support. */
	[[nodiscard]] FArdaRHIStatus ValidateArdaCudaTexture(const FArdaRHITextureDesc& Desc);

	/** Admits dedicated device-local buffers; rejects sparse, placed, versioned, CPU-visible and AS storage. */
	[[nodiscard]] FArdaRHIStatus ValidateArdaCudaBuffer(const FArdaRHIBufferDesc& Desc);

	/** Checks code, dimensions, shared memory and argument references; does not prove kernel memory safety. */
	[[nodiscard]] FArdaRHIStatus ValidateArdaCudaKernels(const eastl::vector<FArdaCudaKernel>& Kernels,
	    size_t BindingCount,
	    const FArdaCudaCapabilities& Capabilities);

	/** Validates one or more ordered kernels/library calls against a shared binding table.
     * Single-operation operands and DispatchCuda continue to use ValidateArdaCudaKernels.
     */
	[[nodiscard]] FArdaRHIStatus ValidateArdaCudaKernelBatch(const eastl::vector<FArdaCudaKernel>& Kernels,
	    size_t BindingCount,
	    const FArdaCudaCapabilities& Capabilities);
}
