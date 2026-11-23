/** Native compute provider registration, device qualification and operand dispatch. */
#pragma once

#include "NativeCompute/ArdaNativeOperand.h"
#include <EASTL/unordered_map.h>
#include <shared_mutex>

namespace arda
{
	// ================================
	// Provider descriptors and resource handoff
	// ================================

	/** A provider-owned resource representation, scoped by the provider ID. */
	struct FArdaNativeResourceCategory
	{
		eastl::string mId;
		/** Use Count for native-only storage owned by an IArdaNativeResource. */
		EArdaRHIResourceType mType = EArdaRHIResourceType::Buffer;
	};

	inline constexpr uint32_t ArdaNativeComputeProviderInterfaceVersion = 2;

	/** Registry identity and compatibility boundaries of a native provider. */
	struct FArdaNativeProviderDesc
	{
		/** Nonempty provider ID unique in the registry. */
		eastl::string mId;
		/** Registered graphics module names, such as native-vulkan. */
		eastl::vector<eastl::string> mGraphicsApis;
		/** Source format IDs the provider recognizes. */
		eastl::vector<eastl::string> mSourceFormats;
		/** Provider SPI version. Checked before publishing a linked module. */
		uint32_t mInterfaceVersion = ArdaNativeComputeProviderInterfaceVersion;
		eastl::vector<FArdaNativeResourceCategory> mResourceCategories;
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

	// ================================
	// Native compute provider interface
	// ================================

	/** Validate and prepare native work for one provider and its supported sources. */
	class IArdaNativeComputeProvider
	{
	public:
		/** Release provider-owned state. */
		virtual ~IArdaNativeComputeProvider() = default;
		/** Return the provider metadata, valid for the lifetime of the provider. */
		virtual const FArdaNativeProviderDesc& GetDesc() const noexcept = 0;
		/** Called once per provider/device pair. A failed probe disables this route on this device. */
		virtual TArdaRHIResult<FArdaNativeProviderSupport> ProbeDevice(const IArdaRHIDevice&) const { return {}; }
		/** Decode provider-owned artifact metadata. Shader fallback remains eligible for unsupported artifacts. */
		virtual TArdaRHIResult<eastl::shared_ptr<const IArdaNativeKernelSource>> LoadKernel(
		    const FArdaNativeKernelBinary&, const FArdaNativeKernelLayout&) const
		{
			return {{}, FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Provider cannot load packaged kernels.")};
		}
		virtual void ConfigureBuffer(FArdaRHIBufferDesc&) const {}
		virtual void ConfigureTexture(FArdaRHITextureDesc&) const {}
		/** Submit this sequence with one graphics/compute handoff. Required for context-switching providers. */
		virtual FArdaRHIStatus RecordBatch(IArdaRHICommandList&, const eastl::vector<FArdaNativeWorkRef>&) const
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, "Provider cannot record a native session.");
		}
		/** Check architecture, source ABI, queue and schema without allocating or recording work. */
		virtual FArdaRHIStatus CheckSupport(const FArdaNativeComputeContext&, const FArdaNativeOperand&,
		    const IArdaNativeKernelSource&) const = 0;
		/** Unknown defers resource state qualification to the physical view at preparation. */
		virtual EArdaRHIResourceState GetRequiredState(const FArdaNativeResourceSlot&) const
		{
			return EArdaRHIResourceState::Unknown;
		}
		/** Inspect the physical view and report whether the caller must add a conversion pass. */
		virtual TArdaRHIResult<FArdaNativeResourceHandoff> QueryHandoff(const FArdaNativeComputeContext&,
		    const FArdaNativeResourceSlot&, const FArdaNativeResourceView&) const = 0;
		/** Freeze arguments and resolve the source. Preparation must never enqueue work. */
		virtual TArdaRHIResult<FArdaNativeWorkRef> Prepare(const FArdaNativeComputeContext&, const FArdaNativeOperand&,
		    const IArdaNativeKernelSource&, const FArdaNativeInvocation&, const FArdaNativeLaunch&) const = 0;
	};

	// ================================
	// Prepared plans and recording sessions
	// ================================

	/** A resolved implementation and frozen work tied to its preparation device and queue. */
	class FArdaNativeComputePlan final : public IArdaRHIComputeCommand
	{
	public:
		/** Return the selected native, compute shader, or graphics shader route. */
		[[nodiscard]] EArdaNativeComputeRoute GetRoute() const noexcept { return mRoute; }
		/** Return the chosen native variant name, or an empty string for a shader route. */
		[[nodiscard]] const eastl::string& GetVariantName() const noexcept { return mVariantName; }
		[[nodiscard]] EArdaNativeExecutionMode GetExecutionMode() const noexcept { return mMode; }
		[[nodiscard]] const eastl::string& GetProviderId() const noexcept { return mProviderId; }
		/** True when recording needs graphics commands before or after the native dispatch. */
		[[nodiscard]] bool HasBoundaryWork() const noexcept { return !mBeforeNative.empty() || !mAfterNative.empty(); }
		/** Borrow the provider work for typed integration. Use Record or a session for plans with boundary work. */
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
		friend class FArdaNativeComputeSession;
		EArdaNativeExecutionMode mMode = EArdaNativeExecutionMode::Direct;
		eastl::string mProviderId;
		eastl::shared_ptr<const IArdaNativeComputeProvider> mProvider;
		FArdaRHIStatus RecordOn(IArdaRHICommandList& Commands) const override;
		FArdaNativeComputeContext mContext;
		EArdaNativeComputeRoute mRoute = EArdaNativeComputeRoute::Native;
		eastl::string mVariantName;
		FArdaNativeWorkRef mWork;
		eastl::vector<FArdaNativeWorkRef> mBeforeNative, mAfterNative;
	};

	/** A command-list-local group of compatible, ordered context-switching operands. */
	class FArdaNativeComputeSession
	{
	public:
		[[nodiscard]] FArdaRHIStatus Append(FArdaNativeComputePlan Plan, IArdaRHICommandList& Commands);
		[[nodiscard]] FArdaRHIStatus Flush(IArdaRHICommandList& Commands);
	private:
		IArdaRHICommandList* mCommands = nullptr;
		eastl::shared_ptr<const IArdaNativeComputeProvider> mProvider;
		eastl::vector<FArdaNativeWorkRef> mWork, mBeforeNative, mAfterNative;
	};

	// ================================
	// Provider and operand registry
	// ================================

	/**
	 * Own native providers and immutable operand definitions.
	 * @threading Registry reads and insertions use a shared mutex. Callbacks can query the registry.
	 */
	class FArdaNativeComputeRegistry
	{
	public:
		/** A graph-local operand registry can resolve providers installed in the backend, including later registrations. */
		explicit FArdaNativeComputeRegistry(bool UseGlobalProviders = false) : mbUseGlobalProviders(UseGlobalProviders) {}

		/**
		 * Register a provider with a unique ID and nonempty, distinct compatibility names.
		 * @param Provider Immutable provider to retain on success.
		 * @return Registration status.
		 * @errors InvalidArgument for an empty provider, invalid metadata, or duplicate ID.
		 */
		[[nodiscard]] FArdaRHIStatus RegisterProvider(eastl::shared_ptr<const IArdaNativeComputeProvider> Provider);
		/** Look up a provider's native representation and its RHI resource kind. */
		[[nodiscard]] TArdaRHIResult<FArdaNativeResourceCategory> FindResourceCategory(
		    const char* ProviderId, const char* CategoryId) const;
		/** Single authoritative device/driver probe, including negative results and JIT availability. */
		[[nodiscard]] TArdaRHIResult<FArdaNativeProviderSupport> QueryProviderSupport(
		    const char* ProviderId, const FArdaNativeComputeContext& Context) const;
		/** Probe all registered modules at device startup; individual failures leave other routes available. */
		void ProbeDeviceProviders(const FArdaRHIDeviceRef& Device) const;
		[[nodiscard]] eastl::vector<eastl::string> GetProviderIds() const;
		[[nodiscard]] TArdaRHIResult<EArdaNativeExecutionMode> QueryExecutionMode(
		    const char* Id, const FArdaNativeComputeContext& Context, eastl::string* OutProviderId = nullptr,
		    const FArdaNativeRouteCheck& CheckRoute = {}) const;
		/**
		 * Publish an operand with at least one native variant or shader implementation.
		 * @param Operand Resource slots, parameter size, implementations, and optional validator.
		 * @return Registration status.
		 * @errors InvalidArgument for a duplicate ID or invalid operand metadata.
		 */
		[[nodiscard]] FArdaRHIStatus RegisterOperand(FArdaNativeOperand Operand);
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
		[[nodiscard]] eastl::shared_ptr<const FArdaNativeOperand> FindOperand(const char* Id) const;

		// ================================
		// Operand support and preparation
		// ================================

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
		 * A native Unsupported or BackendFailure can advance to another eligible variant or shader fallback.
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
		TArdaRHIResult<FArdaNativeProviderSupport> ProbeProvider(const eastl::shared_ptr<const IArdaNativeComputeProvider>&,
		    const FArdaNativeComputeContext&) const;
		TArdaRHIResult<FArdaNativeComputePlan> Resolve(const char* Id, const FArdaNativeComputeContext& Context,
		    const FArdaNativeInvocation* Invocation, const FArdaNativeRouteCheck& CheckRoute) const;
		friend FArdaRHIStatus RegisterLinkedArdaNativeComputeProviders(FArdaNativeComputeRegistry&);
		const bool mbUseGlobalProviders;
		mutable std::shared_mutex mMutex;
		eastl::unordered_map<eastl::string, eastl::shared_ptr<const IArdaNativeComputeProvider>> mProviders;
		eastl::unordered_map<eastl::string, eastl::shared_ptr<const FArdaNativeOperand>> mOperands;
	};

	// ================================
	// Linked providers and packaged operands
	// ================================

	/** Register all linked provider modules atomically. Local registries can opt in. */
	[[nodiscard]] FArdaRHIStatus RegisterLinkedArdaNativeComputeProviders(FArdaNativeComputeRegistry&);
	/** Process-wide registry used by the backend and by graph artifact references. */
	FArdaNativeComputeRegistry& GetArdaNativeComputeRegistry();
	[[nodiscard]] FArdaRHIStatus GetArdaNativeComputeModuleStatus();

	/** Resolve the artifact lazily, allowing a matching registered shader even when the provider is unavailable. */
	[[nodiscard]] TArdaRHIResult<FArdaNativeOperand> MakeArdaPackagedNativeOperand(
	    const FArdaNativeKernelKey&, const FArdaNativeKernelLayout&, FArdaNativeOperand Operand);

}
