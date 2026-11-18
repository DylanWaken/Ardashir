#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellCompactBlasNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaCornellCompactBlasNode, CornellCompactBlas);

	FArdaDependencyNodeRequirements FArdaCornellCompactBlasNode::GetRequirements(const FArdaParameters&)
	{
		FArdaDependencyNodeRequirements R;
		R.mFeatures.mbRequireAccelerationStructures = true;
		R.mFeatures.mbRequireBottomLevelAccelerationStructures = true;
		R.mFeatures.mbRequireAccelerationStructureCompaction = true;
		return R;
	}

	FArdaDependencyNodeMetadata FArdaCornellCompactBlasNode::GetMetadata()
	{
		return {"cornell.compact_blas", 1};
	}

	FArdaDependencyNodeDesc FArdaCornellCompactBlasNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;
		// Declare the native build or compaction inputs so the compiler owns synchronization.

		return D;
	}

	FArdaRHIStatus FArdaCornellCompactBlasNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		auto& Commands = C.GetCommands();

		return Commands.CompactAccelStruct(*C.GetAccelerationStructure(P.mDestination),
		    *C.GetAccelerationStructure(P.mSource));
	}
}
