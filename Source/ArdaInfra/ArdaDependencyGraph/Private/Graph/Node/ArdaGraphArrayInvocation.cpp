#include "Graph/Node/ArdaGraphEdit.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"

namespace arda
{
	void FArdaGraphEdit::MarkArrayInvocation(const TArdaNodeArray<FArdaGraphOperation>& Operations)
	{
		if (Operations.empty() || !CanRecord())
		{
			return;
		}
		auto Family = eastl::make_shared<FArdaNodeArrayInvocation>();
		Family->mLaneCount = Operations.size();
		auto& Topology = GetRecordingGraph().mImpl->mTopology;
		for (size_t Index = 0; Index < Operations.size(); ++Index)
		{
			auto& Node = Topology.TryGetNode(Operations[Index].GetHandle())->mPayload;
			Node.mArrayInvocation = Family;
			Node.mArrayLane = Index;
		}
	}
}
