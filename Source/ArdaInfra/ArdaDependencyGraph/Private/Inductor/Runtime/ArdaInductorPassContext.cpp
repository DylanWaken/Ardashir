#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Inductor/Compiler/ArdaInductorCommandProgramInternal.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace arda
{
	namespace
	{
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
