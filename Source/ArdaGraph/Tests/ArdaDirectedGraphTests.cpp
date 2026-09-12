#include "ArdaGraph.h"
#include <EASTL/string.h>
#include <gtest/gtest.h>

namespace
{
	using namespace arda;
	using FGraph = TArdaDirectedGraph<int, eastl::string>;

	TEST(ArdaDirectedGraph, MaintainsAllIndexesAndPreservesDuplicatePayloads)
	{
		FGraph Graph;
		const auto A = Graph.AddNode(10), B = Graph.AddNode(20), C = Graph.AddNode(30);
		const auto AB = Graph.AddEdge(A, B, "first");
		const auto AC = Graph.AddEdge(A, C, "second");
		ASSERT_TRUE(AB.mbInserted);
		ASSERT_TRUE(AC.mbInserted);
		const auto Duplicate = Graph.AddEdge(A, B, "replacement");
		EXPECT_FALSE(Duplicate.mbInserted);
		EXPECT_EQ(Duplicate.mHandle, AB.mHandle);
		EXPECT_EQ(Graph.TryGetEdge(AB.mHandle)->mPayload, "first");
		EXPECT_EQ(Graph.FindEdge(A, B), AB.mHandle);
		EXPECT_EQ(Graph.FindIncomingEdge(B, A), AB.mHandle);
		EXPECT_EQ(Graph.FindOutgoingEdge(A, B), AB.mHandle);
		EXPECT_EQ(Graph.GetIncomingEdges(B), (eastl::vector<FArdaGraphEdgeHandle>{AB.mHandle}));
		EXPECT_EQ(Graph.GetOutgoingEdges(A).size(), 2u);
		EXPECT_TRUE(Graph.RemoveEdge(AB.mHandle));
		EXPECT_FALSE(Graph.RemoveEdge(AB.mHandle));
		EXPECT_FALSE(Graph.FindEdge(A, B));
		EXPECT_FALSE(Graph.FindIncomingEdge(B, A));
		EXPECT_FALSE(Graph.FindOutgoingEdge(A, B));
		EXPECT_EQ(Graph.GetOutgoingEdges(A), (eastl::vector<FArdaGraphEdgeHandle>{AC.mHandle}));
		EXPECT_TRUE(Graph.RemoveEdge(AC.mHandle));
		EXPECT_EQ(Graph.GetEdgeCount(), 0u);
	}

	TEST(ArdaDirectedGraph, DeletingNodesRemovesSelfAndIncidentEdgesWithoutRevivingHandles)
	{
		FGraph Graph;
		const auto A = Graph.AddNode(1), B = Graph.AddNode(2), C = Graph.AddNode(3);
		const auto AB = Graph.AddEdge(A, B, "ab").mHandle;
		const auto BC = Graph.AddEdge(B, C, "bc").mHandle;
		const auto BB = Graph.AddEdge(B, B, "self").mHandle;
		const auto AC = Graph.AddEdge(A, C, "survivor").mHandle;
		EXPECT_TRUE(Graph.RemoveNode(B));
		EXPECT_FALSE(Graph.ContainsNode(B));
		EXPECT_FALSE(Graph.ContainsEdge(AB));
		EXPECT_FALSE(Graph.ContainsEdge(BC));
		EXPECT_FALSE(Graph.ContainsEdge(BB));
		EXPECT_TRUE(Graph.ContainsEdge(AC));
		EXPECT_EQ(Graph.GetNodeCount(), 2u);
		EXPECT_EQ(Graph.GetEdgeCount(), 1u);
		const auto Replacement = Graph.AddNode(4);
		EXPECT_EQ(Replacement.GetIndex(), B.GetIndex());
		EXPECT_GT(Replacement.GetGeneration(), B.GetGeneration());
		EXPECT_FALSE(Graph.AddEdge(B, C, "stale").mHandle);
		const auto NewEdge = Graph.AddEdge(A, Replacement, "new").mHandle;
		EXPECT_FALSE(Graph.ContainsEdge(AB));
		EXPECT_TRUE(Graph.ContainsEdge(NewEdge));
		Graph.Clear();
		EXPECT_EQ(Graph.GetNodeCount(), 0u);
		EXPECT_EQ(Graph.GetEdgeCount(), 0u);
		for (int Index = 0; Index < 3; ++Index)
		{
			Graph.AddNode(Index);
		}
		EXPECT_FALSE(Graph.ContainsNode(A));
		EXPECT_FALSE(Graph.ContainsNode(C));
		EXPECT_FALSE(Graph.ContainsNode(Replacement));
	}

	TEST(ArdaDirectedGraph, RejectsForeignHandlesAndTransfersIdentityOnMove)
	{
		FGraph Graph, Other;
		const auto Local = Graph.AddNode(1), Foreign = Other.AddNode(2);
		EXPECT_EQ(Local.GetIndex(), Foreign.GetIndex());
		EXPECT_EQ(Local.GetGeneration(), Foreign.GetGeneration());
		EXPECT_NE(Local.GetGraphIdentity(), Foreign.GetGraphIdentity());
		EXPECT_FALSE(Graph.ContainsNode(Foreign));
		EXPECT_FALSE(Graph.RemoveNode(Foreign));
		EXPECT_FALSE(Graph.AddEdge(Local, Foreign).mHandle);
		FGraph Moved(eastl::move(Graph));
		EXPECT_TRUE(Moved.ContainsNode(Local));
		EXPECT_FALSE(Graph.ContainsNode(Local));
		const auto New = Graph.AddNode(3);
		EXPECT_NE(New.GetGraphIdentity(), Local.GetGraphIdentity());
		EXPECT_FALSE(Moved.ContainsNode(New));
		Other = eastl::move(Moved);
		EXPECT_TRUE(Other.ContainsNode(Local));
		EXPECT_FALSE(Other.ContainsNode(Foreign));
		EXPECT_FALSE(Moved.ContainsNode(Local));
	}

	TEST(ArdaDirectedGraph, SnapshotEditsAreIsolatedUntilCommitted)
	{
		FGraph Graph;
		const auto A = Graph.AddNode(1), B = Graph.AddNode(2);
		const auto Edge = Graph.AddEdge(A, B, "original").mHandle;
		auto Edited = Graph.CloneSnapshot();
		EXPECT_EQ(Edited.GetGraphIdentity(), Graph.GetGraphIdentity());
		Edited.TryGetNode(A)->mPayload = 42;
		Edited.TryGetEdge(Edge)->mPayload = "edited";
		EXPECT_EQ(Graph.TryGetNode(A)->mPayload, 1);
		EXPECT_EQ(Graph.TryGetEdge(Edge)->mPayload, "original");
		EXPECT_TRUE(Edited.RemoveNode(B));
		const auto C = Edited.AddNode(3);
		EXPECT_FALSE(Graph.ContainsNode(C));
		EXPECT_TRUE(Graph.ContainsNode(B));
		Graph = eastl::move(Edited);
		EXPECT_EQ(Graph.TryGetNode(A)->mPayload, 42);
		EXPECT_TRUE(Graph.ContainsNode(C));
		EXPECT_FALSE(Graph.ContainsNode(B));
		EXPECT_FALSE(Graph.ContainsEdge(Edge));
	}

	TEST(ArdaDirectedGraph, AbandonedSnapshotHandlesNeverReviveWhenSlotsAreReused)
	{
		FGraph Graph;
		const auto A = Graph.AddNode(1), B = Graph.AddNode(2);
		FArdaGraphNodeHandle CanceledNode;
		FArdaGraphEdgeHandle CanceledEdge;
		{
			auto Edited = Graph.CloneSnapshot();
			CanceledNode = Edited.AddNode(3);
			CanceledEdge = Edited.AddEdge(A, B).mHandle;
		}
		const auto Node = Graph.AddNode(4);
		const auto Edge = Graph.AddEdge(A, B).mHandle;
		EXPECT_EQ(CanceledNode.GetIndex(), Node.GetIndex());
		EXPECT_EQ(CanceledEdge.GetIndex(), Edge.GetIndex());
		EXPECT_NE(CanceledNode.GetGeneration(), Node.GetGeneration());
		EXPECT_NE(CanceledEdge.GetGeneration(), Edge.GetGeneration());
		EXPECT_FALSE(Graph.ContainsNode(CanceledNode));
		EXPECT_FALSE(Graph.ContainsEdge(CanceledEdge));
		auto Sibling = Graph.CloneSnapshot();
		ASSERT_TRUE(Sibling.RemoveNode(B));
		const auto SiblingNode = Sibling.AddNode(5);
		ASSERT_TRUE(Graph.RemoveNode(B));
		const auto Replacement = Graph.AddNode(6);
		EXPECT_EQ(SiblingNode.GetIndex(), Replacement.GetIndex());
		EXPECT_NE(SiblingNode, Replacement);
		EXPECT_FALSE(Graph.ContainsNode(SiblingNode));
		EXPECT_FALSE(Sibling.ContainsNode(Replacement));
	}

	TEST(ArdaDirectedGraph, TopologicalOrderUsesStableTieBreaksAcrossMutation)
	{
		FGraph Graph;
		const auto A = Graph.AddNode(0), B = Graph.AddNode(1), C = Graph.AddNode(2), D = Graph.AddNode(3);
		const auto BD = Graph.AddEdge(B, D).mHandle;
		Graph.AddEdge(A, D);
		Graph.AddEdge(A, C);
		auto Result = Graph.TopologicalSort();
		EXPECT_TRUE(Result.IsAcyclic());
		EXPECT_EQ(Result.mOrder, (eastl::vector<FArdaGraphNodeHandle>{A, B, C, D}));
		Graph.RemoveEdge(BD);
		Graph.AddEdge(B, D);
		EXPECT_EQ(Graph.TopologicalSort().mOrder, Result.mOrder);
		EXPECT_TRUE(Graph.IsReachable(A, D));
		EXPECT_FALSE(Graph.IsReachable(C, D));
		EXPECT_TRUE(Graph.IsReachable(A, A));
		EXPECT_FALSE(Graph.IsReachable({}, A));
		EXPECT_EQ(Graph.GetReachableNodes(A), (eastl::vector<FArdaGraphNodeHandle>{A, C, D}));
	}

	TEST(ArdaDirectedGraph, ReportsConcreteCycleSeparatelyFromBlockedDescendants)
	{
		FGraph Graph;
		const auto A = Graph.AddNode(0), B = Graph.AddNode(1), C = Graph.AddNode(2);
		const auto Tail = Graph.AddNode(3), Independent = Graph.AddNode(4);
		Graph.AddEdge(A, B);
		Graph.AddEdge(B, C);
		Graph.AddEdge(C, A);
		Graph.AddEdge(C, Tail);
		const auto Result = Graph.TopologicalSort();
		EXPECT_FALSE(Result.IsAcyclic());
		EXPECT_EQ(Result.mOrder, (eastl::vector<FArdaGraphNodeHandle>{Independent}));
		EXPECT_EQ(Result.mBlockedNodes, (eastl::vector<FArdaGraphNodeHandle>{A, B, C, Tail}));
		EXPECT_EQ(Result.mCycle, (eastl::vector<FArdaGraphNodeHandle>{A, B, C, A}));
		for (size_t Index = 1; Index < Result.mCycle.size(); ++Index)
		{
			EXPECT_TRUE(Graph.FindEdge(Result.mCycle[Index - 1], Result.mCycle[Index]));
		}
		EXPECT_TRUE(Graph.IsReachable(A, Tail));
		EXPECT_FALSE(Graph.IsReachable(Tail, A));
		FGraph Self;
		const auto Node = Self.AddNode(1);
		Self.AddEdge(Node, Node);
		EXPECT_EQ(Self.TopologicalSort().mCycle, (eastl::vector<FArdaGraphNodeHandle>{Node, Node}));
	}

	struct FCollidingHasher
	{
		size_t operator()(FArdaGraphNodeHandle) const noexcept
		{
			return 0;
		}
	};

	TEST(ArdaDirectedGraph, HashCollisionsDoNotConflateEndpointsOrBreakDenseRemoval)
	{
		TArdaDirectedGraph<int, int, FCollidingHasher> Graph;
		eastl::vector<FArdaGraphNodeHandle> Nodes;
		for (int Index = 0; Index < 24; ++Index)
		{
			Nodes.push_back(Graph.AddNode(Index));
		}
		for (size_t From = 0; From < Nodes.size(); ++From)
		{
			for (size_t To = From + 1; To < Nodes.size(); ++To)
			{
				ASSERT_TRUE(Graph.AddEdge(Nodes[From], Nodes[To], int(From * 24 + To)).mbInserted);
			}
		}
		for (size_t From = 0; From < Nodes.size(); ++From)
		{
			for (size_t To = From + 1; To < Nodes.size(); ++To)
			{
				if ((From + To) % 3 == 0)
				{
					EXPECT_TRUE(Graph.RemoveEdge(Graph.FindEdge(Nodes[From], Nodes[To])));
				}
			}
		}
		for (size_t From = 0; From < Nodes.size(); ++From)
		{
			for (size_t To = From + 1; To < Nodes.size(); ++To)
			{
				const auto Edge = Graph.FindEdge(Nodes[From], Nodes[To]);
				EXPECT_EQ(bool(Edge), (From + To) % 3 != 0);
				EXPECT_EQ(Graph.FindOutgoingEdge(Nodes[From], Nodes[To]), Edge);
				EXPECT_EQ(Graph.FindIncomingEdge(Nodes[To], Nodes[From]), Edge);
				if (Edge)
				{
					EXPECT_EQ(Graph.TryGetEdge(Edge)->mPayload, int(From * 24 + To));
				}
			}
		}
		EXPECT_EQ(Graph.TopologicalSort().mOrder, Nodes);
		for (const auto Node : Nodes)
		{
			EXPECT_TRUE(Graph.RemoveNode(Node));
		}
		EXPECT_EQ(Graph.GetEdgeCount(), 0u);
		EXPECT_TRUE(Graph.TopologicalSort().IsAcyclic());
	}

	TEST(ArdaDirectedGraph, LongCycleUsesIterativeTraversal)
	{
		TArdaDirectedGraph<int> Graph;
		eastl::vector<FArdaGraphNodeHandle> Nodes;
		for (int Index = 0; Index < 4096; ++Index)
		{
			Nodes.push_back(Graph.AddNode(Index));
			if (Index)
			{
				Graph.AddEdge(Nodes[Index - 1], Nodes[Index]);
			}
		}
		Graph.AddEdge(Nodes.back(), Nodes.front());
		const auto Result = Graph.TopologicalSort();
		ASSERT_EQ(Result.mCycle.size(), Nodes.size() + 1);
		EXPECT_EQ(Result.mCycle.front(), Nodes.front());
		EXPECT_EQ(Result.mCycle.back(), Nodes.front());
		EXPECT_EQ(Graph.GetReachableNodes(Nodes.front()), Nodes);
	}

	TEST(ArdaDirectedGraph, PayloadsCanBeMoveOnlyAndRemainAddressStable)
	{
		TArdaDirectedGraph<eastl::unique_ptr<int>, eastl::unique_ptr<int>> Graph;
		const auto A = Graph.AddNode(eastl::make_unique<int>(1));
		auto* Original = Graph.TryGetNode(A);
		for (int Index = 0; Index < 512; ++Index)
		{
			Graph.AddNode(eastl::make_unique<int>(Index));
		}
		EXPECT_EQ(Graph.TryGetNode(A), Original);
		const auto B = Graph.AddNode(eastl::make_unique<int>(2));
		const auto Edge = Graph.AddEdge(A, B, eastl::make_unique<int>(7)).mHandle;
		EXPECT_EQ(*Graph.TryGetEdge(Edge)->mPayload, 7);
		EXPECT_TRUE(Graph.RemoveNode(A));
		EXPECT_FALSE(Graph.ContainsEdge(Edge));
	}
}
