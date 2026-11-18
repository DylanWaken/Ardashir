#pragma once

#include "BackendImpl/ArdaUnrealBackend.h"
#include "ArdaDependencyGraph.h"
#include "RHI/CUDA/ArdaCudaParameters.h"

namespace arda
{
	struct FArdaUnrealCudaPixel
	{
		float mRed;
		float mGreen;
		float mBlue;
		float mAlpha;
	};

#define ARDA_UNREAL_CUDA_FIELDS(VALUE, BUFFER, SURFACE)                                                                \
	SURFACE(FArdaUnrealCudaPixel, mSurface, EArdaComputeAccess::ReadWrite, EArdaRHIFormat::RGBA32Float)                \
	VALUE(uint32_t, mWidth)                                                                                            \
	VALUE(uint32_t, mHeight)
	ARDA_CUDA_PARAMETER_STRUCT(FArdaUnrealCudaParameters, ARDA_UNREAL_CUDA_FIELDS)

	/** Owned UE reflection bindings and immutable Arda pipeline for one graph invocation. */
	struct FArdaUnrealComputeInvocation
	{
		FArdaRHIComputePipelineRef mPipeline;
		eastl::function<FArdaRHIStatus(FRHICommandListImmediate&)> mBind;
		eastl::function<FArdaRHIStatus(FRHICommandListImmediate&)> mUnbind;
		FIntVector mGroups;
	};

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaUnrealComputeNodeParameters)
		ARDA_NODE_TEXTURE(mInput, EArdaDependencyAccess::Read, EArdaRHIResourceState::NonPixelShaderResource)
		ARDA_NODE_TEXTURE(mOutput, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess)
		ARDA_NODE_PARAMETER(eastl::shared_ptr<const FArdaUnrealComputeInvocation>, mInvocation)
	ARDA_END_NODE_PARAMETER_STRUCT()

	ARDA_BEGIN_NODE_PARAMETER_STRUCT(FArdaUnrealCudaNodeParameters)
		ARDA_NODE_CUDA_TEXTURE(mSurface, FArdaUnrealCudaParameters, mSurface)
		ARDA_NODE_PARAMETER(eastl::shared_ptr<const IArdaCudaKernelEntry>, mEntry)
		ARDA_NODE_PARAMETER(uint32_t, mWidth)
		ARDA_NODE_PARAMETER(uint32_t, mHeight)
	ARDA_END_NODE_PARAMETER_STRUCT()

	class FArdaUnrealComputeNode final
	    : public TArdaNodeDefinition<FArdaUnrealComputeNode, FArdaUnrealComputeNodeParameters, EArdaNodeType::Compute>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaRHIStatus Record(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance);
	};

	class FArdaUnrealCudaNode final
	    : public TArdaNodeDefinition<FArdaUnrealCudaNode, FArdaUnrealCudaNodeParameters, EArdaNodeType::Cuda>
	{
	public:
		static FArdaDependencyNodeMetadata GetMetadata();
		static eastl::string GetCanonicalKey(const FArdaParameters& Parameters);
		static FArdaDependencyNodeRequirements GetRequirements(const FArdaParameters& Parameters);
		static FArdaRHIStatus PrepareCuda(FArdaDependencyExecutionContext& Context,
		    const FArdaParameters& Parameters,
		    const FArdaState& State,
		    FArdaInstanceState& Instance,
		    FArdaCudaSequence& Sequence);
	};

	ARDA_DECLARE_GRAPH_NODE(FArdaUnrealComputeNode, ArdaUnrealCompute);
	ARDA_DECLARE_GRAPH_NODE(FArdaUnrealCudaNode, ArdaUnrealCuda);
}
