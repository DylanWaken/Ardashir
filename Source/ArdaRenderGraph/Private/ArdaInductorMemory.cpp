#include "ArdaInductorPch.h"
#include "ArdaInductorMemory.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace arda
{
	namespace
	{
		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

		bool AddBytes(uint64_t& Total, uint64_t Bytes)
		{
			if (Bytes > UINT64_MAX - Total)
			{
				return false;
			}
			Total += Bytes;
			return true;
		}

		bool AlignBytes(uint64_t Bytes, uint64_t Alignment, uint64_t& Result)
		{
			if (!Alignment || (Alignment & (Alignment - 1)) || Bytes > UINT64_MAX - (Alignment - 1))
			{
				return false;
			}
			Result = (Bytes + Alignment - 1) & ~(Alignment - 1);
			return true;
		}

		bool ValidRequirements(const FArdaRHIMemoryRequirements& Requirements)
		{
			return Requirements.mSize && Requirements.mAlignment &&
			    !(Requirements.mAlignment & (Requirements.mAlignment - 1)) && Requirements.mMemoryTypeBits;
		}

		bool SameRequirements(const FArdaRHIMemoryRequirements& Left, const FArdaRHIMemoryRequirements& Right)
		{
			return Left.mSize == Right.mSize && Left.mAlignment == Right.mAlignment &&
			    Left.mMemoryTypeBits == Right.mMemoryTypeBits;
		}

		bool IsExternal(const FArdaInductorMemoryRequest& Request)
		{
			return Request.mExternalBuffer || Request.mExternalTexture || Request.mExternalAccelerationStructure;
		}

		const void* ExternalIdentity(const FArdaInductorMemoryRequest& Request)
		{
			if (Request.mExternalBuffer)
			{
				const void* Identity = Request.mExternalBuffer->GetPhysicalIdentity();
				return Identity ? Identity : Request.mExternalBuffer.Get();
			}
			if (Request.mExternalTexture)
			{
				const void* Identity = Request.mExternalTexture->GetPhysicalIdentity();
				return Identity ? Identity : Request.mExternalTexture.Get();
			}
			if (Request.mExternalAccelerationStructure)
			{
				const void* Identity = Request.mExternalAccelerationStructure->GetPhysicalIdentity();
				return Identity ? Identity : Request.mExternalAccelerationStructure.Get();
			}
			return nullptr;
		}

		FArdaRHIMemoryAllocationInfo ExternalAllocationInfo(const FArdaInductorMemoryRequest& Request)
		{
			return Request.mExternalBuffer ? Request.mExternalBuffer->GetMemoryAllocationInfo()
			    : Request.mExternalTexture ? Request.mExternalTexture->GetMemoryAllocationInfo()
			    : Request.mExternalAccelerationStructure
			    ? Request.mExternalAccelerationStructure->GetMemoryAllocationInfo()
			    : FArdaRHIMemoryAllocationInfo{};
		}

		FArdaRHIStatus CountExternalAllocations(const eastl::vector<FArdaInductorMemoryRequest>& Requests,
		    bool bRequireKnown,
		    uint64_t& Total)
		{
			struct FCounted
			{
				const void* mIdentity;
				uint64_t mBytes;
				EArdaRHIResourceType mKind;
				bool mbAllocation;
			};

			eastl::vector<FCounted> Counted;
			Total = 0;
			const auto Count = [&](const void* PhysicalIdentity,
			                       EArdaRHIResourceType Kind,
			                       const FArdaRHIMemoryAllocationInfo& Info,
			                       uint64_t Minimum) -> FArdaRHIStatus
			{
				if (bRequireKnown && !Info.mbKnown)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
					    "A hard memory budget requires retained-allocation metadata for imports and their retained AS inputs.");
				}
				if (Info.mbKnown && (!Info.mIdentity || Info.mByteSize < Minimum))
				{
					return Invalid("External retained allocation metadata is invalid or smaller than its resource.");
				}
				const void* Identity = Info.mbKnown ? Info.mIdentity : PhysicalIdentity;
				const uint64_t Bytes = Info.mbKnown ? Info.mByteSize : Minimum;
				auto Found = eastl::find_if(Counted.begin(),
				    Counted.end(),
				    [&](const FCounted& Entry)
				    {
					    return Entry.mIdentity == Identity && Entry.mbAllocation == Info.mbKnown &&
					        (Info.mbKnown || Entry.mKind == Kind);
				    });
				if (Found != Counted.end())
				{
					if (Found->mBytes != Bytes)
					{
						return Invalid("Imported resources disagree about their retained allocation capacity.");
					}
					return {};
				}
				Counted.push_back({Identity, Bytes, Kind, Info.mbKnown});
				return AddBytes(Total, Bytes) ? FArdaRHIStatus{}
				                              : Invalid("Retained external allocation total overflowed.");
			};
			const auto CountBuffer = [&](const FArdaRHIBufferRef& Buffer) -> FArdaRHIStatus
			{
				if (!Buffer)
				{
					return {};
				}
				const void* Identity = Buffer->GetPhysicalIdentity() ? Buffer->GetPhysicalIdentity() : Buffer.Get();
				// An explicit import may supply metadata for an otherwise opaque borrowed buffer.
				for (const auto& Request : Requests)
				{
					if (Request.mbUsed && Request.mExternalBuffer && ExternalIdentity(Request) == Identity)
					{
						return Count(Identity,
						    EArdaRHIResourceType::Buffer,
						    Request.mExternalAllocationInfo,
						    Request.mRequirements.mSize);
					}
				}
				return Count(Identity,
				    EArdaRHIResourceType::Buffer,
				    Buffer->GetMemoryAllocationInfo(),
				    Buffer->GetDesc().mByteSize);
			};
			for (const auto& Request : Requests)
			{
				if (!Request.mbUsed || !IsExternal(Request))
				{
					continue;
				}
				const auto Kind = Request.mKind == EArdaInductorMemoryKind::Buffer ? EArdaRHIResourceType::Buffer
				    : Request.mKind == EArdaInductorMemoryKind::Texture            ? EArdaRHIResourceType::Texture
				                                                                   : EArdaRHIResourceType::AccelStruct;
				if (auto Status = Count(ExternalIdentity(Request),
				        Kind,
				        Request.mExternalAllocationInfo,
				        Request.mRequirements.mSize);
				    !Status)
				{
					return Status;
				}
				if (!Request.mExternalAccelerationStructure)
				{
					continue;
				}
				// The facade owns its BLAS geometry descriptors even after a build or compaction.
				for (const auto& Geometry : Request.mExternalAccelerationStructure->GetDesc().mBottomLevelGeometries)
				{
					for (const auto& Buffer :
					    {Geometry.mVertexOrAABBBuffer, Geometry.mIndexBuffer, Geometry.mOpacityMicromapIndexBuffer})
					{
						if (auto Status = CountBuffer(Buffer); !Status)
						{
							return Status;
						}
					}
					if (const auto& Micromap = Geometry.mOpacityMicromap)
					{
						const auto& Desc = Micromap->GetDesc();
						const void* Identity =
						    Micromap->GetPhysicalIdentity() ? Micromap->GetPhysicalIdentity() : Micromap.Get();
						if (auto Status = Count(Identity,
						        EArdaRHIResourceType::OpacityMicromap,
						        Micromap->GetMemoryAllocationInfo(),
						        Desc.mResultSizeOverride);
						    !Status)
						{
							return Status;
						}
						if (auto Status = CountBuffer(Desc.mInputBuffer); !Status)
						{
							return Status;
						}
						if (auto Status = CountBuffer(Desc.mPerMicromapDescBuffer); !Status)
						{
							return Status;
						}
					}
				}
			}
			return {};
		}

		template <typename DescType>
		DescType StorageDesc(DescType Desc, bool bPlaced = false)
		{
			Desc.mDebugName.clear();
			Desc.mInitialState = EArdaRHIResourceState::Common;
			Desc.mbKeepInitialState = false;
			Desc.mbVirtual = bPlaced;
			return Desc;
		}

		bool PlacementDescriptorAllowed(const FArdaInductorMemoryRequest& Request)
		{
			if (!Request.mbAllowHeapPlacement || Request.mbPersistent || IsExternal(Request))
			{
				return false;
			}
			if (Request.mKind == EArdaInductorMemoryKind::Buffer)
			{
				const auto& Desc = Request.mBufferDesc;
				return !Desc.mbCudaInterop && !Desc.mbTiled && Desc.mMaxVersions <= 1 &&
				    Desc.mCpuAccess == EArdaRHICpuAccess::None;
			}
			const auto& Desc = Request.mTextureDesc;
			return !Desc.mbCudaInterop && !Desc.mbTiled && Desc.mSampleCount == 1 &&
			    !HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::RenderTarget | EArdaRHITextureUsage::DepthStencil);
		}

		bool Compatible(const FArdaInductorMemoryRequest& Left, const FArdaInductorMemoryRequest& Right)
		{
			if (Left.mKind != Right.mKind || !SameRequirements(Left.mRequirements, Right.mRequirements))
			{
				return false;
			}
			if (Left.mKind == EArdaInductorMemoryKind::AccelerationStructure)
			{
				return ExternalIdentity(Left) == ExternalIdentity(Right);
			}
			return Left.mKind == EArdaInductorMemoryKind::Buffer
			    ? StorageDesc(Left.mBufferDesc) == StorageDesc(Right.mBufferDesc)
			    : StorageDesc(Left.mTextureDesc) == StorageDesc(Right.mTextureDesc);
		}

		FArdaRHIStatus ValidateRequests(const eastl::vector<FArdaInductorMemoryRequest>& Requests, bool bRequireSizes)
		{
			eastl::vector<bool> Seen(Requests.size(), false);
			for (const auto& Request : Requests)
			{
				if (Request.mIdentifier >= Requests.size() || Seen[Request.mIdentifier])
				{
					return Invalid("Memory request identifiers must be unique and dense from zero.");
				}
				Seen[Request.mIdentifier] = true;
				if (!Request.mbUsed)
				{
					continue;
				}
				if (Request.mFirstUse > Request.mLastUse ||
				    (bRequireSizes && !ValidRequirements(Request.mRequirements)))
				{
					return Invalid("Memory request lifetime or native allocation requirements are invalid.");
				}
				if ((Request.mKind != EArdaInductorMemoryKind::Buffer &&
				        Request.mKind != EArdaInductorMemoryKind::Texture &&
				        Request.mKind != EArdaInductorMemoryKind::AccelerationStructure) ||
				    (Request.mKind == EArdaInductorMemoryKind::Buffer &&
				        (Request.mExternalTexture || Request.mExternalAccelerationStructure)) ||
				    (Request.mKind == EArdaInductorMemoryKind::Texture &&
				        (Request.mExternalBuffer || Request.mExternalAccelerationStructure)) ||
				    (Request.mKind == EArdaInductorMemoryKind::AccelerationStructure &&
				        (!Request.mExternalAccelerationStructure || Request.mExternalBuffer ||
				            Request.mExternalTexture)))
				{
					return Invalid("Memory request kind and external resource disagree.");
				}
				if (Request.mbUseHeapPlacement && !PlacementDescriptorAllowed(Request))
				{
					return Invalid("This memory descriptor cannot use native heap placement.");
				}
				if (Request.mbUseHeapPlacement &&
				    (!Request.mHeapAllocationAlignment ||
				        (Request.mHeapAllocationAlignment & (Request.mHeapAllocationAlignment - 1))))
				{
					return Invalid("Native heap allocation alignment must be a nonzero power of two.");
				}
				if ((Request.mKind == EArdaInductorMemoryKind::Buffer &&
				        (Request.mBufferDesc.mbTiled || Request.mBufferDesc.mMaxVersions > 1)) ||
				    (Request.mKind == EArdaInductorMemoryKind::Texture && Request.mTextureDesc.mbTiled))
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
					    "Sparse or multiply versioned storage cannot be accounted by the persistent memory planner.");
				}
			}
			return {};
		}

		bool CanOrderReuse(const FArdaInductorMemoryRequest& Previous,
		    const FArdaInductorMemoryRequest& Next,
		    const FArdaInductorMemoryOptions& Options)
		{
			if (!Options.mbAllowReuse || Previous.mbPersistent || Next.mbPersistent ||
			    Previous.mLastUse >= Next.mFirstUse || Previous.mLastUseNodes.empty() || Next.mFirstUseNodes.empty())
			{
				return false;
			}
			for (uint32_t Producer : Previous.mLastUseNodes)
			{
				for (uint32_t Consumer : Next.mFirstUseNodes)
				{
					if (Producer == Consumer ||
					    (!Options.mbAllowSerialization &&
					        (!Options.mHappensBefore ||
					            !Options.mHappensBefore(Options.mHappensBeforeContext, Producer, Consumer))))
					{
						return false;
					}
				}
			}
			return true;
		}

		void AddAliasEdges(FArdaInductorMemoryPlan& Plan,
		    const FArdaInductorMemoryRequest& Previous,
		    const FArdaInductorMemoryRequest& Next)
		{
			for (uint32_t Producer : Previous.mLastUseNodes)
			{
				for (uint32_t Consumer : Next.mFirstUseNodes)
				{
					if (eastl::none_of(Plan.mAliasEdges.begin(),
					        Plan.mAliasEdges.end(),
					        [&](const auto& Edge)
					        {
						        return Edge.mProducer == Producer && Edge.mConsumer == Consumer;
					        }))
					{
						Plan.mAliasEdges.push_back({Producer, Consumer});
					}
				}
			}
		}

		bool Overlaps(uint64_t Left, uint64_t LeftSize, uint64_t Right, uint64_t RightSize)
		{
			return Left < Right + RightSize && Right < Left + LeftSize;
		}
	}

	FArdaRHIStatus DescribeArdaInductorMemory(IArdaRHIDevice& Device,
	    eastl::vector<FArdaInductorMemoryRequest>& Requests)
	{
		if (auto Status = ValidateRequests(Requests, false); !Status)
		{
			return Status;
		}
		// Publish queried sizes together, so a failed query cannot leave mixed old/new metadata.
		auto Described = Requests;
		for (auto& Request : Described)
		{
			if (!Request.mbUsed)
			{
				continue;
			}
			const auto& Caps = Device.GetCapabilities();
			Request.mbUseHeapPlacement = Caps.mbHeaps && Caps.mbVirtualResources && Caps.mbAliasingBarriers &&
			    PlacementDescriptorAllowed(Request);
			Request.mHeapAllocationAlignment = Caps.mHeapAllocationAlignment;
			TArdaRHIResult<FArdaRHIMemoryRequirements> Requirements;
			if (Request.mExternalBuffer)
			{
				Request.mBufferDesc = Request.mExternalBuffer->GetDesc();
				Requirements = Device.GetBufferMemoryRequirements(Request.mExternalBuffer);
			}
			else if (Request.mExternalTexture)
			{
				Request.mTextureDesc = Request.mExternalTexture->GetDesc();
				Requirements = Device.GetTextureMemoryRequirements(Request.mExternalTexture);
			}
			else if (Request.mExternalAccelerationStructure)
			{
				Requirements = Device.GetAccelStructMemoryRequirements(Request.mExternalAccelerationStructure);
			}
			else if (Request.mKind == EArdaInductorMemoryKind::Buffer)
			{
				Requirements =
				    Device.QueryBufferMemoryRequirements(StorageDesc(Request.mBufferDesc, Request.mbUseHeapPlacement));
			}
			else
			{
				Requirements = Device.QueryTextureMemoryRequirements(
				    StorageDesc(Request.mTextureDesc, Request.mbUseHeapPlacement));
			}
			if (!Requirements)
			{
				return Requirements.mStatus;
			}
			Request.mRequirements = Requirements.mValue;
			if (const auto Allocation = ExternalAllocationInfo(Request); Allocation.mbKnown)
			{
				Request.mExternalAllocationInfo = Allocation;
			}
		}
		if (auto Status = ValidateRequests(Described, true); !Status)
		{
			return Status;
		}
		Requests = eastl::move(Described);
		return {};
	}

	TArdaRHIResult<FArdaInductorMemoryPlan> PlanArdaInductorMemory(
	    const eastl::vector<FArdaInductorMemoryRequest>& Requests,
	    const FArdaInductorMemoryOptions& Options)
	{
		if (auto Status = ValidateRequests(Requests, true); !Status)
		{
			return {{}, Status};
		}
		if (!Options.mFrameCount)
		{
			return {{}, Invalid("Memory frame count must be positive.")};
		}
		FArdaInductorMemoryPlan Plan;
		Plan.mRequests = Requests;
		eastl::sort(Plan.mRequests.begin(),
		    Plan.mRequests.end(),
		    [](const auto& Left, const auto& Right)
		    {
			    return Left.mIdentifier < Right.mIdentifier;
		    });
		Plan.mResourceSlots.resize(Requests.size(), UINT32_MAX);
		Plan.mResourceHeapIndices.resize(Requests.size(), UINT32_MAX);
		Plan.mResourceOffsets.resize(Requests.size());
		Plan.mResourceBytes.resize(Requests.size());
		Plan.mWorkspaceBytes = Options.mWorkspaceBytes;
		Plan.mFrameCount = Options.mFrameCount;
		eastl::vector<bool> AliasedTextures(Requests.size(), false);
		for (auto& Request : Plan.mRequests)
		{
			if (Request.mbUsed && IsExternal(Request) && !Request.mExternalAllocationInfo.mbKnown)
			{
				Request.mExternalAllocationInfo = ExternalAllocationInfo(Request);
			}
		}
		if (auto Status =
		        CountExternalAllocations(Plan.mRequests, Options.mBudgetBytes != UINT64_MAX, Plan.mExternalBytes);
		    !Status)
		{
			return {{}, Status};
		}
		eastl::vector<uint32_t> Order;
		for (const auto& Request : Plan.mRequests)
		{
			if (Request.mbUsed)
			{
				Order.push_back(Request.mIdentifier);
			}
		}
		eastl::sort(Order.begin(),
		    Order.end(),
		    [&](uint32_t Left, uint32_t Right)
		    {
			    const auto& A = Plan.mRequests[Left];
			    const auto& B = Plan.mRequests[Right];
			    return A.mFirstUse != B.mFirstUse ? A.mFirstUse < B.mFirstUse : Left < Right;
		    });
		for (uint32_t Identifier : Order)
		{
			const auto& Request = Plan.mRequests[Identifier];
			Plan.mResourceBytes[Identifier] = Request.mRequirements.mSize;
			if (Request.mbUseHeapPlacement)
			{
				uint32_t BestHeap = UINT32_MAX;
				uint64_t BestOffset = 0, BestCapacity = 0, BestGrowth = UINT64_MAX;
				// First-fit candidates are zero and the aligned ends of existing placements.
				// Every overlapping predecessor must pass the entire cross-queue frontier test.
				for (uint32_t Index = 0; Index < Plan.mHeaps.size(); ++Index)
				{
					const auto& Heap = Plan.mHeaps[Index];
					if (Heap.mKind != Request.mKind ||
					    !(Heap.mDesc.mMemoryTypeBits & Request.mRequirements.mMemoryTypeBits))
					{
						continue;
					}
					eastl::vector<uint64_t> Candidates{0};
					for (uint32_t Prior : Heap.mResources)
					{
						uint64_t Offset = 0;
						if (AlignBytes(Plan.mResourceOffsets[Prior] + Plan.mResourceBytes[Prior],
						        Request.mRequirements.mAlignment,
						        Offset))
						{
							Candidates.push_back(Offset);
						}
					}
					eastl::sort(Candidates.begin(), Candidates.end());
					Candidates.erase(eastl::unique(Candidates.begin(), Candidates.end()), Candidates.end());
					for (uint64_t Offset : Candidates)
					{
						if (Request.mRequirements.mSize > UINT64_MAX - Offset)
						{
							continue;
						}
						bool Allowed = true;
						for (uint32_t Prior : Heap.mResources)
						{
							if (Overlaps(Offset,
							        Request.mRequirements.mSize,
							        Plan.mResourceOffsets[Prior],
							        Plan.mResourceBytes[Prior]) &&
							    ((Request.mKind == EArdaInductorMemoryKind::Texture &&
							         (!Options.mbAllowTextureAliasing || Request.mFirstUseNodes.size() != 1 ||
							             Plan.mRequests[Prior].mFirstUseNodes.size() != 1)) ||
							        !CanOrderReuse(Plan.mRequests[Prior], Request, Options)))
							{
								Allowed = false;
								break;
							}
						}
						uint64_t Capacity = 0;
						if (!Allowed ||
						    !AlignBytes(eastl::max(Heap.mDesc.mCapacity, Offset + Request.mRequirements.mSize),
						        eastl::max(Heap.mAlignment,
						            eastl::max(Request.mRequirements.mAlignment, Request.mHeapAllocationAlignment)),
						        Capacity))
						{
							continue;
						}
						const uint64_t Growth = Capacity - Heap.mDesc.mCapacity;
						if (BestHeap == UINT32_MAX || Growth < BestGrowth ||
						    (Growth == BestGrowth && Offset < BestOffset))
						{
							BestHeap = Index;
							BestOffset = Offset;
							BestCapacity = Capacity;
							BestGrowth = Growth;
						}
					}
				}
				if (BestHeap == UINT32_MAX)
				{
					FArdaInductorMemoryHeap Heap;
					Heap.mKind = Request.mKind;
					Heap.mAlignment = eastl::max(Request.mHeapAllocationAlignment, Request.mRequirements.mAlignment);
					if (!AlignBytes(Request.mRequirements.mSize, Heap.mAlignment, BestCapacity))
					{
						return {{}, Invalid("Native heap capacity overflowed.")};
					}
					Heap.mDesc.mMemoryTypeBits = Request.mRequirements.mMemoryTypeBits;
					Heap.mDesc.mDebugName = "Inductor frame heap";
					BestHeap = static_cast<uint32_t>(Plan.mHeaps.size());
					Plan.mHeaps.push_back(eastl::move(Heap));
				}
				auto& Heap = Plan.mHeaps[BestHeap];
				eastl::vector<uint32_t> Previous;
				for (uint32_t Prior : Heap.mResources)
				{
					if (Overlaps(BestOffset,
					        Request.mRequirements.mSize,
					        Plan.mResourceOffsets[Prior],
					        Plan.mResourceBytes[Prior]))
					{
						Previous.push_back(Prior);
						AddAliasEdges(Plan, Plan.mRequests[Prior], Request);
						if (Request.mKind == EArdaInductorMemoryKind::Texture)
						{
							AliasedTextures[Prior] = AliasedTextures[Identifier] = true;
						}
					}
				}
				// Each buffer first-user activates after every prior user. These barriers do
				// not discard bytes, so unordered readers may independently issue them safely.
				// First occupants need activation on later frames too, after the frame fence.
				if (Request.mKind == EArdaInductorMemoryKind::Buffer || Request.mFirstUseNodes.size() == 1)
				{
					for (uint32_t Consumer : Request.mFirstUseNodes)
					{
						if (eastl::none_of(Plan.mAliasActivations.begin(),
						        Plan.mAliasActivations.end(),
						        [&](const auto& A)
						        {
							        return A.mResource == Identifier && A.mConsumer == Consumer;
						        }))
						{
							Plan.mAliasActivations.push_back({Identifier, Consumer, Previous});
						}
					}
				}
				Heap.mDesc.mCapacity = BestCapacity;
				Heap.mDesc.mMemoryTypeBits &= Request.mRequirements.mMemoryTypeBits;
				Heap.mAlignment = eastl::max(Heap.mAlignment,
				    eastl::max(Request.mRequirements.mAlignment, Request.mHeapAllocationAlignment));
				Heap.mResources.push_back(Identifier);
				Plan.mResourceHeapIndices[Identifier] = BestHeap;
				Plan.mResourceOffsets[Identifier] = BestOffset;
				Plan.mResourceSlots[Identifier] = static_cast<uint32_t>(Plan.mSlots.size());
				Plan.mSlots.push_back(
				    {Identifier, {Identifier}, Request.mRequirements.mSize, false, false, BestHeap, BestOffset});
				continue;
			}
			const bool bExternal = IsExternal(Request);
			uint32_t SlotIndex = UINT32_MAX;
			for (uint32_t Index = 0; Index < Plan.mSlots.size(); ++Index)
			{
				const auto& Slot = Plan.mSlots[Index];
				if (Slot.mHeapIndex != UINT32_MAX)
				{
					continue;
				}
				const auto& Previous = Plan.mRequests[Slot.mResources.back()];
				if (bExternal && Slot.mbExternal && Request.mKind == Previous.mKind &&
				    ExternalIdentity(Request) == ExternalIdentity(Previous))
				{
					if (!Compatible(Request, Previous))
					{
						return {{}, Invalid("External aliases disagree about their physical storage description.")};
					}
					SlotIndex = Index;
					break;
				}
				if (!bExternal && !Slot.mbExternal && Compatible(Previous, Request) &&
				    CanOrderReuse(Previous, Request, Options))
				{
					SlotIndex = Index;
					AddAliasEdges(Plan, Previous, Request);
					break;
				}
			}
			if (SlotIndex == UINT32_MAX)
			{
				SlotIndex = static_cast<uint32_t>(Plan.mSlots.size());
				Plan.mSlots.push_back({Identifier, {}, Request.mRequirements.mSize, bExternal, Request.mbPersistent});
				uint64_t& Bytes = Request.mbPersistent ? Plan.mPersistentBytes : Plan.mTransientBytes;
				if (!bExternal && !AddBytes(Bytes, Request.mRequirements.mSize))
				{
					return {{}, Invalid("Persistent memory byte count overflowed.")};
				}
			}
			Plan.mSlots[SlotIndex].mResources.push_back(Identifier);
			Plan.mResourceSlots[Identifier] = SlotIndex;
		}
		Plan.mAliasActivations.erase(eastl::remove_if(Plan.mAliasActivations.begin(),
		                                 Plan.mAliasActivations.end(),
		                                 [&](const auto& Activation)
		                                 {
			                                 return Plan.mRequests[Activation.mResource].mKind ==
			                                     EArdaInductorMemoryKind::Texture &&
			                                     !AliasedTextures[Activation.mResource];
		                                 }),
		    Plan.mAliasActivations.end());
		for (const auto& Heap : Plan.mHeaps)
		{
			if (!AddBytes(Plan.mTransientBytes, Heap.mDesc.mCapacity))
			{
				return {{}, Invalid("Native heap total overflowed.")};
			}
		}
		if (Plan.mTransientBytes > UINT64_MAX / Options.mFrameCount)
		{
			return {{}, Invalid("Frame pool byte count overflowed.")};
		}
		Plan.mOwnedBytes = Plan.mTransientBytes * Options.mFrameCount;
		if (!AddBytes(Plan.mOwnedBytes, Plan.mPersistentBytes))
		{
			return {{}, Invalid("Owned memory byte count overflowed.")};
		}
		Plan.mTotalBytes = Plan.mOwnedBytes;
		if (!AddBytes(Plan.mTotalBytes, Plan.mExternalBytes) || !AddBytes(Plan.mTotalBytes, Plan.mWorkspaceBytes))
		{
			return {{}, Invalid("Persistent memory total overflowed.")};
		}
		if (Plan.mTotalBytes > Options.mBudgetBytes)
		{
			return {eastl::move(Plan),
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			        "This memory plan exceeds the budget; another schedule or allocation plan may fit.")};
		}
		Plan.mbBudgetAccepted = true;
		return {eastl::move(Plan), {}};
	}

	TArdaRHIResult<FArdaInductorMemoryResources> MaterializeArdaInductorMemory(IArdaRHIDevice& Device,
	    const FArdaInductorMemoryPlan& Plan,
	    const FArdaInductorMemoryResources* Shared)
	{
		if (!Plan.mbBudgetAccepted || !Plan.mFrameCount)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			        "A memory plan must pass its budget before materialization.")};
		}
		if (auto Status = ValidateRequests(Plan.mRequests, true); !Status)
		{
			return {{}, Status};
		}
		const size_t Count = Plan.mRequests.size();
		if (Plan.mResourceSlots.size() != Count || Plan.mResourceHeapIndices.size() != Count ||
		    Plan.mResourceOffsets.size() != Count || Plan.mResourceBytes.size() != Count ||
		    (Shared &&
		        (Shared->mBuffers.size() != Count || Shared->mTextures.size() != Count ||
		            Shared->mAccelerationStructures.size() != Count)))
		{
			return {{}, Invalid("Memory plan identifier tables or shared frame pool do not match.")};
		}
		// Validate all membership, offsets and requirements before committing any heap.
		eastl::vector<bool> Seen(Count, false);
		uint64_t Transient = 0, Persistent = 0, External = 0;
		if (auto Status = CountExternalAllocations(Plan.mRequests, false, External); !Status)
		{
			return {{}, Status};
		}
		for (const auto& Heap : Plan.mHeaps)
		{
			if (!Heap.mDesc.mCapacity || !Heap.mDesc.mMemoryTypeBits || Heap.mResources.empty() ||
			    Heap.mDesc.mType != EArdaRHIHeapType::DeviceLocal || !AddBytes(Transient, Heap.mDesc.mCapacity))
			{
				return {{}, Invalid("Invalid native heap plan.")};
			}
		}
		for (uint32_t SlotIndex = 0; SlotIndex < Plan.mSlots.size(); ++SlotIndex)
		{
			const auto& Slot = Plan.mSlots[SlotIndex];
			if (Slot.mRepresentative >= Count || Slot.mResources.empty())
			{
				return {{}, Invalid("Invalid persistent memory slot.")};
			}
			const auto& Request = Plan.mRequests[Slot.mRepresentative];
			if (Slot.mbExternal != IsExternal(Request) || Slot.mbPersistent != Request.mbPersistent ||
			    Slot.mBytes != Request.mRequirements.mSize ||
			    (Slot.mHeapIndex != UINT32_MAX) != Request.mbUseHeapPlacement)
			{
				return {{}, Invalid("Memory slot accounting is inconsistent.")};
			}
			for (uint32_t Identifier : Slot.mResources)
			{
				if (Identifier >= Count || Seen[Identifier] || !Plan.mRequests[Identifier].mbUsed ||
				    Plan.mRequests[Identifier].mIdentifier != Identifier ||
				    !Compatible(Request, Plan.mRequests[Identifier]) || Plan.mResourceSlots[Identifier] != SlotIndex ||
				    Plan.mResourceHeapIndices[Identifier] != Slot.mHeapIndex ||
				    Plan.mResourceOffsets[Identifier] != Slot.mOffset || Plan.mResourceBytes[Identifier] != Slot.mBytes)
				{
					return {{}, Invalid("Memory slot membership is invalid.")};
				}
				Seen[Identifier] = true;
			}
			if (Slot.mHeapIndex != UINT32_MAX)
			{
				if (Slot.mHeapIndex >= Plan.mHeaps.size() || Slot.mResources.size() != 1)
				{
					return {{}, Invalid("Invalid native heap slot.")};
				}
				const auto& Heap = Plan.mHeaps[Slot.mHeapIndex];
				if (Heap.mKind != Request.mKind || Slot.mOffset % Request.mRequirements.mAlignment ||
				    Slot.mOffset > Heap.mDesc.mCapacity || Slot.mBytes > Heap.mDesc.mCapacity - Slot.mOffset ||
				    (Heap.mDesc.mMemoryTypeBits & Request.mRequirements.mMemoryTypeBits) !=
				        Heap.mDesc.mMemoryTypeBits ||
				    eastl::count(Heap.mResources.begin(), Heap.mResources.end(), Request.mIdentifier) != 1)
				{
					return {{}, Invalid("Native placement violates capacity, alignment or memory type compatibility.")};
				}
			}
			else if (!Slot.mbExternal && !AddBytes(Slot.mbPersistent ? Persistent : Transient, Slot.mBytes))
			{
				return {{}, Invalid("Memory slot accounting overflowed.")};
			}
			TArdaRHIResult<FArdaRHIMemoryRequirements> Current;
			const auto CurrentAllocation = ExternalAllocationInfo(Request);
			if (CurrentAllocation.mbKnown && !(CurrentAllocation == Request.mExternalAllocationInfo))
			{
				return {{}, Invalid("Retained native allocation changed before materialization; prepare a new plan.")};
			}
			if (Request.mExternalBuffer)
			{
				Current = Device.GetBufferMemoryRequirements(Request.mExternalBuffer);
			}
			else if (Request.mExternalTexture)
			{
				Current = Device.GetTextureMemoryRequirements(Request.mExternalTexture);
			}
			else if (Request.mExternalAccelerationStructure)
			{
				Current = Device.GetAccelStructMemoryRequirements(Request.mExternalAccelerationStructure);
			}
			else if (Shared && Request.mbPersistent)
			{
				const auto& Buffer = Shared->mBuffers[Request.mIdentifier];
				const auto& Texture = Shared->mTextures[Request.mIdentifier];
				if ((Request.mKind == EArdaInductorMemoryKind::Buffer &&
				        (!Buffer || Texture ||
				            !(StorageDesc(Buffer->GetDesc()) == StorageDesc(Request.mBufferDesc)))) ||
				    (Request.mKind == EArdaInductorMemoryKind::Texture &&
				        (!Texture || Buffer ||
				            !(StorageDesc(Texture->GetDesc()) == StorageDesc(Request.mTextureDesc)))))
				{
					return {{}, Invalid("Shared persistent resource does not match this memory plan.")};
				}
				Current =
				    Buffer ? Device.GetBufferMemoryRequirements(Buffer) : Device.GetTextureMemoryRequirements(Texture);
			}
			else if (Request.mKind == EArdaInductorMemoryKind::Buffer)
			{
				Current =
				    Device.QueryBufferMemoryRequirements(StorageDesc(Request.mBufferDesc, Request.mbUseHeapPlacement));
			}
			else
			{
				Current = Device.QueryTextureMemoryRequirements(
				    StorageDesc(Request.mTextureDesc, Request.mbUseHeapPlacement));
			}
			if (!Current)
			{
				return {{}, Current.mStatus};
			}
			if (!SameRequirements(Current.mValue, Request.mRequirements))
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				        "Native allocation requirements changed before materialization; prepare a new plan.")};
			}
		}
		for (const auto& Request : Plan.mRequests)
		{
			if (Seen[Request.mIdentifier] != Request.mbUsed)
			{
				return {{}, Invalid("Memory plan omitted a used allocation.")};
			}
		}
		uint64_t Owned = 0, Total = 0;
		if (Transient > UINT64_MAX / Plan.mFrameCount)
		{
			return {{}, Invalid("Frame pool accounting overflowed.")};
		}
		Owned = Transient * Plan.mFrameCount;
		if (!AddBytes(Owned, Persistent))
		{
			return {{}, Invalid("Owned pool accounting overflowed.")};
		}
		Total = Owned;
		if (!AddBytes(Total, External) || !AddBytes(Total, Plan.mWorkspaceBytes) || Owned != Plan.mOwnedBytes ||
		    Total != Plan.mTotalBytes || Transient != Plan.mTransientBytes || Persistent != Plan.mPersistentBytes ||
		    External != Plan.mExternalBytes)
		{
			return {{}, Invalid("Memory plan byte totals are inconsistent.")};
		}
		FArdaInductorMemoryResources Resources;
		Resources.mBuffers.resize(Count);
		Resources.mTextures.resize(Count);
		Resources.mAccelerationStructures.resize(Count);
		for (const auto& Heap : Plan.mHeaps)
		{
			auto Created = Device.CreateHeap(Heap.mDesc);
			if (!Created)
			{
				return {{}, Created.mStatus};
			}
			if (Created.mValue->GetDesc().mCapacity != Heap.mDesc.mCapacity)
			{
				return {{}, Invalid("Native heap capacity differs from its accepted plan.")};
			}
			Resources.mHeaps.push_back(eastl::move(Created.mValue));
		}
		for (const auto& Slot : Plan.mSlots)
		{
			const auto& Request = Plan.mRequests[Slot.mRepresentative];
			FArdaRHIBufferRef Buffer = Request.mExternalBuffer;
			FArdaRHITextureRef Texture = Request.mExternalTexture;
			FArdaRHIAccelStructRef AccelerationStructure = Request.mExternalAccelerationStructure;
			if (Shared && Request.mbPersistent && !Slot.mbExternal)
			{
				Buffer = Shared->mBuffers[Request.mIdentifier];
				Texture = Shared->mTextures[Request.mIdentifier];
			}
			if (!Buffer && !Texture && !AccelerationStructure)
			{
				if (Request.mKind == EArdaInductorMemoryKind::Buffer)
				{
					auto Created = Device.CreateBuffer(StorageDesc(Request.mBufferDesc, Request.mbUseHeapPlacement));
					if (!Created)
					{
						return {{}, Created.mStatus};
					}
					Buffer = eastl::move(Created.mValue);
				}
				else
				{
					auto Created = Device.CreateTexture(StorageDesc(Request.mTextureDesc, Request.mbUseHeapPlacement));
					if (!Created)
					{
						return {{}, Created.mStatus};
					}
					Texture = eastl::move(Created.mValue);
				}
			}
			const auto Actual = AccelerationStructure ? Device.GetAccelStructMemoryRequirements(AccelerationStructure)
			    : Buffer                              ? Device.GetBufferMemoryRequirements(Buffer)
			                                          : Device.GetTextureMemoryRequirements(Texture);
			if (!Actual)
			{
				return {{}, Actual.mStatus};
			}
			if (!SameRequirements(Actual.mValue, Request.mRequirements))
			{
				return {{},
				    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				        "Native allocation requirements changed after memory planning; prepare a new plan.")};
			}
			if (Slot.mHeapIndex != UINT32_MAX)
			{
				auto Status = Buffer
				    ? Device.BindBufferMemory(Buffer, Resources.mHeaps[Slot.mHeapIndex], Slot.mOffset)
				    : Device.BindTextureMemory(Texture, Resources.mHeaps[Slot.mHeapIndex], Slot.mOffset);
				if (!Status)
				{
					return {{}, Status};
				}
			}
			for (uint32_t Identifier : Slot.mResources)
			{
				Resources.mBuffers[Identifier] = Buffer;
				Resources.mTextures[Identifier] = Texture;
				Resources.mAccelerationStructures[Identifier] = AccelerationStructure;
			}
		}
		Resources.mAllocatedBytes = Transient + (Shared ? 0 : Persistent);
		Resources.mSharedBytes = Shared ? Persistent : 0;
		Resources.mExternalBytes = External;
		Resources.mTotalBytes = Transient + Persistent + External + Plan.mWorkspaceBytes;
		return {eastl::move(Resources), {}};
	}
}
