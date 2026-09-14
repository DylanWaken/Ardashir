#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellCompactBlasNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
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

	eastl::string FArdaCornellCompactBlasNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Resource(P.mSource);
		Key.Resource(P.mDestination);
		Key.Value(P.mWorkspaceBytes);
		return Key.Build();
	}

	FArdaDependencyNodeDesc FArdaCornellCompactBlasNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;
		// Declare the native build or compaction inputs so the compiler owns synchronization.
		D.mAccesses = {{P.mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::AccelStructRead},
		    {P.mDestination, EArdaDependencyAccess::Write, EArdaRHIResourceState::AccelStructWrite}};

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
