#include "Inductor/Runtime/ArdaDependencyExecutionContext.h"
#include "CUDA/ArdaNativeCudaProvider.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Inductor/Compiler/ArdaInductorCommandProgramInternal.h"
#include "Inductor/Runtime/ArdaInductorCudaContract.h"
#include "Inductor/Runtime/ArdaInductorRuntime.h"
#include "Inductor/Runtime/ArdaInductorRuntimeStatus.h"

namespace arda
{
	FArdaRHIStatus FArdaDependencyExecutionContext::AppendNativeCudaCompute(FArdaCudaSequence& Sequence,
	    const FArdaNativeComputeRegistry& Registry,
	    const char* OperandId,
	    const FArdaNativeInvocation& Invocation) const
	{
		FArdaNativeComputeContext Selection;
		ConfigureArdaLegacyCudaSelection(Selection);
		const auto Plan = PrepareNativeCompute(Registry, OperandId, Invocation, eastl::move(Selection));
		if (!Plan)
		{
			return Plan.mStatus;
		}
		if (Plan.mValue.HasBoundaryWork())
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::Unsupported,
			    "Native resource conversion requires a native compute node that records the operand boundaries.");
		}
		return AppendArdaNativeWorkToCudaSequence(Plan.mValue.GetWork(), Sequence);
	}

	void FArdaInductorRuntime::RecordLegacyCudaNodes(const eastl::vector<FArdaGraphNodeHandle>& Nodes,
	    FArdaDependencyExecutionContext& Context,
	    const eastl::shared_ptr<FArdaCudaGraphCache>& CudaCache,
	    const eastl::shared_ptr<FArdaCudaTimingQuery>& CudaTimer,
	    bool SampleTiming)
	{
		auto* GraphPtr = Context.mGraph;
		auto& PassContext = *Context.mPass;
		FArdaCudaSequence Sequence(GraphPtr->mCompile.mExecution->mDevice,
		    PassContext.mUnsafeRawCommandList.GetQueueType(),
		    CudaCache);
		for (auto Handle : Nodes)
		{
			Context.mNode = Handle;
			const auto& Node = GraphPtr->mTopology.TryGetNode(Handle)->mPayload;
			if (CudaTimer && SampleTiming)
			{
				PassContext.ReportStatus(Sequence.BeginTimingRegion(CudaTimer, Handle.GetIndex()));
			}
			if (!PassContext.GetStatus())
			{
				return;
			}
			const size_t FirstOperation = Sequence.GetOperationCount();
			PassContext.ReportStatus(Node.mDefinition->mPrepareCuda(Context,
			    GraphPtr->mCompile.mNodes.at(Handle).mParameters.get(),
			    Sequence));
			PassContext.ReportStatus(Sequence.GetStatus());
			if (PassContext.GetStatus() && Sequence.GetOperationCount() < FirstOperation)
			{
				PassContext.ReportStatus(MakeArdaInductorRuntimeError(
				    "CUDA nodes may append operations but cannot replace the shared sequence."));
			}
			if (PassContext.GetStatus() && Sequence.GetOperationCount() > FirstOperation)
			{
				const auto Declared = Context.GetNativeResourceAccesses();
				for (size_t Operation = FirstOperation;
				    PassContext.GetStatus() && Operation < Sequence.GetOperationCount();
				    ++Operation)
				{
					PassContext.ReportStatus(
					    ValidateArdaInductorCudaBindings(Sequence.GetOperationBindings(Operation), Declared));
				}
			}
			if (CudaTimer && SampleTiming)
			{
				PassContext.ReportStatus(Sequence.EndTimingRegion());
			}
			if (!PassContext.GetStatus())
			{
				return;
			}
		}
		PassContext.ReportStatus(Sequence.DispatchDeferred(PassContext.mUnsafeRawCommandList));
	}
}
