#include "Inductor/Rules/ArdaInductorPipelineInternal.h"

namespace arda::pipeline_detail
{
	bool SameLayout(const FArdaRHIBindingLayoutRef& A, const FArdaRHIBindingLayoutRef& B)
	{
		FArdaPipelineKeyWriter Left, Right;
		Left.Layout(A);
		Right.Layout(B);
		return Left.mBytes == Right.mBytes;
	}

	eastl::vector<FArdaRHIBindingLayoutRef>& GlobalLayouts(FArdaInductorPipelineConfiguration& C)
	{
		switch (C.mKind)
		{
		case EArdaPipelineStateKind::Compute:
			return C.mCompute.mDesc.mBindingLayouts;
		case EArdaPipelineStateKind::Graphics:
			return C.mGraphics.mDesc.mBindingLayouts;
		case EArdaPipelineStateKind::Meshlet:
			return C.mMeshlet.mDesc.mBindingLayouts;
		case EArdaPipelineStateKind::RayTracing:
			return C.mRayTracing.mDesc.mGlobalBindingLayouts;
		default:
			return C.mWorkGraph.mDesc.mGlobalBindingLayouts;
		}
	}

	FArdaRHIStatus MergeLayouts(eastl::vector<FArdaRHIBindingLayoutRef>& Destination,
	    const eastl::vector<FArdaRHIBindingLayoutRef>& Source)
	{
		for (const auto& Layout : Source)
		{
			if (!Layout)
			{
				return Invalid("Pipeline global binding layouts cannot be null.");
			}
			bool Duplicate = false;
			for (const auto& Other : Destination)
			{
				if (SameLayout(Other, Layout))
				{
					Duplicate = true;
					continue;
				}
				const auto& A = Other->GetDesc();
				const auto& B = Layout->GetDesc();
				if (A.mRegisterSpace == B.mRegisterSpace &&
				    (HasAnyFlags(A.mVisibility, B.mVisibility) || Other->GetBindlessDesc() ||
				        Layout->GetBindlessDesc()))
				{
					return Invalid(
					    "Pipeline layouts sharing a register space require identical declarations or disjoint shader visibility.");
				}
			}
			if (!Duplicate)
			{
				Destination.push_back(Layout);
			}
		}
		eastl::sort(Destination.begin(),
		    Destination.end(),
		    [](const auto& A, const auto& B)
		    {
			    if (A->GetDesc().mRegisterSpace != B->GetDesc().mRegisterSpace)
			    {
				    return A->GetDesc().mRegisterSpace < B->GetDesc().mRegisterSpace;
			    }
			    FArdaPipelineKeyWriter Left, Right;
			    Left.Layout(A);
			    Right.Layout(B);
			    return Left.mBytes < Right.mBytes;
		    });
		return {};
	}

	FArdaRHIStatus MergeLocalLayout(FArdaRHIBindingLayoutRef& Destination, const FArdaRHIBindingLayoutRef& Source)
	{
		if (Source && Destination && !SameLayout(Destination, Source))
		{
			return Invalid("One ray export or hit group has incompatible local layouts.");
		}
		if (Source)
		{
			Destination = Source;
		}
		return {};
	}
}
