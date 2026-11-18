#include "Graph/Node/ArdaGraphEditOrdering.h"

#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "RHI/Config/ArdaAssert.h"

#include <EASTL/sort.h>
#include <functional>
#include <queue>
#include <vector>

namespace arda
{
	namespace
	{
		struct FArdaEditResourceUse
		{
			uint32_t mEdit = 0;
			uint64_t mRevision = 0;
			bool mbVersioned = false;
		};
	}

	void NormalizeArdaGraphEditRevisions(FArdaDependencyGraph::FArdaImpl& Graph)
	{
		for (auto& Edit : Graph.mCommittedEdits)
		{
			eastl::unordered_map<FArdaDependencyResourceHandle, uint64_t, FArdaDependencyResourceHandleHash> Revisions;
			eastl::vector<FArdaGraphResourceRevision> Canonical;
			Canonical.reserve(Edit.mResources.size());
			for (const auto& Revision : Edit.mResources)
			{
				const auto Resource = Graph.ResolveResource(Revision.mResource);
				if (!Graph.HasResource(Resource))
				{
					ARDA_CHECK_MSG("Resource revision ordering contains a stale resource identity.");
				}
				const auto Existing = Revisions.emplace(Resource, Revision.mRevision);
				if (!Existing.second && Existing.first->second != Revision.mRevision)
				{
					ARDA_CHECK_MSG(
					    "Resource revision ordering is impossible: one edit assigns different versions to aliased storage.");
				}
				if (Existing.second)
				{
					Canonical.push_back({Resource, Revision.mRevision});
				}
				Graph.mResources[Resource.mIndex].mbRevisionOrdered = true;
			}
			Edit.mResources = eastl::move(Canonical);
		}
	}

	void OrderArdaGraphEdits(FArdaDependencyGraph::FArdaImpl& Graph)
	{
		bool bHasRevisions = false;
		for (const auto& Edit : Graph.mCommittedEdits)
		{
			bHasRevisions |= !Edit.mResources.empty();
		}
		if (!bHasRevisions)
		{
			return;
		}
		NormalizeArdaGraphEditRevisions(Graph);
		if (Graph.mCommittedEdits.size() > UINT32_MAX)
		{
			ARDA_CHECK_MSG("Resource revision ordering exceeds the supported edit count.");
		}
		const auto Count = uint32_t(Graph.mCommittedEdits.size());
		eastl::unordered_map<FArdaGraphNodeHandle, uint32_t, FArdaGraphNodeHandleHash> Owners;
		eastl::unordered_map<uint32_t, eastl::vector<FArdaEditResourceUse>> Resources;
		for (uint32_t EditIndex = 0; EditIndex < Count; ++EditIndex)
		{
			const auto& Edit = Graph.mCommittedEdits[EditIndex];
			eastl::unordered_map<uint32_t, uint64_t> Revisions;
			for (const auto& Revision : Edit.mResources)
			{
				const auto Resource = Graph.ResolveResource(Revision.mResource);
				if (!Graph.HasResource(Resource))
				{
					ARDA_CHECK_MSG("Resource revision ordering contains a stale resource identity.");
				}
				Revisions.emplace(Resource.mIndex, Revision.mRevision);
			}
			eastl::unordered_set<uint32_t> Accessed;
			for (auto Node : Edit.mNodes)
			{
				Node = Graph.ResolveNode(Node);
				const auto* Entry = Graph.mTopology.TryGetNode(Node);
				if (!Entry)
				{
					continue;
				}
				if (!Owners.emplace(Node, EditIndex).second)
				{
					ARDA_CHECK_MSG("Resource revision ordering assigns one operation to multiple edits.");
				}
				for (const auto& Access : Graph.GetNodeDescription(Entry->mPayload).mAccesses)
				{
					const auto Resource = Graph.ResolveResource(Access.mResource);
					if (!Graph.HasResource(Resource) || !Accessed.insert(Resource.mIndex).second)
					{
						continue;
					}
					const auto Revision = Revisions.find(Resource.mIndex);
					Resources[Resource.mIndex].push_back(
					    {EditIndex, Revision == Revisions.end() ? 0 : Revision->second, Revision != Revisions.end()});
				}
			}
		}
		eastl::vector<eastl::vector<uint32_t>> Edges(Count);
		eastl::vector<uint32_t> Incoming(Count, 0);
		eastl::unordered_set<uint64_t> UniqueEdges;
		const auto AddEdge = [&](uint32_t Before, uint32_t After)
		{
			if (Before != After && UniqueEdges.insert((uint64_t(Before) << 32) | After).second)
			{
				Edges[Before].push_back(After);
				++Incoming[After];
			}
		};
		for (const auto& Resource : Resources)
		{
			eastl::vector<FArdaEditResourceUse> Versioned;
			for (const auto& Use : Resource.second)
			{
				if (Use.mbVersioned)
				{
					Versioned.push_back(Use);
				}
			}
			if (Versioned.empty())
			{
				continue;
			}
			eastl::sort(Versioned.begin(),
			    Versioned.end(),
			    [](const auto& Left, const auto& Right)
			    {
				    return Left.mRevision != Right.mRevision ? Left.mRevision < Right.mRevision
				                                             : Left.mEdit < Right.mEdit;
			    });
			for (size_t Index = 1; Index < Versioned.size(); ++Index)
			{
				AddEdge(Versioned[Index - 1].mEdit, Versioned[Index].mEdit);
			}
			// Unversioned accesses are commit-order barriers. Each versioned participant connects
			// only to its neighboring barriers, keeping graph construction linear after sorting.
			uint32_t PreviousUnversioned = UINT32_MAX;
			eastl::vector<uint32_t> Segment;
			for (const auto& Use : Resource.second)
			{
				if (PreviousUnversioned != UINT32_MAX)
				{
					AddEdge(PreviousUnversioned, Use.mEdit);
				}
				if (Use.mbVersioned)
				{
					Segment.push_back(Use.mEdit);
				}
				else
				{
					for (const auto Before : Segment)
					{
						AddEdge(Before, Use.mEdit);
					}
					Segment.clear();
					PreviousUnversioned = Use.mEdit;
				}
			}
		}
		const auto AddNodeEdge = [&](FArdaGraphNodeHandle Before, FArdaGraphNodeHandle After)
		{
			const auto From = Owners.find(Graph.ResolveNode(Before));
			const auto To = Owners.find(Graph.ResolveNode(After));
			if (From != Owners.end() && To != Owners.end())
			{
				AddEdge(From->second, To->second);
			}
		};
		for (const auto& Edge : Graph.mManualEdges)
		{
			AddNodeEdge(Edge.first, Edge.second);
		}
		for (const auto Node : Graph.mTopology.GetNodes())
		{
			const auto& Desc = Graph.GetNodeDescription(Graph.mTopology.TryGetNode(Node)->mPayload);
			AddNodeEdge(Desc.mGraphicsInput.GetProducer(), Node);
			for (const auto& Input : Desc.mRayInputs)
			{
				AddNodeEdge(Input.GetProducer(), Node);
			}
		}
		std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> Ready;
		for (uint32_t Index = 0; Index < Count; ++Index)
		{
			if (Incoming[Index] == 0)
			{
				Ready.push(Index);
			}
		}
		eastl::vector<uint32_t> Order;
		Order.reserve(Count);
		while (!Ready.empty())
		{
			const auto Index = Ready.top();
			Ready.pop();
			Order.push_back(Index);
			for (const auto After : Edges[Index])
			{
				if (--Incoming[After] == 0)
				{
					Ready.push(After);
				}
			}
		}
		if (Order.size() != Count)
		{
			ARDA_CHECK_MSG(
			    "Resource revision ordering is impossible: shared resources or explicit dependencies form an edit cycle.");
		}
		// All validation precedes mutation. Legacy archived nodes without edit records retain
		// their relative order before new edits. Within each recorded edit, call order is stable.
		auto Nodes = Graph.mTopology.GetNodes();
		eastl::sort(Nodes.begin(),
		    Nodes.end(),
		    [&](const auto Left, const auto Right)
		    {
			    return Graph.mTopology.TryGetNode(Left)->mPayload.mAttachmentOrder <
			        Graph.mTopology.TryGetNode(Right)->mPayload.mAttachmentOrder;
		    });
		uint64_t Attachment = 1;
		for (const auto Node : Nodes)
		{
			if (!Owners.count(Node))
			{
				Graph.mTopology.TryGetNode(Node)->mPayload.mAttachmentOrder = Attachment++;
			}
		}
		for (const auto EditIndex : Order)
		{
			for (const auto Node : Graph.mCommittedEdits[EditIndex].mNodes)
			{
				if (auto* Entry = Graph.mTopology.TryGetNode(Graph.ResolveNode(Node)))
				{
					Entry->mPayload.mAttachmentOrder = Attachment++;
				}
			}
		}
		Graph.mNextAttachmentOrder = Attachment;
	}
}
