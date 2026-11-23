/** The backend facade contract for a native compute operand and its implementations. */
#pragma once

#include "NativeCompute/ArdaNativeKernelLibrary.h"
#include "RHI/Device/ArdaRHIDevice.h"
#include "RHI/Resources/ArdaComputeAccess.h"
#include <EASTL/functional.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

namespace arda
{
	// ================================
	// Route selection and device support
	// ================================

	class IArdaNativeComputeProvider;
	/** Choose which implementation routes the resolver may try. */
	enum class EArdaNativeComputePreference : uint8_t
	{
		/** Try native variants in declaration order, then shader fallbacks in declaration order. */
		PreferNative,
		/** Require an eligible native provider and variant. */
		NativeOnly,
		/** Try the registered shader implementations in declaration order. */
		ShaderOnly
	};

	/** How this provider records work on this particular graphics device. */
	enum class EArdaNativeExecutionMode : uint8_t { Direct, ContextSwitching };

	/** Optional state allocated by a provider when it probes a device. Released with that device. */
	class IArdaNativeDeviceContext
	{
	public:
		virtual ~IArdaNativeDeviceContext() = default;
	};

	struct FArdaNativeProviderSupport
	{
		EArdaNativeExecutionMode mMode = EArdaNativeExecutionMode::Direct;
		bool mbRuntimeCompilation = false;
		eastl::shared_ptr<const IArdaNativeDeviceContext> mContext;
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
		/** Filled by the resolver with the selected provider's device state. */
		eastl::shared_ptr<const IArdaNativeDeviceContext> mProviderContext;
	};

	// ================================
	// Resource slots and bound views
	// ================================

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
		/** Optional provider-local category of a native slot. */
		eastl::string mCategory;
	};

	/** Collect an operand's resource slots in binding order. RegisterOperand validates the completed schema. */
	class FArdaNativeResourceSlotsCreator
	{
	public:
		/** Append a buffer with the declared access and optional provider category. */
		FArdaNativeResourceSlotsCreator& AddBuffer(eastl::string Name, EArdaComputeAccess Access,
		    eastl::string Category = {})
		{
			return AddSlot({eastl::move(Name), EArdaRHIResourceType::Buffer, Access,
			    EArdaRHIFormat::Unknown, eastl::move(Category)});
		}

		/** Append a texture. Unknown format accepts any format supported by the operand. */
		FArdaNativeResourceSlotsCreator& AddTexture(eastl::string Name, EArdaComputeAccess Access,
		    EArdaRHIFormat Format = EArdaRHIFormat::Unknown, eastl::string Category = {})
		{
			return AddSlot({eastl::move(Name), EArdaRHIResourceType::Texture, Access, Format, eastl::move(Category)});
		}

		/** Append a fully specified slot, including an opaque provider resource. */
		FArdaNativeResourceSlotsCreator& AddSlot(FArdaNativeResourceSlot Slot)
		{
			mSlots.push_back(eastl::move(Slot));
			return *this;
		}

		/** Transfer the slots in registration order and leave the creator empty for reuse. */
		[[nodiscard]] eastl::vector<FArdaNativeResourceSlot> Finalize()
		{
			eastl::vector<FArdaNativeResourceSlot> Slots;
			Slots.swap(mSlots);
			return Slots;
		}

	private:
		eastl::vector<FArdaNativeResourceSlot> mSlots;
	};

	/** A provider can retain a native allocation that has no RHI buffer or texture representation. */
	class IArdaNativeResource
	{
	public:
		virtual ~IArdaNativeResource() = default;
		virtual const char* GetProviderId() const noexcept = 0;
		virtual const char* GetCategoryId() const noexcept = 0;
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
		/** Native-only storage produced by a conversion for a provider-local opaque category. */
		eastl::shared_ptr<const IArdaNativeResource> mNativeResource;
	};

	// ================================
	// Launch geometry and invocation arguments
	// ================================

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

	// ================================
	// Kernel sources and prepared work
	// ================================

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

	// ================================
	// Graphics and native resource conversion
	// ================================

	/** Borrowed schema and application views for a single native route under preparation. */
	struct FArdaNativeConversionContext
	{
		const FArdaNativeComputeContext& mContext;
		const IArdaNativeComputeProvider& mProvider;
		const eastl::vector<FArdaNativeResourceSlot>& mGraphicsSlots;
		const eastl::vector<FArdaNativeResourceSlot>& mNativeSlots;
		const FArdaNativeInvocation& mGraphics;
	};

	/** Owned native bindings and frozen graphics commands on each side of the native dispatch. */
	struct FArdaNativeConversion
	{
		FArdaNativeInvocation mNative;
		eastl::vector<EArdaRHIResourceState> mGraphicsStates;
		eastl::vector<FArdaNativeWorkRef> mBeforeNative, mAfterNative;
	};

	/** Bind native views and freeze any commands needed to populate them. */
	using FArdaNativeGraphicsToNative = eastl::function<FArdaRHIStatus(const FArdaNativeConversionContext&, FArdaNativeConversion&)>;
	/** Freeze commands that publish native writes back to the graphics views. */
	using FArdaNativeNativeToGraphics = eastl::function<FArdaRHIStatus(const FArdaNativeConversionContext&, FArdaNativeConversion&)>;

	// ================================
	// Kernel variants and shader fallbacks
	// ================================

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
		/** Native-side representations in graphics slot order. Empty uses the operand slots. */
		eastl::vector<FArdaNativeResourceSlot> mNativeResources;
		/** Graphics-side entry and return states. Converters restore these states after recording. */
		eastl::vector<EArdaRHIResourceState> mGraphicsStates;
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

	// ================================
	// Operand definition and conversion helpers
	// ================================

	/** One resource and parameter contract shared by ordered native variants and shader fallbacks. */
	struct FArdaNativeOperand
	{
		/** Nonempty ID unique in the registry. */
		eastl::string mId;
		/** Ordered resource slots shared by every implementation. */
		eastl::vector<FArdaNativeResourceSlot> mResources;
		/** Exact invocation parameter size in bytes. */
		size_t mParameterSize = 0;
		/** First eligible variant wins. Different variants can use different providers. */
		eastl::vector<FArdaNativeKernelVariant> mVariants;
		/** Single fallback shorthand. Tried after the ordered shader fallbacks. */
		FArdaNativeShaderFallback mFallback;
		/** Check algorithm invariants shared by every implementation before selection. */
		eastl::function<FArdaRHIStatus(const FArdaNativeInvocation&)> mValidate;
		/** Stable ABI identity shared by packaged kernels and their shader fallbacks. */
		eastl::string mSignature;
		/** Shader fallbacks in preference order, evaluated after eligible native variants. */
		eastl::vector<FArdaNativeShaderFallback> mShaderFallbacks;
		/** Both boundary hooks are required for native variants. Hooks prepare work without submitting it. */
		FArdaNativeGraphicsToNative mGraphicsToNative;
		FArdaNativeNativeToGraphics mNativeToGraphics;
		/** An archived application-defined conversion must be restored from its registered operand. */
		bool mbRequiresRegisteredConversions = false;
	};

	/** Explicitly use direct views in both directions. The provider still qualifies every physical binding. */
	void UseArdaNativeDirectResources(FArdaNativeOperand& Operand);
	/** True when the operand declares application/provider conversion hooks beyond direct views. */
	[[nodiscard]] bool HasArdaNativeResourceConversions(const FArdaNativeOperand& Operand);

	/** Compatibility name for existing operand declarations. */
	using FArdaNativeOperandDesc = FArdaNativeOperand;

}
