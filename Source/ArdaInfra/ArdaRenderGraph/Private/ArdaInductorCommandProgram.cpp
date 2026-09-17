#include "ArdaInductorPch.h"
#include "ArdaInductorCommandProgramInternal.h"
#include "ArdaInductorState.h"
#include <EASTL/sort.h>

namespace arda
{
	namespace
	{
		template <class Record, class Handle, class ResourceRef>
		Record* BindResource(bool bCompiled,
		    TArdaInductorRecordStore<Record, Handle>& Records,
		    eastl::unordered_map<const void*, Record*>& Imports,
		    ResourceRef Resource,
		    EArdaRHIResourceState State,
		    eastl::string Name)
		{
			if (bCompiled || !Resource)
			{
				ARDA_CHECK_MSG("Inductor binds require a live object before finalization.");
			}
			const void* Identity = Resource->GetPhysicalIdentity();
			if (!Identity)
			{
				Identity = Resource.Get();
			}
			if (auto Found = Imports.find(Identity); Found != Imports.end())
			{
				return Found->second;
			}
			const auto Index = Records.Append(eastl::move(Resource), State, eastl::move(Name));
			auto* Result = &Records.Get(Index);
			Imports.emplace(Identity, Result);
			return Result;
		}

		template <class Record, class Handle, class Access>
		auto ResolveResourceForPass(FArdaInductorCommandProgram::FArdaImpl& Graph,
		    FArdaInductorCommandHandle Pass,
		    Record* Resource,
		    const TArdaInductorRecordStore<Record, Handle>& Records,
		    eastl::vector<Access> FArdaInductorCommandState::* Accesses,
		    Handle Access::* ResourceHandle)
		{
			std::lock_guard<std::mutex> Lock(Graph.mPassAccessMutex);
			const auto* Command = Graph.mPasses.TryGet(Pass);
			if (!Resource || !Command || !Graph.mActivePassAccess.count(Pass.GetIndex()) ||
			    Records.TryGet(Resource->GetHandle()) != Resource || !Resource->GetResource())
			{
				ARDA_CHECK_MSG("A pass requested an unavailable Inductor resource.");
			}
			// Ranges govern hazards; one declared access admits the resource's parent object.
			const auto& Declared = Command->GetState().*Accesses;
			if (eastl::none_of(Declared.begin(),
			        Declared.end(),
			        [&](const auto& State)
			        {
				        return State.*ResourceHandle == Resource->GetHandle();
			        }))
			{
				ARDA_CHECK_MSG("A pass requested a resource absent from its declared accesses.");
			}
			return Resource->GetResource().Get();
		}

		[[nodiscard]] arda::EArdaRHIResourceState MergePassState(arda::EArdaRHIResourceState Existing,
		    arda::EArdaRHIResourceState Required)
		{
			if (Existing == arda::EArdaRHIResourceState::Unknown)
			{
				return Required;
			}
			if (Existing == Required)
			{
				return Existing;
			}
			if (!IsWriteState(Existing) && !IsWriteState(Required))
			{
				return Existing | Required;
			}
			ARDA_CHECK_MSG("A Inductor command declares conflicting states for one resource.");
		}

		void LowerBarriers(FArdaInductorCommandProgram::FArdaImpl& Graph)
		{
			eastl::vector<eastl::vector<arda::EArdaRHIResourceState>> TextureStates;
			TextureStates.reserve(Graph.mTextures.GetCount());
			for (const auto& Texture : Graph.mTextures.GetEntries())
			{
				const arda::FArdaRHITextureDesc& Desc = Texture->GetDesc();
				arda::EArdaRHIResourceState Initial = Texture->GetInitialState();
				if (Initial == arda::EArdaRHIResourceState::Unknown)
				{
					Initial = arda::EArdaRHIResourceState::Common;
				}
				TextureStates.emplace_back(GetInductorTextureStateCount(Desc), Initial);
			}

			eastl::vector<arda::EArdaRHIResourceState> BufferStates;
			BufferStates.reserve(Graph.mBuffers.GetCount());
			for (const auto& Buffer : Graph.mBuffers.GetEntries())
			{
				arda::EArdaRHIResourceState Initial = Buffer->GetInitialState();
				if (Initial == arda::EArdaRHIResourceState::Unknown)
				{
					Initial = arda::EArdaRHIResourceState::Common;
				}
				BufferStates.push_back(Initial);
			}
			eastl::vector<arda::EArdaRHIResourceState> AccelStructStates;
			for (const auto& AccelStruct : Graph.mAccelStructs.GetEntries())
			{
				auto Initial = AccelStruct->GetInitialState();
				AccelStructStates.push_back(
				    Initial == arda::EArdaRHIResourceState::Unknown ? arda::EArdaRHIResourceState::Common : Initial);
			}

			for (const auto& Pass : Graph.mPasses.GetEntries())
			{
				Pass->GetState().mTextureTransitions.clear();
				Pass->GetState().mBufferTransitions.clear();
				Pass->GetState().mAccelStructTransitions.clear();
			}

			for (FArdaInductorCommandHandle Handle : Graph.mPlan.mExecutionOrder)
			{
				FArdaInductorCommand& Pass = Graph.mPasses.Get(Handle);
				if (Pass.GetState().mbSentinel)
				{
					continue;
				}

				// Merge all declarations before advancing the tracked texture state.
				eastl::vector<eastl::vector<arda::EArdaRHIResourceState>> RequiredTextures(Graph.mTextures.GetCount());
				for (const FArdaInductorTextureAccess& Access : Pass.GetState().mTextureStates)
				{
					const arda::EArdaRHIResourceState RequiredState =
					    NormalizeStateForPipeline(Access.mState, Pass.GetState().mQueue);
					const FArdaInductorTexture& Texture = Graph.mTextures.Get(Access.mTexture);
					const arda::FArdaRHITextureDesc& Desc = Texture.GetDesc();
					auto& Required = RequiredTextures[Access.mTexture.GetIndex()];
					if (Required.empty())
					{
						Required.resize(GetInductorTextureStateCount(Desc), arda::EArdaRHIResourceState::Unknown);
					}
					VisitInductorTextureCells(Desc,
					    Access.mSubresources,
					    [&](const auto&, size_t Index)
					    {
						    Required[Index] = MergePassState(Required[Index], RequiredState);
					    });
				}

				// Emit cell-sized records with explicit before/after continuity.
				for (uint32_t TextureIndex = 0; TextureIndex < RequiredTextures.size(); ++TextureIndex)
				{
					const auto& Required = RequiredTextures[TextureIndex];
					if (Required.empty())
					{
						continue;
					}
					const FArdaInductorTexture& Texture = Graph.mTextures.Get(FArdaInductorTextureHandle(TextureIndex));
					const arda::FArdaRHITextureDesc& Desc = Texture.GetDesc();
					auto& Current = TextureStates[TextureIndex];
					VisitInductorTextureCells(Desc,
					    {},
					    [&](const auto& Cell, size_t Index)
					    {
						    if (Required[Index] == EArdaRHIResourceState::Unknown)
						    {
							    return;
						    }
						    const bool UavBarrier = Current[Index] == Required[Index] && IsUAVState(Required[Index]);
						    Pass.GetState().mTextureTransitions.push_back({FArdaInductorTextureHandle(TextureIndex),
						        Cell,
						        Current[Index],
						        Required[Index],
						        UavBarrier});
						    Current[Index] = Required[Index];
					    });
				}

				// Buffer ranges are validated elsewhere but share one state slot.
				eastl::vector<arda::EArdaRHIResourceState> RequiredBuffers(Graph.mBuffers.GetCount(),
				    arda::EArdaRHIResourceState::Unknown);
				for (const FArdaInductorBufferAccess& Access : Pass.GetState().mBufferStates)
				{
					const uint32_t Index = Access.mBuffer.GetIndex();
					RequiredBuffers[Index] = MergePassState(RequiredBuffers[Index],
					    NormalizeStateForPipeline(Access.mState, Pass.GetState().mQueue));
				}
				for (uint32_t BufferIndex = 0; BufferIndex < RequiredBuffers.size(); ++BufferIndex)
				{
					if (RequiredBuffers[BufferIndex] == arda::EArdaRHIResourceState::Unknown)
					{
						continue;
					}
					const bool bUAVBarrier = BufferStates[BufferIndex] == RequiredBuffers[BufferIndex] &&
					    IsUAVState(RequiredBuffers[BufferIndex]);

					Pass.GetState().mBufferTransitions.push_back({FArdaInductorBufferHandle(BufferIndex),
					    BufferStates[BufferIndex],
					    RequiredBuffers[BufferIndex],
					    bUAVBarrier});
					BufferStates[BufferIndex] = RequiredBuffers[BufferIndex];
				}

				eastl::vector<arda::EArdaRHIResourceState> RequiredAccelStructs(Graph.mAccelStructs.GetCount(),
				    arda::EArdaRHIResourceState::Unknown);
				for (const FArdaInductorAccelerationStructureAccess& Access : Pass.GetState().mAccelStructStates)
				{
					const uint32_t Index = Access.mAccelStruct.GetIndex();
					RequiredAccelStructs[Index] = MergePassState(RequiredAccelStructs[Index],
					    NormalizeStateForPipeline(Access.mState, Pass.GetState().mQueue));
				}
				for (uint32_t Index = 0; Index < RequiredAccelStructs.size(); ++Index)
				{
					const auto Required = RequiredAccelStructs[Index];
					if (Required == arda::EArdaRHIResourceState::Unknown)
					{
						continue;
					}
					Pass.GetState().mAccelStructTransitions.push_back(
					    {FArdaInductorAccelerationStructureHandle(Index), AccelStructStates[Index], Required});
					AccelStructStates[Index] = Required;
				}
			}

			// The synthetic epilogue owns graph-exit transitions and UAV ordering.
			FArdaInductorCommand& Epilogue = Graph.mPasses.Get(Graph.mPlan.mEpilogue);
			for (const auto& Texture : Graph.mTextures.GetEntries())
			{
				if (!Texture->GetFirstUse().IsValid())
				{
					continue;
				}
				const arda::FArdaRHITextureDesc& Desc = Texture->GetDesc();
				auto& Current = TextureStates[Texture->GetHandle().GetIndex()];
				VisitInductorTextureCells(Desc,
				    {},
				    [&](const auto& Cell, size_t Index)
				    {
					    const bool UavBarrier =
					        Current[Index] == Texture->GetFinalState() && IsUAVState(Current[Index]);
					    if (Current[Index] != Texture->GetFinalState() || UavBarrier)
					    {
						    Epilogue.GetState().mTextureTransitions.push_back(
						        {Texture->GetHandle(), Cell, Current[Index], Texture->GetFinalState(), UavBarrier});
					    }
				    });
			}
			for (const auto& Buffer : Graph.mBuffers.GetEntries())
			{
				if (!Buffer->GetFirstUse().IsValid())
				{
					continue;
				}
				const uint32_t Index = Buffer->GetHandle().GetIndex();
				const bool bUAVBarrier =
				    BufferStates[Index] == Buffer->GetFinalState() && IsUAVState(BufferStates[Index]);
				if (BufferStates[Index] != Buffer->GetFinalState() || bUAVBarrier)
				{
					Epilogue.GetState().mBufferTransitions.push_back(
					    {Buffer->GetHandle(), BufferStates[Index], Buffer->GetFinalState(), bUAVBarrier});
				}
			}
			for (const auto& AccelStruct : Graph.mAccelStructs.GetEntries())
			{
				if (!AccelStruct->GetFirstUse().IsValid())
				{
					continue;
				}
				const uint32_t Index = AccelStruct->GetHandle().GetIndex();
				if (AccelStructStates[Index] != AccelStruct->GetFinalState())
				{
					Epilogue.GetState().mAccelStructTransitions.push_back(
					    {AccelStruct->GetHandle(), AccelStructStates[Index], AccelStruct->GetFinalState()});
				}
			}
		}
	}

	FArdaInductorCommandProgram::FArdaInductorCommandProgram(FArdaRHIDeviceRef Device)
	    : mImpl(eastl::make_unique<FArdaImpl>(eastl::move(Device)))
	{
		mImpl->mPlan.mPrologue = mImpl->mPasses.Append("Inductor entry", EArdaRHIQueueType::Graphics);
		mImpl->mPasses.Get(mImpl->mPlan.mPrologue).GetState().mbSentinel = true;
	}

	FArdaInductorCommandProgram::~FArdaInductorCommandProgram() = default;

	const FArdaInductorCommand& FArdaInductorCommandProgram::GetCommand(FArdaInductorCommandHandle Command) const
	{
		return mImpl->mPasses.Get(Command);
	}

	FArdaInductorTexture* FArdaInductorCommandProgram::BindTexture(FArdaRHITextureRef Resource,
	    EArdaRHIResourceState State,
	    eastl::string Name)
	{
		return BindResource(mImpl->mbCompiled,
		    mImpl->mTextures,
		    mImpl->mImportedTextures,
		    eastl::move(Resource),
		    State,
		    eastl::move(Name));
	}

	FArdaInductorBuffer* FArdaInductorCommandProgram::BindBuffer(FArdaRHIBufferRef Resource,
	    EArdaRHIResourceState State,
	    eastl::string Name)
	{
		return BindResource(mImpl->mbCompiled,
		    mImpl->mBuffers,
		    mImpl->mImportedBuffers,
		    eastl::move(Resource),
		    State,
		    eastl::move(Name));
	}

	FArdaInductorAccelerationStructure* FArdaInductorCommandProgram::BindAccelerationStructure(
	    FArdaRHIAccelStructRef Resource,
	    EArdaRHIResourceState State,
	    eastl::string Name)
	{
		return BindResource(mImpl->mbCompiled,
		    mImpl->mAccelStructs,
		    mImpl->mImportedAccelStructs,
		    eastl::move(Resource),
		    State,
		    eastl::move(Name));
	}

	FArdaInductorCommandHandle FArdaInductorCommandProgram::AppendCommand(eastl::string Name,
	    EArdaRHIQueueType Queue,
	    FArdaInductorCommandAccesses Accesses,
	    FArdaInductorRecordFunction Record,
	    bool Serial,
	    bool AtSubmit)
	{
		if (mImpl->mbCompiled)
		{
			ARDA_CHECK_MSG("Inductor command program is already finalized.");
		}
		const auto Handle = mImpl->mPasses.Append(eastl::move(Name), Queue, eastl::move(Record), Serial, AtSubmit);
		auto& State = mImpl->mPasses.Get(Handle).GetState();
		State.mTextureStates = eastl::move(Accesses.mTextures);
		State.mBufferStates = eastl::move(Accesses.mBuffers);
		State.mAccelStructStates = eastl::move(Accesses.mAccelerationStructures);
		for (const auto& A : State.mTextureStates)
		{
			mImpl->mTextures.Get(A.mTexture).MarkUsed(Handle);
		}
		for (const auto& A : State.mBufferStates)
		{
			mImpl->mBuffers.Get(A.mBuffer).MarkUsed(Handle);
		}
		for (const auto& A : State.mAccelStructStates)
		{
			mImpl->mAccelStructs.Get(A.mAccelStruct).MarkUsed(Handle);
		}
		return Handle;
	}

	void FArdaInductorCommandProgram::AddDependency(FArdaInductorCommandHandle Producer,
	    FArdaInductorCommandHandle Consumer)
	{
		if (mImpl->mbCompiled || !mImpl->mPasses.TryGet(Producer) || !mImpl->mPasses.TryGet(Consumer) ||
		    !(Producer < Consumer))
		{
			ARDA_CHECK_MSG("Inductor command dependencies must follow its accepted schedule.");
		}
		auto& Producers = mImpl->mPasses.Get(Consumer).GetState().mProducers;
		if (eastl::find(Producers.begin(), Producers.end(), Producer) == Producers.end())
		{
			Producers.push_back(Producer);
		}
	}

	const FArdaInductorCommandPlan& FArdaInductorCommandProgram::Finalize()
	{
		if (mImpl->mbCompiled)
		{
			return mImpl->mPlan;
		}
		auto& G = *mImpl;
		G.mPlan.mEpilogue = G.mPasses.Append("Inductor exit", EArdaRHIQueueType::Graphics);
		G.mPasses.Get(G.mPlan.mEpilogue).GetState().mbSentinel = true;
		for (const auto& Command : G.mPasses.GetEntries())
		{
			G.mPlan.mExecutionOrder.push_back(Command->GetHandle());
			if (Command->GetHandle() != G.mPlan.mEpilogue)
			{
				AddDependency(Command->GetHandle(), G.mPlan.mEpilogue);
			}
		}
		LowerBarriers(G);
		for (const auto& Command : G.mPasses.GetEntries())
		{
			for (auto Producer : Command->GetState().mProducers)
			{
				const auto Source = G.mPasses.Get(Producer).GetState().mQueue;
				if (Source != Command->GetState().mQueue)
				{
					G.mPlan.mQueueDependencies.push_back(
					    {Producer.GetIndex(), Command->GetHandle().GetIndex(), Source, Command->GetState().mQueue});
				}
			}
		}
		G.mbCompiled = true;
		return G.mPlan;
	}

	void FArdaInductorCommandProgram::BeginPassAccess(FArdaInductorCommandHandle Pass)
	{
		std::lock_guard<std::mutex> Lock(mImpl->mPassAccessMutex);
		const FArdaInductorCommand* PassRecord = mImpl->mPasses.TryGet(Pass);
		if (!mImpl->mbExecutionStarted || mImpl->mbExecuted || mImpl->mbFailed || PassRecord == nullptr ||
		    PassRecord->GetState().mbSentinel || !mImpl->mActivePassAccess.insert(Pass.GetIndex()).second)
		{
			ARDA_CHECK_MSG("A Inductor command physical-access gate cannot be opened.");
		}
	}

	void FArdaInductorCommandProgram::EndPassAccess(FArdaInductorCommandHandle Pass) noexcept
	{
		std::lock_guard<std::mutex> Lock(mImpl->mPassAccessMutex);
		mImpl->mActivePassAccess.erase(Pass.GetIndex());
	}

	arda::IArdaRHITexture* FArdaInductorCommandProgram::ResolveTextureForPass(FArdaInductorCommandHandle Pass,
	    FArdaInductorTexture* Texture) const
	{
		return ResolveResourceForPass(*mImpl,
		    Pass,
		    Texture,
		    mImpl->mTextures,
		    &FArdaInductorCommandState::mTextureStates,
		    &FArdaInductorTextureAccess::mTexture);
	}

	arda::IArdaRHIBuffer* FArdaInductorCommandProgram::ResolveBufferForPass(FArdaInductorCommandHandle Pass,
	    FArdaInductorBuffer* Buffer) const
	{
		return ResolveResourceForPass(*mImpl,
		    Pass,
		    Buffer,
		    mImpl->mBuffers,
		    &FArdaInductorCommandState::mBufferStates,
		    &FArdaInductorBufferAccess::mBuffer);
	}

	arda::IArdaRHIAccelStruct* FArdaInductorCommandProgram::ResolveAccelStructForPass(FArdaInductorCommandHandle Pass,
	    FArdaInductorAccelerationStructure* AccelStruct) const
	{
		return ResolveResourceForPass(*mImpl,
		    Pass,
		    AccelStruct,
		    mImpl->mAccelStructs,
		    &FArdaInductorCommandState::mAccelStructStates,
		    &FArdaInductorAccelerationStructureAccess::mAccelStruct);
	}

	FArdaInductorPassContext::FArdaInductorPassContext(FArdaInductorCommandProgram& Program,
	    FArdaInductorCommandHandle Command,
	    IArdaRHICommandList& Commands,
	    EArdaRHIQueueType Queue)
	    : mUnsafeRawCommandList(Commands),
	      mQueue(Queue),
	      mGraph(Program),
	      mPass(Command)
	{
		mGraph.BeginPassAccess(mPass);
	}

	FArdaInductorPassContext::~FArdaInductorPassContext() noexcept
	{
		mGraph.EndPassAccess(mPass);
	}

	IArdaRHITexture* FArdaInductorPassContext::GetTexture(FArdaInductorTexture* Resource) const
	{
		return mGraph.ResolveTextureForPass(mPass, Resource);
	}

	IArdaRHIBuffer* FArdaInductorPassContext::GetBuffer(FArdaInductorBuffer* Resource) const
	{
		return mGraph.ResolveBufferForPass(mPass, Resource);
	}

	IArdaRHIAccelStruct* FArdaInductorPassContext::GetAccelStruct(FArdaInductorAccelerationStructure* Resource) const
	{
		return mGraph.ResolveAccelStructForPass(mPass, Resource);
	}
}
