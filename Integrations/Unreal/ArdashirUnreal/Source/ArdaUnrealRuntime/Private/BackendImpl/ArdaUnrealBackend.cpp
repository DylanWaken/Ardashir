#include "BackendImpl/ArdaUnrealBackend.h"

#include "ArdaBackend.h"
#include "BackendImpl/ArdaUnrealUnsupportedCommands.h"
#include "BackendImpl/ArdaUnrealUnsupportedDevice.h"
#include "RHI/Providers/ArdaBackendProvider.h"
#include "RHI/Providers/ArdaExternalDeviceProvider.h"

#if __has_include("CUDA/ArdaCudaProviderCommands.h")
#include "CUDA/ArdaCudaProviderCommands.h"
#else
#include "RHI/Providers/ArdaProviderCommandTypes.h"
#endif

#include "DynamicRHI.h"
#include "PipelineStateCache.h"
#include "RHICommandList.h"
#include "RHIShaderParameters.h"
#include "RenderingThread.h"
#include "Misc/ScopeLock.h"

#include <EASTL/unique_ptr.h>
#include <cstring>

namespace arda
{
	namespace
	{
		class FArdaUnrealCommands;
		thread_local FRHICommandListImmediate* GArdaBorrowedList = nullptr;
		thread_local FArdaUnrealCommands* GArdaRecordingList = nullptr;
		thread_local uint64 GArdaScopeGeneration = 0;
		thread_local uint32 GArdaOpenLists = 0;
		thread_local bool bArdaReplaying = false;
		eastl::shared_ptr<IArdaUnrealCudaInterop> GArdaCudaInterop;

		FArdaRHIStatus InvalidState(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, Message);
		}

		FArdaRHIStatus Unsupported(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported, Message);
		}

		EPixelFormat ToUnrealFormat(EArdaRHIFormat Format)
		{
			switch (Format)
			{
			case EArdaRHIFormat::R8UNorm:
				return PF_G8;
			case EArdaRHIFormat::R8UInt:
				return PF_R8_UINT;
			case EArdaRHIFormat::R16Float:
				return PF_R16F;
			case EArdaRHIFormat::R16UInt:
				return PF_R16_UINT;
			case EArdaRHIFormat::R32Float:
				return PF_R32_FLOAT;
			case EArdaRHIFormat::R32UInt:
				return PF_R32_UINT;
			case EArdaRHIFormat::R32SInt:
				return PF_R32_SINT;
			case EArdaRHIFormat::RG16Float:
				return PF_G16R16F;
			case EArdaRHIFormat::RG32Float:
				return PF_G32R32F;
			case EArdaRHIFormat::RGBA8UNorm:
				return PF_R8G8B8A8;
			case EArdaRHIFormat::BGRA8UNorm:
				return PF_B8G8R8A8;
			case EArdaRHIFormat::RGBA16Float:
				return PF_FloatRGBA;
			case EArdaRHIFormat::RGBA32Float:
				return PF_A32B32G32R32F;
			case EArdaRHIFormat::R11G11B10Float:
				return PF_FloatR11G11B10;
			case EArdaRHIFormat::R10G10B10A2UNorm:
				return PF_A2B10G10R10;
			default:
				return PF_Unknown;
			}
		}

		EArdaRHIFormat FromUnrealFormat(EPixelFormat Format)
		{
			for (uint32 Index = 1; Index < static_cast<uint32>(EArdaRHIFormat::Count); ++Index)
			{
				const auto Candidate = static_cast<EArdaRHIFormat>(Index);
				if (ToUnrealFormat(Candidate) == Format && Format != PF_Unknown)
				{
					return Candidate;
				}
			}
			return EArdaRHIFormat::Unknown;
		}

		ERHIAccess ToUnrealAccess(EArdaRHIResourceState State)
		{
			switch (State)
			{
			case EArdaRHIResourceState::Unknown:
				return ERHIAccess::Unknown;
			case EArdaRHIResourceState::ShaderResource:
				return ERHIAccess::SRVMask;
			case EArdaRHIResourceState::PixelShaderResource:
				return ERHIAccess::SRVGraphicsPixel;
			case EArdaRHIResourceState::NonPixelShaderResource:
				return ERHIAccess::SRVCompute;
			case EArdaRHIResourceState::UnorderedAccess:
				return ERHIAccess::UAVCompute;
			case EArdaRHIResourceState::CopySource:
				return ERHIAccess::CopySrc;
			case EArdaRHIResourceState::CopyDest:
				return ERHIAccess::CopyDest;
			case EArdaRHIResourceState::RenderTarget:
				return ERHIAccess::RTV;
			case EArdaRHIResourceState::IndirectArgument:
				return ERHIAccess::IndirectArgs;
			case EArdaRHIResourceState::Present:
				return ERHIAccess::Present;
			default:
				return ERHIAccess::Unknown;
			}
		}

		bool IsWholeTexture(const FArdaRHITextureSubresourceRange& Range, const FArdaRHITextureDesc& Desc)
		{
			const auto Resolved = Range.Resolve(Desc);
			return Resolved.mBaseMipLevel == 0 && Resolved.mMipLevelCount == Desc.mMipLevels &&
			    Resolved.mBaseArraySlice == 0 && Resolved.mArraySliceCount == Desc.mArraySize &&
			    Resolved.mBasePlane == 0 && Resolved.mPlaneCount == 1;
		}

		/** Object classes are private to this provider; the core rejects cross-device references. */
		class FArdaUnrealObject final : public IArdaProviderObject
		{
		public:
			const void* GetIdentity() const noexcept override
			{
				if (mTexture)
				{
					return mTexture.GetReference();
				}
				if (mBuffer)
				{
					return mBuffer.GetReference();
				}
				return this;
			}

			FArdaCudaResourceInfo GetCudaResourceInfo() const noexcept override
			{
				return mCudaInfo;
			}

			FTextureRHIRef mTexture;
			FBufferRHIRef mBuffer;
			FComputeShaderRHIRef mShader;
			FArdaRHIBindingLayoutDesc mLayout;
			eastl::vector<FArdaProviderBinding> mBindings;
			TArray<FShaderResourceViewRHIRef> mSRVs;
			TArray<FUnorderedAccessViewRHIRef> mUAVs;
			eastl::shared_ptr<void> mLifetime;
			eastl::shared_ptr<IArdaUnrealCudaInterop> mCudaInterop;
			FArdaCudaResourceInfo mCudaInfo;
			EArdaRHIResourceState mState = EArdaRHIResourceState::Unknown;
		};

		FArdaUnrealObject& GetObject(const FArdaProviderObjectRef& Object)
		{
			return static_cast<FArdaUnrealObject&>(*Object);
		}

		/** In-process shader import payload, never persisted as a shader artifact. */
		struct FArdaUnrealShaderPayload
		{
			uint64 mMagic = 0x4152444155454353ull;
			FRHIComputeShader* mShader = nullptr;
		};

		class FArdaUnrealCommands final : public FArdaUnrealUnsupportedCommands
		{
		public:
			explicit FArdaUnrealCommands(uint64 ScopeGeneration,
			    bool bDeferred,
			    eastl::shared_ptr<IArdaUnrealCudaInterop> CudaInterop)
			    : mScopeGeneration(ScopeGeneration),
			      mbDeferred(bDeferred),
			      mCudaInterop(eastl::move(CudaInterop))
			{
			}

			~FArdaUnrealCommands() override
			{
				if (mbOpen && mScopeGeneration == GArdaScopeGeneration && GArdaOpenLists > 0)
				{
					FinishMarkers();
					--GArdaOpenLists;
					GArdaRecordingList = nullptr;
				}
			}

			bool IsOpen() const noexcept override
			{
				return mbOpen;
			}

			FArdaRHIStatus Open() override
			{
				if (!IsInScope() || mbOpen || GArdaOpenLists != 0 || mbExecuted || mbClosed || bArdaReplaying)
				{
					return InvalidState("Open requires one fresh Arda list inside the current Unreal RHI scope.");
				}
				mbOpen = true;
				++GArdaOpenLists;
				GArdaRecordingList = this;
				mStatus = {};
				return {};
			}

			FArdaRHIStatus Close() override
			{
				if (!IsInScope() || !mbOpen)
				{
					return InvalidState("Close requires an open Arda list in its original Unreal RHI scope.");
				}
				mbOpen = false;
				mbClosed = true;
				FinishMarkers();
				--GArdaOpenLists;
				GArdaRecordingList = nullptr;
				return mStatus;
			}

			FArdaRHIStatus Reset() override
			{
				return Unsupported("Borrowed Unreal recording cannot be rewound; create a fresh Arda command list.");
			}

			bool CanExecute() const
			{
				return IsInScope() && mbClosed && !mbOpen && !mbExecuted && GArdaOpenLists == 0 && !bArdaReplaying;
			}

			void MarkExecuted()
			{
				mbExecuted = true;
			}

			const eastl::vector<FArdaProviderObjectRef>& GetRetainedResources() const
			{
				return mRetainedResources;
			}

			const eastl::vector<eastl::function<FArdaRHIStatus()>>& GetRetainedCallbacks() const
			{
				return mOperations;
			}

			const eastl::vector<FArdaCudaKernel>& GetRetainedKernels() const
			{
				return mRetainedKernels;
			}

			FArdaRHIStatus Replay()
			{
				if (!CanExecute())
				{
					return InvalidState("Deferred replay requires the original Unreal scope.");
				}
				if (!mStatus)
				{
					return mStatus;
				}
				TGuardValue<bool> ReplayGuard(bArdaReplaying, true);
				mbDeferred = false;
				mTrackedStates.Reset();
				mbOpen = true;
				for (const auto& Operation : mOperations)
				{
					mStatus = Operation();
					if (!mStatus)
					{
						break;
					}
				}
				mbOpen = false;
				FinishMarkers();
				return mStatus;
			}

			FArdaRHIStatus RecordHost(eastl::function<FArdaRHIStatus(FRHICommandListImmediate&)> Callback)
			{
				if (auto Status = RequireOpen(); !Status)
				{
					return Status;
				}
				if (!Callback)
				{
					return InvalidState("A recorded Unreal callback cannot be empty.");
				}
				if (mbDeferred)
				{
					mOperations.push_back(
					    [Callback = eastl::move(Callback)]()
					    {
						    return Callback(*GArdaBorrowedList);
					    });
					return {};
				}
				mStatus = Callback(*GArdaBorrowedList);
				// Preserve captured RHI references even if a later immediate operation fails.
				mOperations.push_back(
				    [Callback = eastl::move(Callback)]()
				    {
					    static_cast<void>(Callback);
					    return FArdaRHIStatus{};
				    });
				return mStatus;
			}

			FArdaRHIStatus DispatchCuda(const eastl::vector<FArdaProviderCudaBinding>& Bindings,
			    const eastl::vector<FArdaCudaKernel>& Kernels) override
			{
				if (auto Status = RequireOpen(); !Status)
				{
					return Status;
				}
				if (!mCudaInterop)
				{
					return Unsupported("Install a qualified host CUDA interop bridge before Arda initialization.");
				}
				mRetainedKernels.insert(mRetainedKernels.end(), Kernels.begin(), Kernels.end());
				if (mbDeferred)
				{
					mOperations.push_back(
					    [this, Bindings, Kernels]()
					    {
						    return DispatchCuda(Bindings, Kernels);
					    });
					return {};
				}
				eastl::vector<FArdaUnrealCudaBinding> HostBindings;
				for (const auto& Binding : Bindings)
				{
					mRetainedResources.push_back(Binding.mObject);
					const auto& Object = GetObject(Binding.mObject);
					if (!Object.mCudaInfo.mbSharingEnabled || Object.mCudaInterop != mCudaInterop)
					{
						return Unsupported("CUDA requires a resource qualified by this device's host bridge.");
					}
					FArdaUnrealCudaBinding Host;
					Host.mTexture = Object.mTexture;
					Host.mBuffer = Object.mBuffer;
					Host.mType = Binding.mType;
					Host.mAccess = Binding.mAccess;
					Host.mMipLevel = Binding.mMipLevel;
					Host.mBufferRange = Binding.mBufferRange;
					HostBindings.push_back(eastl::move(Host));
				}
				return mCudaInterop->Dispatch(*GArdaBorrowedList, HostBindings, Kernels);
			}

			FArdaRHIStatus SetComputeState(const FArdaProviderComputeState& State) override
			{
				if (auto Status = RequireOpen(); !Status)
				{
					return Status;
				}
				if (!State.mPipeline || !GetObject(State.mPipeline).mShader)
				{
					return InvalidState("The compute pipeline must contain an imported Unreal compute shader.");
				}
				mShader = GetObject(State.mPipeline).mShader;
				mRetainedResources.push_back(State.mPipeline);
				mRetainedResources.insert(mRetainedResources.end(), State.mBindings.begin(), State.mBindings.end());
				if (mbAutomaticBarriers)
				{
					for (const auto& Set : State.mBindings)
					{
						for (const auto& Binding : GetObject(Set).mBindings)
						{
							const auto Required = Binding.mItem.mType == EArdaRHIBindingType::TextureUAV
							    ? EArdaRHIResourceState::UnorderedAccess
							    : EArdaRHIResourceState::NonPixelShaderResource;
							if (auto Status = ChangeState(Binding.mObject, Required); !Status)
							{
								return Status;
							}
						}
					}
				}
				if (mbDeferred)
				{
					mOperations.push_back(
					    [this, State]()
					    {
						    // The matching transitions were recorded immediately before this operation.
						    TGuardValue<bool> BarrierGuard(mbAutomaticBarriers, false);
						    return SetComputeState(State);
					    });
					return {};
				}
				SetComputePipelineState(*GArdaBorrowedList, mShader);
				auto& Parameters = GArdaBorrowedList->GetScratchShaderParameters();
				for (const auto& BindingSet : State.mBindings)
				{
					auto& Set = GetObject(BindingSet);
					for (size_t Index = 0; Index < Set.mBindings.size(); ++Index)
					{
						const auto& Binding = Set.mBindings[Index];
						if (Binding.mItem.mType == EArdaRHIBindingType::TextureSRV)
						{
							Parameters.SetShaderResourceViewParameter(Binding.mItem.mSlot,
							    Set.mSRVs[static_cast<int32>(Index)]);
						}
						else
						{
							Parameters.SetUAVParameter(Binding.mItem.mSlot, Set.mUAVs[static_cast<int32>(Index)]);
						}
					}
				}
				GArdaBorrowedList->SetBatchedShaderParameters(mShader, Parameters);
				return {};
			}

			void Dispatch(uint32_t X, uint32_t Y, uint32_t Z) override
			{
				mStatus = RequireOpen();
				if (!mStatus)
				{
					return;
				}
				if (!mShader || X == 0 || Y == 0 || Z == 0)
				{
					mStatus = InvalidState("Dispatch requires a compute pipeline and nonzero group counts.");
					return;
				}
				if (mbDeferred)
				{
					mOperations.push_back(
					    [this, X, Y, Z]()
					    {
						    Dispatch(X, Y, Z);
						    return mStatus;
					    });
					return;
				}
				GArdaBorrowedList->DispatchComputeShader(X, Y, Z);
			}

			FArdaRHIStatus CopyBuffer(const FArdaProviderObjectRef& Destination,
			    uint64_t DestinationOffset,
			    const FArdaProviderObjectRef& Source,
			    uint64_t SourceOffset,
			    uint64_t Size) override
			{
				if (auto Status = RequireOpen(); !Status)
				{
					return Status;
				}
				if (!GetObject(Destination).mBuffer || !GetObject(Source).mBuffer)
				{
					return InvalidState("CopyBuffer requires imported or provider-created Unreal buffers.");
				}
				mRetainedResources.push_back(Destination);
				mRetainedResources.push_back(Source);
				if (mbAutomaticBarriers)
				{
					if (auto Status = ChangeState(Source, EArdaRHIResourceState::CopySource); !Status)
					{
						return Status;
					}
					if (auto Status = ChangeState(Destination, EArdaRHIResourceState::CopyDest); !Status)
					{
						return Status;
					}
				}
				if (mbDeferred)
				{
					mOperations.push_back(
					    [this, Destination, DestinationOffset, Source, SourceOffset, Size]()
					    {
						    TGuardValue<bool> BarrierGuard(mbAutomaticBarriers, false);
						    return CopyBuffer(Destination, DestinationOffset, Source, SourceOffset, Size);
					    });
					return {};
				}
				GArdaBorrowedList->CopyBufferRegion(GetObject(Destination).mBuffer,
				    DestinationOffset,
				    GetObject(Source).mBuffer,
				    SourceOffset,
				    Size);
				return {};
			}

			FArdaRHIStatus CopyTexture(const FArdaProviderObjectRef& Destination,
			    const FArdaRHITextureDesc& DestinationDesc,
			    const FArdaRHITextureSlice& DestinationSlice,
			    const FArdaProviderObjectRef& Source,
			    const FArdaRHITextureDesc& SourceDesc,
			    const FArdaRHITextureSlice& SourceSlice) override
			{
				if (auto Status = RequireOpen(); !Status)
				{
					return Status;
				}
				FArdaRHITextureCopyExtent Extent;
				if (auto Status = ResolveArdaRHITextureCopyExtent(DestinationDesc,
				        DestinationSlice,
				        SourceDesc,
				        SourceSlice,
				        Extent);
				    !Status)
				{
					return Status;
				}
				FRHICopyTextureInfo Info;
				mRetainedResources.push_back(Destination);
				mRetainedResources.push_back(Source);
				if (mbAutomaticBarriers)
				{
					if (auto Status = ChangeState(Source, EArdaRHIResourceState::CopySource); !Status)
					{
						return Status;
					}
					if (auto Status = ChangeState(Destination, EArdaRHIResourceState::CopyDest); !Status)
					{
						return Status;
					}
				}
				if (mbDeferred)
				{
					mOperations.push_back(
					    [this, Destination, DestinationDesc, DestinationSlice, Source, SourceDesc, SourceSlice]()
					    {
						    TGuardValue<bool> BarrierGuard(mbAutomaticBarriers, false);
						    return CopyTexture(Destination,
						        DestinationDesc,
						        DestinationSlice,
						        Source,
						        SourceDesc,
						        SourceSlice);
					    });
					return {};
				}
				Info.SourcePosition = FIntVector(SourceSlice.mX, SourceSlice.mY, SourceSlice.mZ);
				Info.DestPosition = FIntVector(DestinationSlice.mX, DestinationSlice.mY, DestinationSlice.mZ);
				Info.Size = FIntVector(Extent.mWidth, Extent.mHeight, Extent.mDepth);
				Info.SourceMipIndex = SourceSlice.mMipLevel;
				Info.DestMipIndex = DestinationSlice.mMipLevel;
				Info.SourceSliceIndex = SourceSlice.mArraySlice;
				Info.DestSliceIndex = DestinationSlice.mArraySlice;
				GArdaBorrowedList->CopyTexture(GetObject(Source).mTexture, GetObject(Destination).mTexture, Info);
				return {};
			}

			FArdaRHIStatus SetTextureState(const FArdaProviderObjectRef& Texture,
			    const FArdaRHITextureDesc& Desc,
			    const FArdaRHITextureSubresourceRange& Range,
			    EArdaRHIResourceState State) override
			{
				if (!IsWholeTexture(Range, Desc))
				{
					return Unsupported("Partial texture state tracking is not implemented by unreal-rhi.");
				}
				return ChangeState(Texture, State);
			}

			FArdaRHIStatus SetBufferState(const FArdaProviderObjectRef& Buffer,
			    const FArdaRHIBufferDesc&,
			    EArdaRHIResourceState State) override
			{
				return ChangeState(Buffer, State);
			}

			FArdaRHIStatus TransitionTexture(const FArdaProviderObjectRef& Texture,
			    const FArdaRHITextureDesc& Desc,
			    const FArdaRHITextureTransitionDesc& Transition) override
			{
				if (auto Status = RequireOpen(); !Status)
				{
					return Status;
				}
				if (Transition.mFlags != EArdaRHITransitionFlags::None || Transition.mbQueueOwnershipTransfer ||
				    Transition.mSourcePipelines != EArdaRHIPipeline::Graphics ||
				    Transition.mDestinationPipelines != EArdaRHIPipeline::Graphics)
				{
					return Unsupported(
					    "unreal-rhi supports whole-resource transitions on the borrowed graphics list only.");
				}
				if (Transition.mStateBefore != EArdaRHIResourceState::Unknown &&
				    GetTrackedState(Texture) != Transition.mStateBefore)
				{
					return InvalidState("Texture before-state disagrees with its imported or tracked Unreal state.");
				}
				return SetTextureState(Texture, Desc, Transition.mSubresources, Transition.mStateAfter);
			}

			FArdaRHIStatus TransitionBuffer(const FArdaProviderObjectRef& Buffer,
			    const FArdaRHIBufferDesc& Desc,
			    const FArdaRHIBufferTransitionDesc& Transition) override
			{
				if (auto Status = RequireOpen(); !Status)
				{
					return Status;
				}
				if (Transition.mFlags != EArdaRHITransitionFlags::None || Transition.mbQueueOwnershipTransfer ||
				    Transition.mSourcePipelines != EArdaRHIPipeline::Graphics ||
				    Transition.mDestinationPipelines != EArdaRHIPipeline::Graphics)
				{
					return Unsupported("unreal-rhi does not expose split barriers or queue ownership transfers.");
				}
				if (Transition.mStateBefore != EArdaRHIResourceState::Unknown &&
				    GetTrackedState(Buffer) != Transition.mStateBefore)
				{
					return InvalidState("Buffer before-state disagrees with its imported or tracked Unreal state.");
				}
				return SetBufferState(Buffer, Desc, Transition.mStateAfter);
			}

			void SetAutomaticBarriers(bool bEnabled) override
			{
				mbAutomaticBarriers = bEnabled;
			}

			FArdaRHIStatus BeginTrackingTextureState(const FArdaProviderObjectRef& Texture,
			    const FArdaRHITextureDesc& Desc,
			    const FArdaRHITextureSubresourceRange& Range,
			    EArdaRHIResourceState State) override
			{
				if (auto Status = RequireOpen(); !Status)
				{
					return Status;
				}
				if (!IsWholeTexture(Range, Desc))
				{
					return Unsupported("Partial texture tracking is unavailable.");
				}
				if (ToUnrealAccess(State) == ERHIAccess::Unknown)
				{
					return Unsupported("Texture tracking requires an explicit qualified Unreal access state.");
				}
				if (mbDeferred)
				{
					mTrackedStates.Add(Texture.get(), State);
					mOperations.push_back(
					    [this, Texture, Desc, Range, State]()
					    {
						    return BeginTrackingTextureState(Texture, Desc, Range, State);
					    });
					return {};
				}
				GetObject(Texture).mState = State;
				return {};
			}

			FArdaRHIStatus BeginTrackingBufferState(const FArdaProviderObjectRef& Buffer,
			    const FArdaRHIBufferDesc&,
			    EArdaRHIResourceState State) override
			{
				if (auto Status = RequireOpen(); !Status)
				{
					return Status;
				}
				if (ToUnrealAccess(State) == ERHIAccess::Unknown)
				{
					return Unsupported("Buffer tracking requires an explicit qualified Unreal access state.");
				}
				if (mbDeferred)
				{
					mTrackedStates.Add(Buffer.get(), State);
					mOperations.push_back(
					    [Buffer, State]()
					    {
						    GetObject(Buffer).mState = State;
						    return FArdaRHIStatus{};
					    });
					return {};
				}
				GetObject(Buffer).mState = State;
				return {};
			}

			TArdaRHIResult<FArdaRHINativeResourceState> QueryTextureState(const FArdaProviderObjectRef& Texture,
			    const FArdaRHITextureDesc& Desc,
			    const FArdaRHITextureSubresourceRange& Range) const override
			{
				if (!IsWholeTexture(Range, Desc))
				{
					return {{}, Unsupported("Partial texture tracking is unavailable.")};
				}
				return QueryState(Texture);
			}

			TArdaRHIResult<FArdaRHINativeResourceState> QueryBufferState(const FArdaProviderObjectRef& Buffer,
			    const FArdaRHIBufferDesc&) const override
			{
				return QueryState(Buffer);
			}

			FArdaRHIStatus SetUAVBarriersForTexture(const FArdaProviderObjectRef&, bool bEnabled) override
			{
				return bEnabled ? FArdaRHIStatus{} : Unsupported("Disabling Unreal UAV barriers is unsupported.");
			}

			FArdaRHIStatus SetUAVBarriersForBuffer(const FArdaProviderObjectRef&, bool bEnabled) override
			{
				return bEnabled ? FArdaRHIStatus{} : Unsupported("Disabling Unreal UAV barriers is unsupported.");
			}

			void CommitBarriers() override
			{ /* Transitions append immediately to the borrowed list. */
			}

			void BeginMarker(const char* Name) override
			{
				mStatus = RequireOpen();
				if (mStatus && mbDeferred)
				{
					const eastl::string Label = Name ? Name : "Arda";
					mOperations.push_back(
					    [this, Label]()
					    {
						    BeginMarker(Label.c_str());
						    return mStatus;
					    });
					return;
				}
				if (mStatus)
				{
#if WITH_RHI_BREADCRUMBS
					mMarkers.Add(MakeUnique<FRHIBreadcrumbEventManual>(*GArdaBorrowedList,
					    RHI_BREADCRUMB_DESC_COPY_VALUES(TEXT("Arda"), TEXT("%s"), RHI_GPU_STAT_ARGS_NONE)(
					        FString(UTF8_TO_TCHAR(Name ? Name : "Arda")))));
#endif
				}
			}

			void EndMarker() override
			{
				mStatus = RequireOpen();
				if (mStatus && mbDeferred)
				{
					mOperations.push_back(
					    [this]()
					    {
						    EndMarker();
						    return mStatus;
					    });
					return;
				}
				if (mStatus)
				{
#if WITH_RHI_BREADCRUMBS
					if (mMarkers.IsEmpty())
					{
						mStatus = InvalidState("EndMarker requires a matching BeginMarker.");
						return;
					}
					auto Event = mMarkers.Pop(EAllowShrinking::No);
					Event->End(*GArdaBorrowedList);
#endif
				}
			}

		private:
			void FinishMarkers()
			{
#if WITH_RHI_BREADCRUMBS
				if (!mMarkers.IsEmpty() && mStatus)
				{
					mStatus = InvalidState("Arda recording has unbalanced GPU markers.");
				}
				while (!mMarkers.IsEmpty())
				{
					auto Event = mMarkers.Pop(EAllowShrinking::No);
					Event->End(*GArdaBorrowedList);
				}
#endif
			}

			EArdaRHIResourceState GetTrackedState(const FArdaProviderObjectRef& Resource) const
			{
				const auto* State = mTrackedStates.Find(Resource.get());
				return State ? *State : GetObject(Resource).mState;
			}

			bool IsInScope() const
			{
				return GArdaBorrowedList && mScopeGeneration == GArdaScopeGeneration && IsInRenderingThread();
			}

			FArdaRHIStatus RequireOpen() const
			{
				if (!mStatus)
				{
					return mStatus;
				}
				return IsInScope() && mbOpen ? FArdaRHIStatus{}
				                             : InvalidState("Arda commands require their open Unreal RHI scope.");
			}

			FArdaRHIStatus ChangeState(const FArdaProviderObjectRef& Resource, EArdaRHIResourceState State)
			{
				if (auto Status = RequireOpen(); !Status)
				{
					return Status;
				}
				const ERHIAccess After = ToUnrealAccess(State);
				mRetainedResources.push_back(Resource);
				if (After == ERHIAccess::Unknown)
				{
					return Unsupported("The requested resource state has no qualified Unreal mapping.");
				}
				if (mbDeferred)
				{
					mTrackedStates.Add(Resource.get(), State);
					mOperations.push_back(
					    [this, Resource, State]()
					    {
						    return ChangeState(Resource, State);
					    });
					return {};
				}
				auto& Object = GetObject(Resource);
				const ERHIAccess Before = ToUnrealAccess(Object.mState);
				if (Before != After || State == EArdaRHIResourceState::UnorderedAccess)
				{
					if (Object.mTexture)
					{
						GArdaBorrowedList->Transition(FRHITransitionInfo(Object.mTexture, Before, After));
					}
					else if (Object.mBuffer)
					{
						GArdaBorrowedList->Transition(FRHITransitionInfo(Object.mBuffer, Before, After));
					}
					else
					{
						return InvalidState("Transitions require a texture or buffer.");
					}
				}
				Object.mState = State;
				return {};
			}

			TArdaRHIResult<FArdaRHINativeResourceState> QueryState(const FArdaProviderObjectRef& Resource) const
			{
				if (!IsInScope())
				{
					return {{}, InvalidState("State queries require the original Unreal RHI scope.")};
				}
				FArdaRHINativeResourceState State;
				State.mState = GetTrackedState(Resource);
				State.mPrimaryState = static_cast<uint64>(ToUnrealAccess(State.mState));
				State.mbKnown = State.mState != EArdaRHIResourceState::Unknown;
				State.mbNativeCompatible = ToUnrealAccess(State.mState) != ERHIAccess::Unknown;
				return {State, {}};
			}

			uint64 mScopeGeneration = 0;
			bool mbOpen = false;
			bool mbClosed = false;
			bool mbExecuted = false;
			bool mbAutomaticBarriers = true;
			bool mbDeferred = false;
			FComputeShaderRHIRef mShader;
			eastl::shared_ptr<IArdaUnrealCudaInterop> mCudaInterop;
			eastl::vector<eastl::function<FArdaRHIStatus()>> mOperations;
			eastl::vector<FArdaProviderObjectRef> mRetainedResources;
			eastl::vector<FArdaCudaKernel> mRetainedKernels;
			TMap<IArdaProviderObject*, EArdaRHIResourceState> mTrackedStates;
#if WITH_RHI_BREADCRUMBS
			TArray<TUniquePtr<FRHIBreadcrumbEventManual>> mMarkers;
#endif
		};

		struct FArdaUnrealSubmission
		{
			FGPUFenceRHIRef mFence;
			eastl::vector<FArdaProviderObjectRef> mResources;
			eastl::vector<eastl::function<FArdaRHIStatus()>> mCallbacks;
			eastl::vector<FArdaCudaKernel> mKernels;
		};

		class FArdaUnrealDevice final : public FArdaUnrealUnsupportedDevice
		{
		public:
			FArdaUnrealDevice()
			    : mCudaInterop(GArdaCudaInterop)
			{
				mCapabilities.mbTextureCopies = true;
				mCapabilities.mbExplicitTransitions = true;
				mCapabilities.mLimits.mMaxTexture2D = GMaxTextureDimensions;
				mCapabilities.mLimits.mMaxComputeWorkGroupCount[0] = 65535;
				mCapabilities.mLimits.mMaxComputeWorkGroupCount[1] = 65535;
				mCapabilities.mLimits.mMaxComputeWorkGroupCount[2] = 65535;
#if __has_include("RHI/Config/ArdaBackendFeatures.h")
				// Older pinned Arda SDKs predate the provider feature list.
				mCapabilities.PublishBackendFeatures({});
#endif
			}

			const FArdaRHICapabilities& GetCapabilities() const noexcept override
			{
				return mCapabilities;
			}

			FArdaCudaCapabilities GetCudaCapabilities() const override
			{
				return mCudaInterop ? mCudaInterop->GetCapabilities() : FArdaCudaCapabilities{};
			}

			FArdaRHIFormatSupport QueryFormatSupport(EArdaRHIFormat Format) const noexcept override
			{
				FArdaRHIFormatSupport Result;
				const EPixelFormat Native = ToUnrealFormat(Format);
				if (Native == PF_Unknown || !GPixelFormats[Native].Supported)
				{
					return Result;
				}
				const auto Flags = GPixelFormats[Native].Capabilities;
				Result.mNativeFormat = static_cast<uint64>(Native);
				Result.mbTexture2D = EnumHasAnyFlags(Flags, EPixelFormatCapabilities::Texture2D);
				Result.mbShaderResource = EnumHasAnyFlags(Flags,
				    EPixelFormatCapabilities::TextureLoad | EPixelFormatCapabilities::TextureSample);
				Result.mbFilterable = EnumHasAnyFlags(Flags, EPixelFormatCapabilities::TextureFilterable);
				Result.mbStorage = EnumHasAnyFlags(Flags, EPixelFormatCapabilities::UAV);
				Result.mbStorageLoad = EnumHasAnyFlags(Flags, EPixelFormatCapabilities::TypedUAVLoad);
				Result.mbStorageStore = EnumHasAnyFlags(Flags, EPixelFormatCapabilities::TypedUAVStore);
				Result.mbColorAttachment = EnumHasAnyFlags(Flags, EPixelFormatCapabilities::RenderTarget);
				Result.mSampleCounts = 1;
				return Result;
			}

			TArdaRHIResult<FArdaRHIMemoryRequirements> GetTextureMemoryRequirements(
			    const FArdaProviderObjectRef& Texture,
			    const FArdaRHITextureDesc&) override
			{
				const auto Native = GetObject(Texture).mTexture;
				if (!Native || !GDynamicRHI)
				{
					return {{}, InvalidState("Texture requirements need an active Unreal texture.")};
				}
				const auto Size = RHICalcTexturePlatformSize(Native->GetDesc());
				if (!Size.Size || !Size.Align)
				{
					return {{}, Unsupported("Unreal did not report texture allocation requirements.")};
				}
				return {{Size.Size, Size.Align, 0xffffffffu}, {}};
			}

			TArdaRHIResult<FArdaRHIMemoryRequirements> QueryTextureMemoryRequirements(
			    const FArdaRHITextureDesc& Desc) override
			{
				if (!GDynamicRHI || Desc.mDimension != EArdaRHITextureDimension::Texture2D || Desc.mSampleCount != 1 ||
				    Desc.mbVirtual || Desc.mbTiled || Desc.mbCudaInterop || ToUnrealFormat(Desc.mFormat) == PF_Unknown)
				{
					return {{},
					    Unsupported(
					        "Texture allocation queries require a supported ordinary Unreal 2D texture descriptor.")};
				}
				ETextureCreateFlags Flags = ETextureCreateFlags::None;
				if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::ShaderResource))
				{
					Flags |= ETextureCreateFlags::ShaderResource;
				}
				if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::UnorderedAccess))
				{
					Flags |= ETextureCreateFlags::UAV;
				}
				if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::RenderTarget))
				{
					Flags |= ETextureCreateFlags::RenderTargetable;
				}
				const auto Native = FRHITextureCreateDesc::Create2D(TEXT("ArdaTextureRequirements"),
				    Desc.mWidth,
				    Desc.mHeight,
				    ToUnrealFormat(Desc.mFormat))
				                        .SetNumMips(Desc.mMipLevels)
				                        .SetFlags(Flags);
				const auto Size = RHICalcTexturePlatformSize(Native);
				if (!Size.Size || !Size.Align)
				{
					return {{}, Unsupported("Unreal did not report texture allocation requirements.")};
				}
				return {{Size.Size, Size.Align, 0xffffffffu}, {}};
			}

			FArdaProviderObjectResult CreateTexture(const FArdaRHITextureDesc& Desc) override
			{
				if (!GArdaBorrowedList)
				{
					return {{}, InvalidState("Texture creation requires an Unreal RHI scope.")};
				}
				if (ToUnrealAccess(Desc.mInitialState) == ERHIAccess::Unknown)
				{
					return {{},
					    Unsupported(
					        "Texture creation requires an explicit qualified Unreal access state; Common is not portable.")};
				}
				if (Desc.mDimension != EArdaRHITextureDimension::Texture2D || Desc.mSampleCount != 1 ||
				    Desc.mbVirtual || Desc.mbTiled || Desc.mbCudaInterop || Desc.mbUseClearValue ||
				    Desc.mbKeepInitialState || ToUnrealFormat(Desc.mFormat) == PF_Unknown ||
				    HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::DepthStencil | EArdaRHITextureUsage::Typeless))
				{
					return {{},
					    Unsupported(
					        "unreal-rhi creates ordinary 2D single-sample color textures; CUDA sharing uses the explicit host bridge.")};
				}
				ETextureCreateFlags Flags = ETextureCreateFlags::None;
				if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::ShaderResource))
				{
					Flags |= ETextureCreateFlags::ShaderResource;
				}
				if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::UnorderedAccess))
				{
					Flags |= ETextureCreateFlags::UAV;
				}
				if (HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::RenderTarget))
				{
					Flags |= ETextureCreateFlags::RenderTargetable;
				}
				const FString DebugName = UTF8_TO_TCHAR(Desc.mDebugName.c_str());
				auto NativeDesc =
				    FRHITextureCreateDesc::Create2D(*DebugName, Desc.mWidth, Desc.mHeight, ToUnrealFormat(Desc.mFormat))
				        .SetNumMips(Desc.mMipLevels)
				        .SetFlags(Flags)
				        .SetInitialState(ToUnrealAccess(Desc.mInitialState));
				auto Object = eastl::make_shared<FArdaUnrealObject>();
				Object->mTexture = GArdaBorrowedList->CreateTexture(NativeDesc);
				Object->mState = Desc.mInitialState;
				return {Object, Object->mTexture ? FArdaRHIStatus{} : InvalidState("Unreal failed to create texture.")};
			}

			FArdaProviderObjectResult CreateBuffer(const FArdaRHIBufferDesc& Desc) override
			{
				if (!GArdaBorrowedList)
				{
					return {{}, InvalidState("Buffer creation requires an Unreal RHI scope.")};
				}
				if (ToUnrealAccess(Desc.mInitialState) == ERHIAccess::Unknown)
				{
					return {{},
					    Unsupported(
					        "Buffer creation requires an explicit qualified Unreal access state; Common is not portable.")};
				}
				constexpr auto Supported = EArdaRHIBufferUsage::ShaderResource | EArdaRHIBufferUsage::UnorderedAccess |
				    EArdaRHIBufferUsage::Raw | EArdaRHIBufferUsage::Structured;
				if (Desc.mByteSize > MAX_uint32 || Desc.mbVirtual || Desc.mbTiled || Desc.mbCudaInterop ||
				    Desc.mbKeepInitialState || Desc.mCpuAccess != EArdaRHICpuAccess::None ||
				    (static_cast<uint32>(Desc.mUsage) & ~static_cast<uint32>(Supported)) != 0)
				{
					return {{},
					    Unsupported(
					        "unreal-rhi creates GPU-only compute buffers up to 4 GiB with no explicit placement or CUDA sharing.")};
				}
				EBufferUsageFlags Flags = EBufferUsageFlags::SourceCopy;
				if (HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::ShaderResource))
				{
					Flags |= EBufferUsageFlags::ShaderResource;
				}
				if (HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::UnorderedAccess))
				{
					Flags |= EBufferUsageFlags::UnorderedAccess;
				}
				if (HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::Raw))
				{
					Flags |= EBufferUsageFlags::ByteAddressBuffer;
				}
				if (HasAnyFlags(Desc.mUsage, EArdaRHIBufferUsage::Structured))
				{
					Flags |= EBufferUsageFlags::StructuredBuffer;
				}
				const FString DebugName = UTF8_TO_TCHAR(Desc.mDebugName.c_str());
				auto NativeDesc = FRHIBufferCreateDesc::Create(*DebugName,
				    static_cast<uint32>(Desc.mByteSize),
				    Desc.mStructureStride,
				    Flags)
				                      .SetInitialState(ToUnrealAccess(Desc.mInitialState));
				auto Object = eastl::make_shared<FArdaUnrealObject>();
				Object->mBuffer = GArdaBorrowedList->CreateBuffer(NativeDesc);
				Object->mState = Desc.mInitialState;
				return {Object, Object->mBuffer ? FArdaRHIStatus{} : InvalidState("Unreal failed to create buffer.")};
			}

			FArdaProviderObjectResult ImportTexture(const FArdaRHINativeTextureImportDesc& Desc) override
			{
				if (!IsInRenderingThread() || Desc.mNativeTypeName != "unreal-rhi.texture" || !Desc.mNativeObject ||
				    Desc.mOwnership != EArdaRHINativeOwnership::Borrowed)
				{
					return {{},
					    InvalidState("Texture import requires a borrowed unreal-rhi.texture on the render thread.")};
				}
				auto* Texture = reinterpret_cast<FRHITexture*>(Desc.mNativeObject);
				if (ToUnrealAccess(Desc.mInitialState) == ERHIAccess::Unknown)
				{
					return {{},
					    Unsupported(
					        "Texture import requires an explicit qualified Unreal access state; Common is not portable.")};
				}
				const auto& Native = Texture->GetDesc();
				if (Native.Dimension != ETextureDimension::Texture2D || Native.NumSamples != 1 ||
				    Desc.mTexture.mbKeepInitialState || Desc.mTexture.mbVirtual || Desc.mTexture.mbTiled ||
				    EnumHasAnyFlags(Native.Flags, ETextureCreateFlags::SRGB) ||
				    Native.Extent.X != static_cast<int32>(Desc.mTexture.mWidth) ||
				    Native.Extent.Y != static_cast<int32>(Desc.mTexture.mHeight) ||
				    Native.NumMips != Desc.mTexture.mMipLevels ||
				    Native.Format != ToUnrealFormat(Desc.mTexture.mFormat))
				{
					return {{},
					    Unsupported("Texture import descriptor must exactly match a supported 2D color texture.")};
				}
				if ((HasAnyFlags(Desc.mTexture.mUsage, EArdaRHITextureUsage::ShaderResource) &&
				        !EnumHasAnyFlags(Native.Flags, ETextureCreateFlags::ShaderResource)) ||
				    (HasAnyFlags(Desc.mTexture.mUsage, EArdaRHITextureUsage::UnorderedAccess) &&
				        !EnumHasAnyFlags(Native.Flags, ETextureCreateFlags::UAV)) ||
				    (HasAnyFlags(Desc.mTexture.mUsage, EArdaRHITextureUsage::RenderTarget) &&
				        !EnumHasAnyFlags(Native.Flags, ETextureCreateFlags::RenderTargetable)))
				{
					return {{}, InvalidState("Texture import usage must be present on the native Unreal resource.")};
				}
				auto Object = eastl::make_shared<FArdaUnrealObject>();
				Object->mTexture = Texture;
				Object->mState = Desc.mInitialState;
				Object->mLifetime = Desc.mLifetimeToken;
				Object->mCudaInterop = mCudaInterop;
				if (mCudaInterop)
				{
					Object->mCudaInfo = mCudaInterop->DescribeTexture(Texture);
				}
				return {Object, {}};
			}

			FArdaProviderObjectResult ImportBuffer(const FArdaRHINativeBufferImportDesc& Desc) override
			{
				if (!IsInRenderingThread() || Desc.mNativeTypeName != "unreal-rhi.buffer" || !Desc.mNativeObject ||
				    Desc.mOwnership != EArdaRHINativeOwnership::Borrowed)
				{
					return {{},
					    InvalidState("Buffer import requires a borrowed unreal-rhi.buffer on the render thread.")};
				}
				auto* Buffer = reinterpret_cast<FRHIBuffer*>(Desc.mNativeObject);
				if (ToUnrealAccess(Desc.mInitialState) == ERHIAccess::Unknown)
				{
					return {{},
					    Unsupported(
					        "Buffer import requires an explicit qualified Unreal access state; Common is not portable.")};
				}
				if (Desc.mBuffer.mbKeepInitialState || Desc.mBuffer.mbVirtual || Desc.mBuffer.mbTiled ||
				    Buffer->GetSize() != Desc.mBuffer.mByteSize ||
				    (Desc.mBuffer.mStructureStride != 0 && Buffer->GetStride() != Desc.mBuffer.mStructureStride))
				{
					return {{}, InvalidState("Buffer import size and stride must match the Unreal allocation.")};
				}
				auto Object = eastl::make_shared<FArdaUnrealObject>();
				Object->mBuffer = Buffer;
				Object->mState = Desc.mInitialState;
				Object->mLifetime = Desc.mLifetimeToken;
				return {Object, {}};
			}

			FArdaProviderObjectResult CreateShader(const FArdaRHIShaderDesc& Desc) override
			{
				if (Desc.mStage != EArdaRHIShaderStage::Compute || !Desc.mBytecode ||
				    Desc.mBytecodeSize != sizeof(FArdaUnrealShaderPayload))
				{
					return {{},
					    Unsupported(
					        "Use ImportUnrealComputeShader with a UE-compiled shader; raw DXIL/SPIR-V is not UE RHI shader code.")};
				}
				FArdaUnrealShaderPayload Payload;
				std::memcpy(&Payload, Desc.mBytecode, sizeof(Payload));
				if (Payload.mMagic != FArdaUnrealShaderPayload{}.mMagic || !Payload.mShader)
				{
					return {{}, InvalidState("Invalid Unreal compute shader import payload.")};
				}
				auto Object = eastl::make_shared<FArdaUnrealObject>();
				Object->mShader = Payload.mShader;
				return {Object, {}};
			}

			FArdaProviderObjectResult CreateBindingLayout(const FArdaRHIBindingLayoutDesc& Desc) override
			{
				if (Desc.mVisibility != EArdaRHIShaderStage::Compute || Desc.mRegisterSpace != 0 ||
				    Desc.mbRegisterSpaceIsDescriptorSet)
				{
					return {{},
					    Unsupported(
					        "Unreal layouts require compute visibility and reflected resource indices in space zero.")};
				}
				for (const auto& Item : Desc.mItems)
				{
					if ((Item.mType != EArdaRHIBindingType::TextureSRV &&
					        Item.mType != EArdaRHIBindingType::TextureUAV) ||
					    Item.mArraySize != 1 || Item.mSlot > MAX_uint16)
					{
						return {{},
						    Unsupported(
						        "This Unreal backend supports single texture SRV/UAV bindings; use Unreal parameter metadata for other bindings.")};
					}
				}
				auto Object = eastl::make_shared<FArdaUnrealObject>();
				Object->mLayout = Desc;
				return {Object, {}};
			}

			FArdaProviderObjectResult CreateBindingSet(const FArdaRHIBindingSetDesc&,
			    const FArdaProviderObjectRef&,
			    const eastl::vector<FArdaProviderBinding>& Bindings) override
			{
				if (!GArdaBorrowedList)
				{
					return {{}, InvalidState("Binding creation requires an Unreal RHI scope.")};
				}
				auto Object = eastl::make_shared<FArdaUnrealObject>();
				Object->mBindings = Bindings;
				Object->mSRVs.SetNum(static_cast<int32>(Bindings.size()));
				Object->mUAVs.SetNum(static_cast<int32>(Bindings.size()));
				for (size_t Index = 0; Index < Bindings.size(); ++Index)
				{
					const auto& Binding = Bindings[Index];
					auto Texture = GetObject(Binding.mObject).mTexture;
					if (!Texture || Binding.mItem.mArrayElement != 0)
					{
						return {{}, Unsupported("Only individual texture bindings are supported.")};
					}
					const auto& Range = Binding.mItem.mView.mTextureRange;
					if (Range.mBaseArraySlice != 0 || Range.mBasePlane != 0 ||
					    (Binding.mItem.mView.mFormat != EArdaRHIFormat::Unknown &&
					        ToUnrealFormat(Binding.mItem.mView.mFormat) != Texture->GetFormat()))
					{
						return {{}, Unsupported("Texture bindings do not support plane or format reinterpretation.")};
					}
					if (Binding.mItem.mType == EArdaRHIBindingType::TextureSRV)
					{
						const uint32 Mips = Range.mMipLevelCount == ArdaRHIAllSubresources
						    ? Texture->GetNumMips() - Range.mBaseMipLevel
						    : Range.mMipLevelCount;
						Object->mSRVs[static_cast<int32>(Index)] = GArdaBorrowedList->CreateShaderResourceView(Texture,
						    FRHIViewDesc::CreateTextureSRV().SetDimensionFromTexture(Texture).SetMipRange(
						        static_cast<uint8>(Range.mBaseMipLevel),
						        static_cast<uint8>(Mips)));
					}
					else if (Binding.mItem.mType == EArdaRHIBindingType::TextureUAV)
					{
						Object->mUAVs[static_cast<int32>(Index)] = GArdaBorrowedList->CreateUnorderedAccessView(Texture,
						    FRHIViewDesc::CreateTextureUAV().SetDimensionFromTexture(Texture).SetMipLevel(
						        static_cast<uint8>(Range.mBaseMipLevel)));
					}
					else
					{
						return {{}, Unsupported("Only texture SRV/UAV binding sets are qualified.")};
					}
				}
				return {Object, {}};
			}

			FArdaProviderObjectResult CreateComputePipeline(const FArdaProviderComputePipelineCreateInfo& Info) override
			{
				if (!Info.mComputeShader || !GetObject(Info.mComputeShader).mShader)
				{
					return {{}, InvalidState("Unreal compute pipelines require an imported UE compute shader.")};
				}
				auto Object = eastl::make_shared<FArdaUnrealObject>();
				Object->mShader = GetObject(Info.mComputeShader).mShader;
				return {Object, {}};
			}

			TArdaRHIResult<eastl::unique_ptr<IArdaProviderCommandList>> CreateCommandList(EArdaRHIQueueType Queue,
			    bool bImmediate) override
			{
				if (!GArdaBorrowedList || !IsInRenderingThread() || bArdaReplaying)
				{
					return {{}, InvalidState("CreateCommandList requires FArdaUnrealRHIScope on the render thread.")};
				}
				if (Queue != EArdaRHIQueueType::Graphics)
				{
					return {{}, Unsupported("Only Arda lists on Unreal's borrowed graphics queue are supported.")};
				}
				return {eastl::make_unique<FArdaUnrealCommands>(GArdaScopeGeneration, !bImmediate, mCudaInterop), {}};
			}

			TArdaRHIResult<uint64_t> ExecuteCommandList(IArdaProviderCommandList& CommandList,
			    EArdaRHIQueueType Queue) override
			{
				RunGarbageCollection();
				auto& Commands = static_cast<FArdaUnrealCommands&>(CommandList);
				if (Queue != EArdaRHIQueueType::Graphics || !Commands.CanExecute())
				{
					return {0,
					    InvalidState("Execute requires a closed unexecuted list in its original Unreal RHI scope.")};
				}
				// No native submit: Unreal owns queue scheduling. The fence retires core-owned resource leases.
				const auto Status = Commands.Replay();
				Commands.MarkExecuted();
				auto Fence = RHICreateGPUFence(TEXT("ArdashirSubmission"));
				GArdaBorrowedList->WriteGPUFence(Fence);
				FArdaUnrealSubmission Submission;
				Submission.mFence = Fence;
				Submission.mResources = Commands.GetRetainedResources();
				Submission.mCallbacks = Commands.GetRetainedCallbacks();
				Submission.mKernels = Commands.GetRetainedKernels();
				FScopeLock Lock(&mSubmissionMutex);
				const uint64 Token = ++mLastSubmission;
				mSubmissions.Add(Token, MoveTemp(Submission));
				return {Status ? Token : 0, Status};
			}

			TArdaRHIResult<bool> PollSubmission(uint64_t Submission) override
			{
				RunGarbageCollection();
				FScopeLock Lock(&mSubmissionMutex);
				if (Submission == 0 || Submission > mLastSubmission)
				{
					return {false, InvalidState("Submission token does not belong to this Unreal device.")};
				}
				if (Submission <= mCompletedSubmission)
				{
					return {true, {}};
				}
				const auto* Fence = mSubmissions.Find(Submission);
				return {Fence && Fence->mFence->Poll(), {}};
			}

			FArdaRHIStatus QueueWait(EArdaRHIQueueType WaitQueue,
			    EArdaRHIQueueType ExecutionQueue,
			    uint64_t Submission) override
			{
				if (WaitQueue != EArdaRHIQueueType::Graphics || ExecutionQueue != EArdaRHIQueueType::Graphics)
				{
					return Unsupported("Cross-queue synchronization belongs to Unreal/RDG.");
				}
				FScopeLock Lock(&mSubmissionMutex);
				return Submission > 0 && Submission <= mLastSubmission
				    ? FArdaRHIStatus{}
				    : InvalidState("Unknown Unreal submission token.");
			}

			FArdaRHIStatus WaitForSubmission(uint64_t Submission) override
			{
				const auto Result = PollSubmission(Submission);
				if (!Result || Result.mValue)
				{
					return Result.mStatus;
				}
				if (GArdaBorrowedList)
				{
					return InvalidState("An incomplete Unreal submission cannot be waited inside its recording scope.");
				}
				FGPUFenceRHIRef Fence;
				{
					FScopeLock Lock(&mSubmissionMutex);
					if (const auto* Pending = mSubmissions.Find(Submission))
					{
						Fence = Pending->mFence;
					}
				}
				if (!Fence)
				{
					return {};
				}
				if (IsInRenderingThread())
				{
					auto& List = FRHICommandListExecutor::GetImmediateCommandList();
					Fence->Wait(List, List.GetGPUMask());
				}
				else if (IsInGameThread())
				{
					ENQUEUE_RENDER_COMMAND(ArdaExplicitSubmissionWait)(
					    [Fence](FRHICommandListImmediate& List)
					    {
						    Fence->Wait(List, List.GetGPUMask());
					    });
					FlushRenderingCommands();
				}
				else
				{
					return InvalidState("Explicit Unreal submission waits require the game or render thread.");
				}
				RunGarbageCollection();
				return {};
			}

			FArdaRHIStatus WaitForIdle() override
			{
				if (GArdaBorrowedList)
				{
					return InvalidState(
					    "Waiting inside an Unreal RHI recording scope can deadlock; end the scope before an explicit idle wait.");
				}
				if (IsInRenderingThread())
				{
					FRHICommandListExecutor::GetImmediateCommandList().SubmitAndBlockUntilGPUIdle();
				}
				else if (IsInGameThread())
				{
					ENQUEUE_RENDER_COMMAND(ArdaExplicitIdleWait)(
					    [](FRHICommandListImmediate& List)
					    {
						    List.SubmitAndBlockUntilGPUIdle();
					    });
					FlushRenderingCommands();
				}
				else
				{
					return InvalidState("Explicit Unreal idle waits require the game or render thread.");
				}
				RunGarbageCollection();
				return {};
			}

			void RunGarbageCollection() override
			{
				FScopeLock Lock(&mSubmissionMutex);
				while (auto* Fence = mSubmissions.Find(mCompletedSubmission + 1))
				{
					if (!Fence->mFence->Poll())
					{
						break;
					}
					mSubmissions.Remove(++mCompletedSubmission);
				}
			}

			FArdaProviderLifetimeStats GetLifetimeStats() const noexcept override
			{
				FScopeLock Lock(&mSubmissionMutex);
				FArdaProviderLifetimeStats Stats;
				Stats.mPendingSubmissions = static_cast<size_t>(mSubmissions.Num());
				return Stats;
			}

		private:
			FArdaRHICapabilities mCapabilities;
			eastl::shared_ptr<IArdaUnrealCudaInterop> mCudaInterop;
			mutable FCriticalSection mSubmissionMutex;
			TMap<uint64, FArdaUnrealSubmission> mSubmissions;
			uint64 mLastSubmission = 0;
			uint64 mCompletedSubmission = 0;
		};

		class FArdaUnrealRuntime final : public IArdaBackendRuntime
		{
		public:
			FArdaSwapChainCreateResult CreateSwapChain(uint32_t, uint32_t, FArdaRHIDeviceRef) override
			{
				return {{}, "Unreal owns presentation; use a view extension or RDG pass."};
			}
		};

		class FArdaUnrealExternalDevice final : public IArdaExternalDeviceProvider
		{
		public:
			const char* GetBackendName() const noexcept override
			{
				return "unreal-rhi";
			}

			bool GetExternalDeviceDesc(FArdaExternalDeviceDesc& OutDesc) const override
			{
				if (!GDynamicRHI)
				{
					return false;
				}
				OutDesc = {};
				OutDesc.mBackendName = "unreal-rhi";
				OutDesc.mNativeApi = "unreal-rhi";
				OutDesc.mDevice = FArdaNativeObject(GDynamicRHI);
				return true;
			}
		};

		class FArdaUnrealBackend final : public IArdaBackendModule
		{
		public:
			FArdaUnrealBackend()
			{
				mDescriptor.mName = "unreal-rhi";
				mDescriptor.mDisplayName = "Unreal Engine RHI";
				mDescriptor.mShaderBinaryFormat = EArdaShaderBinaryFormat::BackendDefined;
				mDescriptor.mShaderArtifactExtension = ".ueshader";
				mDescriptor.mShaderCompilerIdentity = "unreal-global-shader-map";
				mDescriptor.mbSupportsExternalDevice = true;
			}

			const FArdaBackendModuleDescriptor& GetDescriptor() const noexcept override
			{
				return mDescriptor;
			}

			TArdaRHIResult<eastl::vector<FArdaAdapterInfo>> EnumerateAdapters() const override
			{
				return {{}, Unsupported("Unreal selects the physical adapter before Arda initializes.")};
			}

			FArdaBackendDeviceCreateResult CreateDevice(const FArdaBackendConfiguration& Configuration,
			    const FArdaAdapterId&,
			    IArdaWindowSurface*,
			    const IArdaExternalDeviceProvider* Provider) override
			{
				FArdaBackendDeviceCreateResult Result;
				FArdaExternalDeviceDesc External;
				if (!GDynamicRHI || Configuration.mDeviceSource != EArdaDeviceSource::ExternalProvider || !Provider ||
				    !Provider->GetExternalDeviceDesc(External) || External.mDevice.As<FDynamicRHI*>() != GDynamicRHI)
				{
					Result.mError =
					    "Initialize Unreal RHI first and configure Arda's unreal-rhi external device provider.";
					return Result;
				}
				Result.mProviderDevice = eastl::make_shared<FArdaUnrealDevice>();
				if (Configuration.mCudaExecutionMode == EArdaCudaExecutionMode::GraphicsQueue &&
				    Result.mProviderDevice->GetCudaCapabilities().mLaunchMode != EArdaCudaLaunchMode::D3D12CiG &&
				    Result.mProviderDevice->GetCudaCapabilities().mLaunchMode != EArdaCudaLaunchMode::VulkanCiG)
				{
					Result.mProviderDevice.reset();
					Result.mError =
					    "Unreal's portable RHI does not expose CUDA-in-Graphics capture; use a qualified ContextSwitch host bridge.";
					return Result;
				}
				Result.mBackendRuntime = eastl::make_unique<FArdaUnrealRuntime>();
				Result.mAdapter.mId.mBackendName = "unreal-rhi";
				Result.mAdapter.mId.mValue = TCHAR_TO_UTF8(*GRHIAdapterName);
				Result.mAdapter.mName = TCHAR_TO_UTF8(*GRHIAdapterName);
				Result.mAdapter.mVendorId = GRHIVendorId;
				Result.mAdapter.mDeviceId = GRHIDeviceId;
				Result.mResult = EArdaInitializeResult::Success;
				return Result;
			}

			FArdaRHIStatus ConfigureShaderCompileInvocation(FArdaBackendShaderCompileInvocation&) const override
			{
				return Unsupported(
				    "Unreal shaders must be registered as UE shader types, cooked by UE and imported with ImportUnrealComputeShader.");
			}

		private:
			FArdaBackendModuleDescriptor mDescriptor;
		};

		FArdaUnrealBackend GArdaBackend;
		FArdaUnrealExternalDevice GArdaExternalDevice;
	}

	bool RegisterUnrealRHIBackend()
	{
		if (!RegisterBackendModule(GArdaBackend))
		{
			return false;
		}
		if (!RegisterExternalDeviceProvider(GArdaExternalDevice))
		{
			(void)UnregisterBackendModule(GArdaBackend);
			return false;
		}
		return true;
	}

	bool SetUnrealCudaInterop(eastl::shared_ptr<IArdaUnrealCudaInterop> Interop)
	{
		if (GetActiveBackendModule())
		{
			return false;
		}
		GArdaCudaInterop = eastl::move(Interop);
		return true;
	}

	FArdaRHIStatus RecordUnrealCommands(eastl::function<FArdaRHIStatus(FRHICommandListImmediate&)> Callback)
	{
		if (!GArdaRecordingList)
		{
			return InvalidState("RecordUnrealCommands requires one open Arda list in the current Unreal scope.");
		}
		return GArdaRecordingList->RecordHost(eastl::move(Callback));
	}

	bool UnregisterUnrealRHIBackend()
	{
		return UnregisterExternalDeviceProvider(GArdaExternalDevice) && UnregisterBackendModule(GArdaBackend);
	}

	FArdaUnrealRHIScope::FArdaUnrealRHIScope(FRHICommandListImmediate& CommandList)
	{
		if (!IsInRenderingThread() || GArdaBorrowedList || CommandList.IsInsideRenderPass())
		{
			mStatus = InvalidState("An Arda Unreal scope must be unnested on the render thread outside a render pass.");
			return;
		}
		GArdaBorrowedList = &CommandList;
		++GArdaScopeGeneration;
		GArdaOpenLists = 0;
		mbActive = true;
	}

	FArdaUnrealRHIScope::~FArdaUnrealRHIScope()
	{
		if (mbActive)
		{
			ensureMsgf(GArdaOpenLists == 0,
			    TEXT("Close and execute Arda command lists before ending their Unreal RHI scope."));
			if (GArdaRecordingList)
			{
				(void)GArdaRecordingList->Close();
			}
			GArdaRecordingList = nullptr;
			GArdaOpenLists = 0;
			GArdaBorrowedList = nullptr;
		}
	}

	const FArdaRHIStatus& FArdaUnrealRHIScope::GetStatus() const noexcept
	{
		return mStatus;
	}

	TArdaRHIResult<FArdaRHIShaderRef> ImportUnrealComputeShader(FArdaRHIDeviceRef Device, FRHIComputeShader* Shader)
	{
		if (!Device || !Shader || !IsInRenderingThread())
		{
			return {{}, InvalidState("Shader import requires a device, shader and render thread.")};
		}
		FArdaUnrealShaderPayload Payload;
		Payload.mShader = Shader;
		FArdaRHIShaderDesc Desc;
		Desc.mStage = EArdaRHIShaderStage::Compute;
		Desc.mBytecode = &Payload;
		Desc.mBytecodeSize = sizeof(Payload);
		Desc.mDebugName = "UnrealComputeShader";
		return Device->CreateShader(Desc);
	}

	TArdaRHIResult<FArdaRHITextureRef> ImportUnrealTexture(FArdaRHIDeviceRef Device,
	    FRHITexture* Texture,
	    EArdaRHIResourceState InitialState)
	{
		if (!Device || !Texture)
		{
			return {{}, InvalidState("Texture import requires a device and texture.")};
		}
		const auto& Native = Texture->GetDesc();
		FArdaRHINativeTextureImportDesc Desc;
		Desc.mNativeObject = reinterpret_cast<uintptr_t>(Texture);
		Desc.mNativeTypeName = "unreal-rhi.texture";
		Desc.mInitialState = InitialState;
		Desc.mTexture.mWidth = Native.Extent.X;
		Desc.mTexture.mHeight = Native.Extent.Y;
		Desc.mTexture.mMipLevels = Native.NumMips;
		Desc.mTexture.mFormat = FromUnrealFormat(Native.Format);
		Desc.mTexture.mSampleCount = Native.NumSamples;
		Desc.mTexture.mInitialState = InitialState;
		Desc.mTexture.mUsage = EArdaRHITextureUsage::None;
		if (EnumHasAnyFlags(Native.Flags, ETextureCreateFlags::ShaderResource))
		{
			Desc.mTexture.mUsage |= EArdaRHITextureUsage::ShaderResource;
		}
		if (EnumHasAnyFlags(Native.Flags, ETextureCreateFlags::UAV))
		{
			Desc.mTexture.mUsage |= EArdaRHITextureUsage::UnorderedAccess;
		}
		if (EnumHasAnyFlags(Native.Flags, ETextureCreateFlags::RenderTargetable))
		{
			Desc.mTexture.mUsage |= EArdaRHITextureUsage::RenderTarget;
		}
		Desc.mTexture.mDebugName = "UnrealImportedTexture";
		return Device->ImportNativeTexture(Desc);
	}

	TArdaRHIResult<FArdaRHIBufferRef> ImportUnrealBuffer(FArdaRHIDeviceRef Device,
	    FRHIBuffer* Buffer,
	    const FArdaRHIBufferDesc& BufferDesc)
	{
		if (!Device || !Buffer)
		{
			return {{}, InvalidState("Buffer import requires a device and buffer.")};
		}
		FArdaRHINativeBufferImportDesc Desc;
		Desc.mNativeObject = reinterpret_cast<uintptr_t>(Buffer);
		Desc.mNativeTypeName = "unreal-rhi.buffer";
		Desc.mBuffer = BufferDesc;
		Desc.mInitialState = BufferDesc.mInitialState;
		return Device->ImportNativeBuffer(Desc);
	}
}
