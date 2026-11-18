#include "ArdaUnrealGraphNodes.h"

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaUnrealComputeNode, ArdaUnrealCompute);
	ARDA_DEFINE_GRAPH_NODE(FArdaUnrealCudaNode, ArdaUnrealCuda);

	FArdaDependencyNodeMetadata FArdaUnrealComputeNode::GetMetadata()
	{
		return {"unreal-test.compute-postprocess", 1};
	}

	eastl::string FArdaUnrealComputeNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		return FArdaDependencyKeyBuilder()
		    .Resource(Parameters.mInput)
		    .Resource(Parameters.mOutput)
		    .Value(reinterpret_cast<uintptr_t>(Parameters.mInvocation.get()))
		    .Build();
	}

	FArdaRHIStatus FArdaUnrealComputeNode::Record(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&)
	{
		if (!Parameters.mInvocation)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, "Unreal compute bindings are missing.");
		}
		const FArdaUnrealComputeInvocation& Invocation = *Parameters.mInvocation;
		FArdaRHIComputeState State;
		State.mPipeline = Invocation.mPipeline;
		if (auto Status = Context.GetCommands().SetComputeState(State); !Status)
		{
			return Status;
		}
		// Unreal owns its shader's reflected loose-uniform offsets and texture binding ABI.
		if (auto Status = RecordUnrealCommands(Invocation.mBind); !Status)
		{
			return Status;
		}
		Context.GetCommands().Dispatch(Invocation.mGroups.X, Invocation.mGroups.Y, Invocation.mGroups.Z);
		return RecordUnrealCommands(Invocation.mUnbind);
	}

	FArdaDependencyNodeMetadata FArdaUnrealCudaNode::GetMetadata()
	{
		return {"unreal-test.cuda-postprocess", 1};
	}

	eastl::string FArdaUnrealCudaNode::GetCanonicalKey(const FArdaParameters& Parameters)
	{
		return FArdaDependencyKeyBuilder()
		    .Resource(Parameters.mSurface)
		    .Value(reinterpret_cast<uintptr_t>(Parameters.mEntry.get()))
		    .Value(Parameters.mWidth)
		    .Value(Parameters.mHeight)
		    .Build();
	}

	FArdaDependencyNodeRequirements FArdaUnrealCudaNode::GetRequirements(const FArdaParameters&)
	{
		FArdaDependencyNodeRequirements Requirements;
		Requirements.mbRequireCuda = true;
		Requirements.mbRequireCudaSurfaces = true;
		return Requirements;
	}

	FArdaRHIStatus FArdaUnrealCudaNode::PrepareCuda(FArdaDependencyExecutionContext& Context,
	    const FArdaParameters& Parameters,
	    const FArdaState&,
	    FArdaInstanceState&,
	    FArdaCudaSequence& Sequence)
	{
		FArdaUnrealCudaParameters Host;
		if (auto Status = ResolveNodeCudaParameters(Context, Parameters, Host); !Status)
		{
			return Status;
		}
		Host.mWidth = Parameters.mWidth;
		Host.mHeight = Parameters.mHeight;
		auto Plan = eastl::make_shared<FArdaCudaDispatchPlan>();
		Plan->mDevice = Context.GetDevice();
		Plan->mQueue = EArdaRHIQueueType::Graphics;
		if (auto Status = FArdaUnrealCudaParameters::GetCudaMetadata().Prepare(&Host, Plan->mDispatch); !Status)
		{
			return Status;
		}
		FArdaCudaKernel& Kernel = Plan->mDispatch.mKernels.front();
		Kernel.mEntry = Parameters.mEntry;
		Kernel.mGridSize[0] = (Parameters.mWidth + 7) / 8;
		Kernel.mGridSize[1] = (Parameters.mHeight + 7) / 8;
		Kernel.mBlockSize[0] = 8;
		Kernel.mBlockSize[1] = 8;
		return Sequence.AddPlan(Plan);
	}
}
