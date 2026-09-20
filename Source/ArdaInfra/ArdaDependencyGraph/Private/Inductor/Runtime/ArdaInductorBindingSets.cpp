#include "Inductor/Compiler/ArdaInductorPch.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Rules/ArdaInductorBindingArguments.h"
#include "Inductor/Runtime/ArdaInductorBindings.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace arda
{
	FArdaRHIResourceRef Physical(const FArdaDependencyGraph::FArdaImpl& G,
	    FArdaInductorFrame& Frame,
	    FArdaDependencyResourceHandle R,
	    const FArdaRHISamplerRef& Sampler)
	{
		if (Sampler)
		{
			return FArdaRHIResourceRef(Sampler.Get());
		}
		const auto& D = G.mResources[R.mIndex];
		if (D.mbTexture)
		{
			return FArdaRHIResourceRef(Frame.mMemory.mTextures[R.mIndex].Get());
		}
		if (D.mExternalAccelerationStructure)
		{
			return FArdaRHIResourceRef(Frame.mMemory.mAccelerationStructures[R.mIndex].Get());
		}
		return FArdaRHIResourceRef(Frame.mMemory.mBuffers[R.mIndex].Get());
	}

	bool MatchesSchema(const FArdaDependencyShaderBindings& B, const FArdaRHIBindingLayoutRef& Layout)
	{
		if (!B.mMetadata || !Layout || Layout->GetBindlessDesc())
		{
			return false;
		}
		eastl::vector<FArdaRHIBindingLayoutDesc> Generated;
		if (!B.mMetadata->BuildBindingLayoutDescs(Generated))
		{
			return false;
		}
		return eastl::any_of(Generated.begin(),
		    Generated.end(),
		    [&](const auto& D)
		    {
			    return D == Layout->GetDesc();
		    });
	}

	FArdaRHIBindingSetDesc MakeSet(const FArdaDependencyGraph::FArdaImpl& G,
	    FArdaInductorFrame& Frame,
	    const FArdaDependencyShaderBindings& B,
	    const FArdaRHIBindingLayoutRef& Layout)
	{
		FArdaRHIBindingSetDesc Set;
		Set.mLayout = Layout;
		eastl::vector<FArdaFlattenedShaderParameterMember> Members;
		B.mMetadata->GetFlattenedMembers(Members);
		for (const auto& F : Members)
		{
			const auto& M = *F.mMember;
			if (M.mKind == EArdaShaderParameterKind::Value || M.mKind == EArdaShaderParameterKind::PushConstants ||
			    M.mRegisterSpace != Layout->GetDesc().mRegisterSpace || M.mVisibility != Layout->GetDesc().mVisibility)
			{
				continue;
			}
			for (uint32_t E = 0; E < M.mArrayCount; ++E)
			{
				const auto* R = FindArgument(B, F.mPath, E);
				FArdaRHIBindingItem I;
				I.mSlot = M.mSlot;
				I.mArrayElement = E;
				I.mType = M.mBindingType;
				I.mView = R->mView;
				I.mResource = Physical(G, Frame, R->mResource, R->mSampler);
				Set.mItems.push_back(eastl::move(I));
			}
		}
		return Set;
	}
}
