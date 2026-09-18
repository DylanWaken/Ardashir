/** Facade command recording state and provider delegation. */
#pragma once

#include "RHI/Device/ArdaRHIFacadeHelpers.h"

namespace arda::detail
{
	class FArdaRHIDeviceImpl;

	struct FArdaPendingBufferCopyCompletion
	{
		bool mbBlocking = false;
		FArdaProviderObjectRef mReadbackBuffer;
		size_t mByteSize = 0;
		eastl::vector<uint8_t>* mOutput = nullptr;
		FArdaRHIHostToDeviceCopyCallback mUploadCallback;
		FArdaRHIDeviceToHostCopyCallback mReadbackCallback;
	};

	class FArdaCommandList final : public FArdaResource, public IArdaRHICommandList
	{
	public:
		FArdaRHIStatus DispatchCuda(const FArdaCudaDispatch&) override;
		FArdaRHIStatus DispatchCudaSequence(const eastl::vector<FArdaCudaDispatch>&) override;
		FArdaRHIStatus RecordCudaBatch(const eastl::vector<FArdaCudaBinding>&,
		    const eastl::vector<FArdaCudaKernel>&);
		FArdaCommandList(FArdaRHIDeviceImpl* Device,
		    EArdaRHIQueueType Queue,
		    eastl::unique_ptr<IArdaProviderCommandList> Native,
		    eastl::shared_ptr<FArdaLifetimeTracker> LifetimeTracker);

		IArdaRHIDevice* GetDevice() const noexcept override;

		EArdaRHIQueueType GetQueueType() const noexcept override
		{
			return mQueue;
		}

		FArdaRHIStatus Open() override;

		FArdaRHIStatus Close() override
		{
			if (!mbRecordingOpen)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "Command list is not open.");
			}
			const auto Status = mNative->Close();
			LatchError(Status);
			if (Status)
			{
				mbRecordingOpen = false;
			}
			if (!mRecordingStatus)
			{
				return mRecordingStatus;
			}
			return Status;
		}

		FArdaRHIStatus Reset() override;
		FArdaRHIStatus WriteBuffer(IArdaRHIBuffer&, const void*, size_t, uint64_t) override;
		FArdaRHIStatus CopyBufferHostToDevice(IArdaRHIBuffer&, const void*, size_t, uint64_t) override;
		FArdaRHIStatus CopyBufferHostToDeviceAsync(IArdaRHIBuffer&,
		    const void*,
		    size_t,
		    FArdaRHIHostToDeviceCopyCallback,
		    uint64_t) override;
		FArdaRHIStatus CopyBufferDeviceToHost(IArdaRHIBuffer&,
		    eastl::vector<uint8_t>&,
		    uint64_t,
		    uint64_t) override;
		FArdaRHIStatus CopyBufferDeviceToHostAsync(IArdaRHIBuffer&,
		    FArdaRHIDeviceToHostCopyCallback,
		    uint64_t,
		    uint64_t) override;
		FArdaRHIStatus CopyBuffer(IArdaRHIBuffer&, uint64_t, IArdaRHIBuffer&, uint64_t, uint64_t) override;
		FArdaRHIStatus CopyTexture(IArdaRHITexture&,
		    const FArdaRHITextureSlice&,
		    IArdaRHITexture&,
		    const FArdaRHITextureSlice&) override;
		FArdaRHIStatus CopyBufferToTexture(IArdaRHITexture&,
		    const FArdaRHITextureSlice&,
		    IArdaRHIBuffer&,
		    const FArdaRHITextureBufferLayout&) override;
		FArdaRHIStatus CopyTextureToBuffer(IArdaRHIBuffer&,
		    const FArdaRHITextureBufferLayout&,
		    IArdaRHITexture&,
		    const FArdaRHITextureSlice&) override;
		FArdaRHIStatus ResolveTexture(IArdaRHITexture&,
		    const FArdaRHITextureSlice&,
		    IArdaRHITexture&,
		    const FArdaRHITextureSlice&) override;
		FArdaRHIStatus CopyTextureToStaging(IArdaRHIStagingTexture&,
		    const FArdaRHITextureSlice&,
		    IArdaRHITexture&,
		    const FArdaRHITextureSlice&) override;
		FArdaRHIStatus CopyTextureFromStaging(IArdaRHITexture&,
		    const FArdaRHITextureSlice&,
		    IArdaRHIStagingTexture&,
		    const FArdaRHITextureSlice&) override;
		FArdaRHIStatus ClearTexture(IArdaRHITexture&,
		    const FArdaRHITextureSubresourceRange&,
		    const FArdaRHIColor&) override;
		FArdaRHIStatus SetTextureState(IArdaRHITexture&,
		    const FArdaRHITextureSubresourceRange&,
		    EArdaRHIResourceState) override;
		FArdaRHIStatus SetBufferState(IArdaRHIBuffer&, EArdaRHIResourceState) override;
		FArdaRHIStatus TransitionTexture(IArdaRHITexture&, const FArdaRHITextureTransitionDesc&) override;
		FArdaRHIStatus TransitionBuffer(IArdaRHIBuffer&, const FArdaRHIBufferTransitionDesc&) override;
		FArdaRHIStatus SetAccelStructState(IArdaRHIAccelStruct&, EArdaRHIResourceState) override;
		TArdaRHIResult<FArdaRHIResourceStateSnapshot> QueryAccelStructState(IArdaRHIAccelStruct&) const override;

		void SetAutomaticBarriers(bool bEnabled) override
		{
			mNative->SetAutomaticBarriers(bEnabled);
		}

		FArdaRHIStatus BeginTrackingTextureState(IArdaRHITexture&,
		    const FArdaRHITextureSubresourceRange&,
		    EArdaRHIResourceState) override;
		FArdaRHIStatus BeginTrackingBufferState(IArdaRHIBuffer&, EArdaRHIResourceState) override;
		TArdaRHIResult<FArdaRHIResourceStateSnapshot> QueryTextureState(IArdaRHITexture&,
		    const FArdaRHITextureSubresourceRange&) const override;
		TArdaRHIResult<FArdaRHIResourceStateSnapshot> QueryBufferState(IArdaRHIBuffer&) const override;
		TArdaRHIResult<FArdaRHIResourceStateSnapshot> QuerySamplerFeedbackTextureState(
		    IArdaRHISamplerFeedbackTexture&) const override;
		FArdaRHIStatus SetUAVBarriersForTexture(IArdaRHITexture&, bool) override;
		FArdaRHIStatus SetUAVBarriersForBuffer(IArdaRHIBuffer&, bool) override;

		void CommitBarriers() override
		{
			mNative->CommitBarriers();
		}

		FArdaRHIStatus AliasingBarrier(IArdaRHIResource*, IArdaRHIResource*) override;
		FArdaRHIStatus ClearTextureUInt(IArdaRHITexture&,
		    const FArdaRHITextureSubresourceRange&,
		    uint32_t) override;
		FArdaRHIStatus ClearDepthStencilTexture(IArdaRHITexture&,
		    const FArdaRHITextureSubresourceRange&,
		    bool,
		    float,
		    bool,
		    uint8_t) override;
		FArdaRHIStatus ClearBufferUInt(IArdaRHIBuffer&, uint32_t) override;
		FArdaRHIStatus SetGraphicsState(const FArdaRHIGraphicsState&) override;
		FArdaRHIStatus SetComputeState(const FArdaRHIComputeState&) override;
		FArdaRHIStatus SetMeshletState(const FArdaRHIMeshletState&) override;
		FArdaRHIStatus SetRayTracingState(const FArdaRHIRayTracingState&) override;

		void SetPushConstants(const void* Data, size_t Size) override
		{
			if (auto Status = ValidateRecording(false); !Status)
			{
				LatchError(Status);
				return;
			}
			// Defer void-call errors to Close without reading invalid caller storage or recording native work.
			if (!Data || !Size || Size % sizeof(uint32_t) || Size > mPushConstantCapacity)
			{
				mRecordingStatus =
				    Invalid("Push constants require nonempty DWORD-aligned data fitting every bound push block.");
				return;
			}
			mNative->SetPushConstants(Data, Size);
		}

		void Draw(const FArdaRHIDrawArguments& Arguments) override;
		void DrawIndexed(const FArdaRHIDrawArguments& Arguments) override;

		FArdaRHIStatus DrawIndirect(IArdaRHIBuffer&, uint64_t, uint32_t, uint32_t) override;
		FArdaRHIStatus DrawIndexedIndirect(IArdaRHIBuffer&, uint64_t, uint32_t, uint32_t) override;

		void Dispatch(uint32_t X, uint32_t Y, uint32_t Z) override;

		FArdaRHIStatus DispatchIndirect(IArdaRHIBuffer&, uint64_t) override;
		FArdaRHIStatus DispatchMesh(uint32_t, uint32_t, uint32_t) override;
		FArdaRHIStatus DispatchRays(uint32_t, uint32_t, uint32_t) override;
		FArdaRHIStatus DispatchRaysIndirect(IArdaRHIBuffer&, uint64_t) override;
		FArdaRHIStatus BuildBottomLevelAccelStruct(IArdaRHIAccelStruct&,
		    const eastl::vector<FArdaRHIRayTracingGeometryDesc>&,
		    EArdaRHIAccelStructBuildFlags) override;
		FArdaRHIStatus BuildTopLevelAccelStruct(IArdaRHIAccelStruct&,
		    const eastl::vector<FArdaRHIRayTracingInstanceDesc>&,
		    EArdaRHIAccelStructBuildFlags) override;
		FArdaRHIStatus BuildTopLevelAccelStructFromBuffer(IArdaRHIAccelStruct&,
		    IArdaRHIBuffer&,
		    uint64_t,
		    size_t,
		    EArdaRHIAccelStructBuildFlags) override;
		FArdaRHIStatus CopyAccelStruct(IArdaRHIAccelStruct&, IArdaRHIAccelStruct&) override;
		FArdaRHIStatus CompactAccelStruct(IArdaRHIAccelStruct&, IArdaRHIAccelStruct&) override;
		FArdaRHIStatus DispatchShaderBundle(IArdaRHIShaderBundle&) override;
		FArdaRHIStatus DispatchWorkGraph(IArdaRHIWorkGraphPipeline&,
		    const void*,
		    uint32_t,
		    uint32_t,
		    const eastl::vector<FArdaRHIBindingSetRef>&) override;
		FArdaRHIStatus BuildOpacityMicromap(IArdaRHIOpacityMicromap&) override;
		FArdaRHIStatus CompactOpacityMicromap(IArdaRHIOpacityMicromap&, IArdaRHIOpacityMicromap&) override;
		TArdaRHIResult<FArdaRHIResourceStateSnapshot> QueryOpacityMicromapState(
		    IArdaRHIOpacityMicromap&) const override;
		FArdaRHIStatus ClearSamplerFeedbackTexture(IArdaRHISamplerFeedbackTexture&) override;
		FArdaRHIStatus DecodeSamplerFeedbackTexture(IArdaRHITexture&,
		    IArdaRHISamplerFeedbackTexture&,
		    EArdaRHIFormat) override;
		FArdaRHIStatus SetSamplerFeedbackTextureState(IArdaRHISamplerFeedbackTexture&,
		    EArdaRHIResourceState) override;
		FArdaRHIStatus BeginTimerQuery(IArdaRHITimerQuery&) override;
		FArdaRHIStatus EndTimerQuery(IArdaRHITimerQuery&) override;

		void BeginMarker(const char* Name) override
		{
			mNative->BeginMarker(Name);
		}

		void EndMarker() override
		{
			mNative->EndMarker();
		}

		IArdaProviderCommandList& GetNative() const noexcept
		{
			return *mNative;
		}

		[[nodiscard]] FArdaRHIStatus ValidateFacadeStartStates() const;
		void CommitFacadeStates();

		eastl::vector<FArdaPendingBufferCopyCompletion> TakeCopyCompletions()
		{
			return eastl::move(mCopyCompletions);
		}

	private:
		enum class EArdaPipelineKind : uint8_t
		{
			None,
			Graphics,
			Compute,
			Mesh,
			RayTracing
		};
		FArdaRHIStatus ValidateRecording(bool bGraphics) const;
		FArdaRHIStatus ValidateWork(EArdaPipelineKind Kind) const;
		FArdaRHIStatus ValidateDraw(const FArdaRHIDrawArguments& Arguments, bool bIndexed) const;

		void LatchError(const FArdaRHIStatus& Status)
		{
			if (mRecordingStatus && !Status)
			{
				mRecordingStatus = Status;
			}
		}

		FArdaRHIStatus QueueBufferReadback(IArdaRHIBuffer& Source,
		    uint64_t SourceOffset,
		    uint64_t Size,
		    FArdaPendingBufferCopyCompletion Completion);
		void ClearRecordingState();
		bool RetainOwned(const FArdaResource* Resource) const;
		FArdaRHIStatus ResolveBindings(const eastl::vector<FArdaRHIBindingSetRef>& Bindings,
		    eastl::vector<FArdaProviderObjectRef>& OutBindings,
		    const eastl::vector<FArdaRHIBindingLayoutRef>& Layouts) const;
		eastl::vector<EArdaRHIResourceState>& GetFacadeTextureStates(FArdaTexture& Texture) const;
		void StoreTextureState(FArdaTexture& Texture,
		    const FArdaRHITextureSubresourceRange& Range,
		    EArdaRHIResourceState State);
		TArdaRHIRef<FArdaRHIDeviceImpl> mDevice;
		EArdaRHIQueueType mQueue = EArdaRHIQueueType::Graphics;
		eastl::unique_ptr<IArdaProviderCommandList> mNative;
		FArdaRHIMeshletState mMeshletState;
		FArdaRHIGraphicsState mGraphicsState;
		EArdaPipelineKind mPipelineKind = EArdaPipelineKind::None;
		bool mbRecordingOpen = false;
		size_t mPushConstantCapacity = 0;
		FArdaRHIStatus mRecordingStatus;

		// State maps use facade identities until submission commits them.
		mutable std::unordered_map<const FArdaResource*, FArdaRHIResourceRef> mRetainedResources;
		mutable std::unordered_map<const IArdaRHIBindingLayout*, FArdaRHIBindingSetRef> mEmptyBindingSets;
		eastl::vector<FArdaPendingBufferCopyCompletion> mCopyCompletions;
		mutable std::unordered_map<FArdaTexture*, eastl::vector<EArdaRHIResourceState>> mFacadeTextureStates;
		std::unordered_map<FArdaTexture*, eastl::vector<uint8_t>> mTouchedTextureStates;
		mutable std::unordered_map<FArdaBuffer*, EArdaRHIResourceState> mFacadeBufferStates;
		mutable std::unordered_map<FArdaSamplerFeedbackTexture*, EArdaRHIResourceState>
		    mFacadeSamplerFeedbackStates;
		mutable std::unordered_map<FArdaTexture*, EArdaRHIQueueType> mFacadeTextureQueueOwners;
		mutable std::unordered_map<FArdaBuffer*, EArdaRHIQueueType> mFacadeBufferQueueOwners;

		struct FArdaAccelStructTracking
		{
			EArdaRHIResourceState mState = EArdaRHIResourceState::AccelStructRead;
			EArdaRHIAccelStructBuildState mBuildState = EArdaRHIAccelStructBuildState::Unbuilt;
			// State-only command lists must not republish an earlier lifecycle snapshot.
			bool mbLifecycleWritten = false;
		};

		mutable std::unordered_map<FArdaAccelStruct*, FArdaAccelStructTracking> mFacadeAccelStructStates;
		mutable std::unordered_map<FArdaOpacityMicromap*, FArdaAccelStructTracking> mFacadeOpacityMicromapStates;
		std::unordered_map<FArdaTexture*, eastl::vector<EArdaRHIResourceState>> mExpectedTextureStartStates;
		std::unordered_map<FArdaBuffer*, EArdaRHIResourceState> mExpectedBufferStartStates;
	};
}
