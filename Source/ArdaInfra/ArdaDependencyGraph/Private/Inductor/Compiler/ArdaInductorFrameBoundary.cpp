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
	FArdaRHIStatus FArdaInductorRuntime::FinalizeFrameBoundary(FArdaInductorFrame& Storage,
	    const eastl::vector<uint32_t>& OrderedAliasedImages,
	    const eastl::unordered_map<uint32_t, FArdaInductorCommandHandle>& ImageRetirements)
	{
		auto* Frame = &Storage;
		(void)Frame->mLowered->Finalize();
		// Complete the reusable boundary once, before publishing the immutable plan.
		// Equal-state transitions still matter when the last use owned a different
		// queue/family. Physical lowering derives the actual before-state and adds
		// the release/acquire and queue wait; an ordinary state equality is insufficient.
		auto& Lowered = *Frame->mLowered->mImpl;
		auto& Epilogue = Lowered.mPasses.Get(Lowered.mPlan.mEpilogue).GetState();
		eastl::unordered_set<uint32_t> RetiredTextureHandles;
		for (const auto Resource : OrderedAliasedImages)
		{
			if (!ImageRetirements.count(Resource))
			{
				return MakeArdaInductorRuntimeError("An aliased image has no retirement boundary.");
			}
			RetiredTextureHandles.insert(Frame->mTextures[Resource]->GetHandle().GetIndex());
		}
		Epilogue.mTextureTransitions.erase(eastl::remove_if(Epilogue.mTextureTransitions.begin(),
		                                       Epilogue.mTextureTransitions.end(),
		                                       [&](const auto& Transition)
		                                       {
			                                       return RetiredTextureHandles.count(Transition.mTexture.GetIndex()) !=
			                                           0;
		                                       }),
		    Epilogue.mTextureTransitions.end());
		for (const auto& Buffer : Lowered.mBuffers.GetEntries())
		{
			if (Buffer->GetFirstUse().IsValid() && Buffer->GetFirstUse() != Lowered.mPlan.mEpilogue &&
			    eastl::none_of(Epilogue.mBufferTransitions.begin(),
			        Epilogue.mBufferTransitions.end(),
			        [&Buffer](const auto& Transition)
			        {
				        return Transition.mBuffer == Buffer->GetHandle();
			        }))
			{
				Epilogue.mBufferTransitions.push_back(
				    {Buffer->GetHandle(), Buffer->GetFinalState(), Buffer->GetFinalState()});
			}
		}
		for (const auto& Texture : Lowered.mTextures.GetEntries())
		{
			if (RetiredTextureHandles.count(Texture->GetHandle().GetIndex()))
			{
				continue;
			}
			if (!Texture->GetFirstUse().IsValid() || Texture->GetFirstUse() == Lowered.mPlan.mEpilogue)
			{
				continue;
			}
			const auto& Desc = Texture->GetDesc();
			VisitInductorTextureCells(Desc,
			    {},
			    [&](const auto& Cell, size_t)
			    {
				    const bool Present = eastl::any_of(Epilogue.mTextureTransitions.begin(),
				        Epilogue.mTextureTransitions.end(),
				        [&Texture, &Cell](const auto& Transition)
				        {
					        return Transition.mTexture == Texture->GetHandle() && Transition.mSubresources == Cell;
				        });
				    if (!Present)
				    {
					    Epilogue.mTextureTransitions.push_back(
					        {Texture->GetHandle(), Cell, Texture->GetFinalState(), Texture->GetFinalState()});
				    }
			    });
		}
		return {};
	}
}
