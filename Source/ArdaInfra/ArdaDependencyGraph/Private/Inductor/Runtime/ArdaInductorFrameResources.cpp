#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Runtime/ArdaDependencyExecutionContext.h"
#include "Inductor/Compiler/ArdaInductorCommandProgramInternal.h"
#include "Inductor/Runtime/ArdaInductorRuntimeStatus.h"
#include "Inductor/Rules/ArdaInductorResourceStates.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

#include <EASTL/unordered_set.h>
#include <cstring>

namespace arda
{
	FArdaRHIStatus FArdaInductorRuntime::MaterializeFrame(FArdaDependencyGraph::FArdaImpl& Graph,
	    FArdaInductorRuntime& Runtime,
	    const FArdaMemoryPlan& Plan,
	    uint32_t FrameIndex,
	    const eastl::vector<FArdaInductorReuseFrame>* Reuse,
	    FArdaRHICommandListRef& ImportStateQuery,
	    eastl::unique_ptr<FArdaInductorFrame>& Frame)
	{
		auto Storage = Reuse
		    ? TArdaRHIResult<FArdaMemoryResources>{(*Reuse)[FrameIndex].mMemory, {}}
		    : MaterializeArdaMemory(*Graph.mDevice, Plan, FrameIndex ? &Runtime.mFrames.front()->mMemory : nullptr);
		if (!Storage)
		{
			return Storage.mStatus;
		}
		Frame = eastl::make_unique<FArdaInductorFrame>();
		Frame->mIndex = FrameIndex;
		Frame->mMemory = eastl::move(Storage.mValue);
		Frame->mLowered = eastl::make_unique<FArdaInductorCommandProgram>(Graph.mDevice);
		Frame->mBuffers.resize(Plan.mRequests.size());
		Frame->mTextures.resize(Plan.mRequests.size());
		Frame->mAccelerationStructures.resize(Plan.mRequests.size());
		for (size_t Index = 0; Index < Plan.mRequests.size(); ++Index)
		{
			// Retained imports still count toward the memory budget, but resources
			// without scheduled users need no native state contract or command binding.
			if (Plan.mRequests[Index].mFirstUseNodes.empty())
			{
				continue;
			}
			const auto Name = Index < Graph.mResources.size()
			    ? Graph.mResources[Index].mName
			    : eastl::string("Inductor workspace ") + eastl::to_string(Index);
			if (Frame->mMemory.mBuffers[Index])
			{
				// External callers hand storage to the graph in its descriptor-declared
				// state on the graphics queue. Owned/persistent pool storage declares Common.
				Frame->mBuffers[Index] = Frame->mLowered->BindBuffer(Frame->mMemory.mBuffers[Index],
				    Reuse ? (*Reuse)[FrameIndex].mBufferStates[Index]
				          : NormalizeInitialState(Frame->mMemory.mBuffers[Index]->GetDesc().mInitialState),
				    Name);
			}
			if (Frame->mMemory.mAccelerationStructures[Index])
			{
				Frame->mAccelerationStructures[Index] =
				    Frame->mLowered->BindAccelerationStructure(Frame->mMemory.mAccelerationStructures[Index],
				        EArdaRHIResourceState::AccelStructRead,
				        Name);
			}

			if (Frame->mMemory.mTextures[Index])
			{
				auto State = Reuse ? (*Reuse)[FrameIndex].mTextureStates[Index]
				                   : NormalizeInitialState(Frame->mMemory.mTextures[Index]->GetDesc().mInitialState);
				if (!Reuse && Plan.mRequests[Index].mExternalTexture)
				{
					// Creation descriptors can retain Discard after WSI acquisition has
					// transitioned the image to Present. Preserve the state actually handed
					// to this graph, not the original allocation hint.
					if (!ImportStateQuery)
					{
						auto Created = Graph.mDevice->CreateCommandList(EArdaRHIQueueType::Graphics);
						if (!Created)
						{
							return Created.mStatus;
						}
						ImportStateQuery = eastl::move(Created.mValue);
					}
					// State queries are read-only and do not require opening or submitting a list.
					const auto Snapshot = ImportStateQuery->QueryTextureState(*Frame->mMemory.mTextures[Index], {});
					if (!Snapshot)
					{
						return Snapshot.mStatus;
					}
					State = Snapshot.mValue.mFacadeState;
					if (!Snapshot.mValue.IsConsistent() || State == EArdaRHIResourceState::Unknown ||
					    State == EArdaRHIResourceState::Discard)
					{
						return MakeArdaInductorRuntimeError(
						    "Imported textures require an initialized, uniform entry state. Initialize the image or acquire its swap-chain image before submitting the graph.");
					}
					if (Snapshot.mValue.mbFacadeQueueOwnerKnown &&
					    Snapshot.mValue.mFacadeQueueOwner != EArdaRHIQueueType::Graphics)
					{
						return MakeArdaInductorRuntimeError(
						    "Imported textures must be handed to the graph on the graphics queue.");
					}
				}
				Frame->mTextures[Index] = Frame->mLowered->BindTexture(Frame->mMemory.mTextures[Index], State, Name);
			}
		}
		return {};
	}
}
