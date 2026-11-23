// Documentation snapshot of Source/ArdaTests/Examples/PixelSort/Private/Nodes/ArdaPixelSortSortNode.cpp.
#include "Nodes/ArdaPixelSortSortNode.h"
#include "ArdaPixelSortNodeInternal.h"

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaPixelSortSortNode, PixelSortSort);

	FArdaRHIStatus FArdaPixelSortSortNode::DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
	{
		FArdaRHITextureDesc D;
		D.mWidth = P.mWidth;
		D.mHeight = P.mHeight;
		D.mFormat = EArdaRHIFormat::RGBA8UInt;
		D.mbCudaInterop = bool(CheckPixelSortNativeSupport(C.GetDevice()));
		D.mUsage = EArdaRHITextureUsage::UnorderedAccess | EArdaRHITextureUsage::ShaderResource;
		return C.Texture(P.mSorted, "Output", D);
	}

	TArdaRHIResult<EArdaNativeExecutionMode> FArdaPixelSortSortNode::SelectNativeExecutionMode(
	    const FArdaNativeComputeContext& Context,
	    eastl::string& ProviderId)
	{
		if (auto Status = EnsurePixelSortOperandRegistered(); !Status)
		{
			return {{}, eastl::move(Status)};
		}
		return GetArdaNativeComputeRegistry().QueryExecutionMode(ArdaPixelSortOperandId, Context, &ProviderId);
	}

	FArdaDependencyNodeMetadata FArdaPixelSortSortNode::GetMetadata()
	{
		return {"example.pixel-sort.radix", 1};
	}

	eastl::string FArdaPixelSortSortNode::GetCanonicalKey(const FArdaParameters& P)
	{
		return FArdaDependencyKeyBuilder()
		    .Resource(P.mNoise)
		    .Resource(P.mSorted)
		    .Value(reinterpret_cast<uintptr_t>(P.mInput.get()))
		    .Value(P.mWidth)
		    .Value(P.mHeight)
		    .Build();
	}

	FArdaRHIStatus FArdaPixelSortSortNode::Validate(const FArdaParameters& P)
	{
		if (!P.mInput)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "PixelSort sorting requires dynamic sort inputs.");
		}
		return ValidatePixelSortExtent(P.mWidth, P.mHeight);
	}

	TArdaRHIResult<FArdaNativeComputePlan> FArdaPixelSortSortNode::PrepareNative(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		const auto Invocation = MakePixelSortInvocation(C.GetTexture(P.mNoise),
		    C.GetTexture(P.mSorted),
		    {P.mWidth, P.mHeight, P.mInput->mChannel, P.mInput->mThreshold});
		return C.PrepareNativeCompute(GetArdaNativeComputeRegistry(),
		    ArdaPixelSortOperandId,
		    Invocation,
		    EArdaNativeComputePreference::PreferNative);
	}
}
