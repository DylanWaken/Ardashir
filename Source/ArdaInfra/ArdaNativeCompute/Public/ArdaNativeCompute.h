/** Register native kernels and shader implementations of the same operation. */
#pragma once

#include "RHI/Device/ArdaRHIDevice.h"
#include "RHI/Resources/ArdaComputeAccess.h"
#include <EASTL/functional.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/unordered_map.h>
#include <shared_mutex>

namespace arda
{
	/** Choose which implementation routes the resolver may try. */
	enum class EArdaNativeComputePreference : uint8_t
	{
		/** Try native variants in declaration order, then the shader fallback. */
		PreferNative,
		/** Require an eligible native provider and variant. */
		NativeOnly,
		/** Use the registered shader implementation. */
		ShaderOnly
	};

	/** Device, queue, and route preference for support checks and preparation. */
	struct FArdaNativeComputeContext
	{
		/** Device on which the prepared work will record. */
		FArdaRHIDeviceRef mDevice;
		/** Queue the command list must use when it records the prepared plan. */
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
		/** Routes the resolver may try. */
		EArdaNativeComputePreference mPreference = EArdaNativeComputePreference::PreferNative;
		/** Require a particular native provider ID. Empty leaves the choice to the operand. */
		eastl::string mRequiredProviderId;
		/** Optional scheduler restriction. Admit only these provider IDs, preserving the operand's variant order. */
		eastl::vector<eastl::string> mAllowedProviderIds;
	};

	/** All implementations of an operand consume these slots in declaration order. */
	struct FArdaNativeResourceSlot
	{
		/** Nonempty name unique within the operand. */
		eastl::string mName;
		/** RHI resource kind required by this slot. */
		EArdaRHIResourceType mType = EArdaRHIResourceType::Buffer;
		/** Read, write, or read/write access required by every route. */
		EArdaComputeAccess mAccess = EArdaComputeAccess::Read;
		/** Required texture storage format. Unknown leaves the format unconstrained. */
		EArdaRHIFormat mFormat = EArdaRHIFormat::Unknown;
	};

	/** Physical resource bound to one declared operand slot. */
	struct FArdaNativeResourceView
	{
		/** Retained RHI resource. Its kind must match the slot. */
		FArdaRHIResourceRef mResource;
		/** Nonempty range inside a bound buffer. */
		FArdaRHIBufferRange mBufferRange;
		/** Mip index for a bound texture. */
		uint32_t mMipLevel = 0;
	};

	/** Three-dimensional group count and group size for one native dispatch. */
	struct FArdaNativeLaunch
	{
		/** Work-group counts in X, Y, and Z. */
		uint32_t mGridSize[3] = {1, 1, 1};
		/** Threads per group in X, Y, and Z. */
		uint32_t mBlockSize[3] = {1, 1, 1};
		/** Dynamic shared memory requested per group, in bytes. */
		uint32_t mSharedMemoryBytes = 0;
	};

	/** Resource bindings and parameter bytes for one invocation. */
	struct FArdaNativeInvocation
	{
		/** Physical views in the operand's slot order. */
		eastl::vector<FArdaNativeResourceView> mResources;
		/** Parameter block with exactly the operand's declared byte count. */
		eastl::vector<uint8_t> mParameters;
		/** Launch geometry for a variant without a custom selection policy. */
		FArdaNativeLaunch mLaunch;
	};

	/** Source objects own their artifact or linked entry and its provider-specific ABI. */
	class IArdaNativeKernelSource
	{
	public:
		/** Release ownership of the source artifact or linked entry. */
		virtual ~IArdaNativeKernelSource() = default;
		/** Return the registry ID of the default provider for this source. */
		virtual const char* GetProviderId() const noexcept = 0;
		/** Return the format ID used to match the provider's supported formats. */
		virtual const char* GetFormat() const noexcept = 0;
	};

	/** Prepared, immutable work for one implementation route. */
	class IArdaNativePreparedWork
	{
	public:
		/** Release the captured state held by this work. */
		virtual ~IArdaNativePreparedWork() = default;
		/**
		 * Append work to an open command list without submitting it.
		 * @param Commands Open list for the prepared device and queue.
		 * @return Recording status.
		 * @ownership The RHI retains submitted dependencies through GPU completion.
		 */
		virtual FArdaRHIStatus Record(IArdaRHICommandList& Commands) const = 0;
	};

	/** Shared ownership of immutable work prepared for a specific invocation. */
	using FArdaNativeWorkRef = eastl::shared_ptr<const IArdaNativePreparedWork>;
	/**
	 * Wrap a command-list recording callback as prepared work.
	 * @param Record Callback that appends commands. Capture pipeline state, bindings, and constants by value.
	 * @return Prepared work, or an empty reference for an empty callback.
	 */
	[[nodiscard]] FArdaNativeWorkRef MakeArdaNativeWork(eastl::function<FArdaRHIStatus(IArdaRHICommandList&)> Record);

	/** One native implementation and its optional launch policy. */
	struct FArdaNativeKernelVariant
	{
		/** Variant name unique within the operand, returned by a native plan. */
		eastl::string mName;
		/** Source retained by the registration. Its provider and format IDs must be nonempty. */
		eastl::shared_ptr<const IArdaNativeKernelSource> mSource;
		/** Return geometry, or Unsupported when another variant should handle this input. */
		eastl::function<TArdaRHIResult<FArdaNativeLaunch>(const FArdaNativeComputeContext&, const FArdaNativeInvocation&)> mSelect;
		/** Optional provider override. Allows compatible providers to share the same compiled source and ABI. */
		eastl::string mProviderId;
	};

	/** Implementation selected for a prepared plan. */
	enum class EArdaNativeComputeRoute : uint8_t
	{
		/** Kernel or external call prepared by a registered native provider. */
		Native,
		/** Fallback that records a compute shader. */
		ComputeShader,
		/** Fallback that records a graphics shader on a graphics list. */
		GraphicsShader
	};

	/** Optional shader implementation of the same operation and resource access contract. */
	struct FArdaNativeShaderFallback
	{
		/** Compute or graphics shader route. A graphics route requires a graphics queue. */
		EArdaNativeComputeRoute mRoute = EArdaNativeComputeRoute::ComputeShader;
		/** Freeze the resources, bindings, and arguments into work without submitting it. */
		eastl::function<TArdaRHIResult<FArdaNativeWorkRef>(const FArdaNativeComputeContext&, const FArdaNativeInvocation&)> mPrepare;
		/** Device features required by this shader implementation. */
		FArdaRHIFeatureRequirements mRequiredFeatures;
	};

	/** Register one resource and parameter contract with at least one implementation. */
	struct FArdaNativeOperandDesc
	{
		/** Nonempty ID unique in the registry. */
		eastl::string mId;
		/** Ordered resource slots shared by every implementation. */
		eastl::vector<FArdaNativeResourceSlot> mResources;
		/** Exact invocation parameter size in bytes. */
		size_t mParameterSize = 0;
		/** First eligible variant wins. Different variants can use different providers. */
		eastl::vector<FArdaNativeKernelVariant> mVariants;
		/** Shader implementation eligible when the preference permits a fallback. */
		FArdaNativeShaderFallback mFallback;
		/** Check algorithm invariants shared by every implementation before selection. */
		eastl::function<FArdaRHIStatus(const FArdaNativeInvocation&)> mValidate;
	};

	/** Registry identity and compatibility boundaries of a native provider. */
	struct FArdaNativeProviderDesc
	{
		/** Nonempty provider ID unique in the registry. */
		eastl::string mId;
		/** Registered graphics module names, such as native-vulkan. */
		eastl::vector<eastl::string> mGraphicsApis;
		/** Source format IDs the provider recognizes. */
		eastl::vector<eastl::string> mSourceFormats;
	};

	/** Whether a provider can use the bound resource as supplied. */
	enum class EArdaNativeHandoff : uint8_t
	{
		/** The provider can access the selected view directly. */
		Direct,
		/** Add a conversion/copy pass and bind its output to use this provider. */
		ConversionRequired
	};

	/** Representation and graphics resource state that the provider expects at a handoff. */
	struct FArdaNativeResourceHandoff
	{
		/** Direct access or a request for an explicit copy/conversion pass. */
		EArdaNativeHandoff mKind = EArdaNativeHandoff::Direct;
		/** State required on entry to a direct handoff. The provider records its native synchronization. */
		EArdaRHIResourceState mRequiredState = EArdaRHIResourceState::Common;
	};

	/** A scheduler may decline a route whose resource states conflict with the pass it compiled.
	 * Native routes supply the source and one required state per operand slot. Shader routes supply
	 * a null source and no native states. Return Unsupported to continue selection.
	 */
	using FArdaNativeRouteCheck = eastl::function<FArdaRHIStatus(EArdaNativeComputeRoute,
	    const IArdaNativeKernelSource*, const eastl::vector<EArdaRHIResourceState>&)>;

	/** Validate and prepare native work for one provider and its supported sources. */
	class IArdaNativeComputeProvider
	{
	public:
		/** Release provider-owned state. */
		virtual ~IArdaNativeComputeProvider() = default;
		/** Return the provider metadata, valid for the lifetime of the provider. */
		virtual const FArdaNativeProviderDesc& GetDesc() const noexcept = 0;
		/** Check architecture, source ABI, queue and schema without allocating or recording work. */
		virtual FArdaRHIStatus CheckSupport(const FArdaNativeComputeContext&, const FArdaNativeOperandDesc&,
		    const IArdaNativeKernelSource&) const = 0;
		/** Inspect the physical view and report whether the caller must add a conversion pass. */
		virtual TArdaRHIResult<FArdaNativeResourceHandoff> QueryHandoff(const FArdaNativeComputeContext&,
		    const FArdaNativeResourceSlot&, const FArdaNativeResourceView&) const = 0;
		/** Freeze arguments and resolve the source. Preparation must never enqueue work. */
		virtual TArdaRHIResult<FArdaNativeWorkRef> Prepare(const FArdaNativeComputeContext&, const FArdaNativeOperandDesc&,
		    const IArdaNativeKernelSource&, const FArdaNativeInvocation&, const FArdaNativeLaunch&) const = 0;
	};

	/** A resolved implementation and frozen work tied to its preparation device and queue. */
	class FArdaNativeComputePlan final : public IArdaRHIComputeCommand
	{
	public:
		/** Return the selected native, compute shader, or graphics shader route. */
		[[nodiscard]] EArdaNativeComputeRoute GetRoute() const noexcept { return mRoute; }
		/** Return the chosen native variant name, or an empty string for a shader route. */
		[[nodiscard]] const eastl::string& GetVariantName() const noexcept { return mVariantName; }
		/** Borrow the shared work reference for low-level integration. Copy it to retain ownership. */
		[[nodiscard]] const FArdaNativeWorkRef& GetWork() const noexcept { return mWork; }
		/**
		 * Record the frozen work without submitting it.
		 * @param Commands Open command list on the preparation device and queue.
		 * @return Recording status.
		 * @errors An empty plan returns InvalidState. Another device returns WrongDevice. A different queue returns InvalidArgument.
		 */
		[[nodiscard]] FArdaRHIStatus Record(IArdaRHICommandList& Commands) const { return Commands.RecordNativeCompute(*this); }

	private:
		friend class FArdaNativeComputeRegistry;
		FArdaRHIStatus RecordOn(IArdaRHICommandList& Commands) const override;
		FArdaNativeComputeContext mContext;
		EArdaNativeComputeRoute mRoute = EArdaNativeComputeRoute::Native;
		eastl::string mVariantName;
		FArdaNativeWorkRef mWork;
	};

	/**
	 * Own native providers and immutable operand definitions.
	 * @threading Registry reads and insertions use a shared mutex. Callbacks can query the registry.
	 */
	class FArdaNativeComputeRegistry
	{
	public:
		/**
		 * Register a provider with a unique ID and nonempty, distinct compatibility names.
		 * @param Provider Immutable provider to retain on success.
		 * @return Registration status.
		 * @errors InvalidArgument for an empty provider, invalid metadata, or duplicate ID.
		 */
		[[nodiscard]] FArdaRHIStatus RegisterProvider(eastl::shared_ptr<const IArdaNativeComputeProvider> Provider);
		/**
		 * Publish an operand with at least one native variant or shader implementation.
		 * @param Operand Resource slots, parameter size, implementations, and optional validator.
		 * @return Registration status.
		 * @errors InvalidArgument for a duplicate ID or invalid operand metadata.
		 */
		[[nodiscard]] FArdaRHIStatus RegisterOperand(FArdaNativeOperandDesc Operand);
		/**
		 * Look up a registered provider by ID.
		 * @param Id Provider registry ID.
		 * @return Shared provider, or an empty reference for an unknown or null ID.
		 */
		[[nodiscard]] eastl::shared_ptr<const IArdaNativeComputeProvider> FindProvider(const char* Id) const;
		/**
		 * Look up a registered operand by ID.
		 * @param Id Operand registry ID.
		 * @return Shared immutable definition, or an empty reference for an unknown or null ID.
		 */
		[[nodiscard]] eastl::shared_ptr<const FArdaNativeOperandDesc> FindOperand(const char* Id) const;
		/**
		 * Check device, queue, and provider or shader eligibility before supplying frame inputs.
		 * @param Id Registered operand ID.
		 * @param Context Device, queue, and allowed routes.
		 * @return Support status. Prepare checks actual resource handoffs and input policy.
		 */
		[[nodiscard]] FArdaRHIStatus CheckSupport(const char* Id, const FArdaNativeComputeContext& Context) const;
		/**
		 * Validate bindings, choose an implementation, and freeze one invocation.
		 *
		 * A native Unsupported or BackendFailure can advance to another eligible variant or the shader fallback.
		 * An invalid invocation fails before any implementation runs. Preparation does not submit work.
		 *
		 * @param Id Registered operand ID.
		 * @param Context Device, queue, and allowed routes.
		 * @param Invocation Bindings, parameter bytes, and default launch geometry.
		 * @param CheckRoute Optional scheduler callback run before preparing each eligible route.
		 * @return Prepared plan or failure status.
		 */
		[[nodiscard]] TArdaRHIResult<FArdaNativeComputePlan> Prepare(const char* Id,
		    const FArdaNativeComputeContext& Context, const FArdaNativeInvocation& Invocation,
		    const FArdaNativeRouteCheck& CheckRoute = {}) const;

	private:
		TArdaRHIResult<FArdaNativeComputePlan> Resolve(const char* Id, const FArdaNativeComputeContext& Context,
		    const FArdaNativeInvocation* Invocation, const FArdaNativeRouteCheck& CheckRoute) const;
		mutable std::shared_mutex mMutex;
		eastl::unordered_map<eastl::string, eastl::shared_ptr<const IArdaNativeComputeProvider>> mProviders;
		eastl::unordered_map<eastl::string, eastl::shared_ptr<const FArdaNativeOperandDesc>> mOperands;
	};
}
