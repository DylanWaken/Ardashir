/** @file ArdaGraph.h
 * Graphics-independent directed graph with stable handles and indexed adjacency.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <EASTL/algorithm.h>
#include <EASTL/atomic.h>
#include <EASTL/heap.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/sort.h>
#include <EASTL/type_traits.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unordered_set.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>

namespace arda
{
	/** Distinguishes node handles from edge handles at compile time. */
	struct FArdaGraphNodeTag;
	/** Distinguishes edge handles from node handles at compile time. */
	struct FArdaGraphEdgeTag;

	/** Stable slot identity. A handle is meaningful only in its graph or an explicit snapshot. */
	template <typename Tag>
	class TArdaGraphHandle
	{
	public:
		/** Constructs an invalid handle. */
		constexpr TArdaGraphHandle() noexcept = default;

		/** Constructs a serialized identity; graph lookup still validates every component. */
		constexpr TArdaGraphHandle(uint32_t Index, uint32_t Generation, uint64_t GraphIdentity) noexcept
		    : mIndex(Index),
		      mGeneration(Generation),
		      mGraphIdentity(GraphIdentity)
		{
		}

		/** Returns the reusable storage slot, not the position in the live-node/edge list. */
		[[nodiscard]] constexpr uint32_t GetIndex() const noexcept
		{
			return mIndex;
		}

		/** Returns the generation that prevents deleted slot identities from being reused. */
		[[nodiscard]] constexpr uint32_t GetGeneration() const noexcept
		{
			return mGeneration;
		}

		/** Returns the owning graph identity, shared only by deliberate edit snapshots. */
		[[nodiscard]] constexpr uint64_t GetGraphIdentity() const noexcept
		{
			return mGraphIdentity;
		}

		/** Tests structural validity; use graph ContainsNode/ContainsEdge to test liveness. */
		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return mIndex != UINT32_MAX && mGeneration != 0 && mGraphIdentity != 0;
		}

		/** Tests structural validity without looking up the graph. */
		explicit constexpr operator bool() const noexcept
		{
			return IsValid();
		}

		/** Compares complete identities. */
		constexpr bool operator==(TArdaGraphHandle Other) const noexcept
		{
			return mIndex == Other.mIndex && mGeneration == Other.mGeneration && mGraphIdentity == Other.mGraphIdentity;
		}

		/** Compares complete identities. */
		constexpr bool operator!=(TArdaGraphHandle Other) const noexcept
		{
			return !(*this == Other);
		}

		/** Orders identities deterministically; nodes of one graph sort by slot then generation. */
		constexpr bool operator<(TArdaGraphHandle Other) const noexcept
		{
			return mGraphIdentity != Other.mGraphIdentity ? mGraphIdentity < Other.mGraphIdentity
			    : mIndex != Other.mIndex                  ? mIndex < Other.mIndex
			                                              : mGeneration < Other.mGeneration;
		}

	private:
		uint32_t mIndex = UINT32_MAX;
		uint32_t mGeneration = 0;
		uint64_t mGraphIdentity = 0;
	};

	/** Stable directed-graph node handle. */
	using FArdaGraphNodeHandle = TArdaGraphHandle<FArdaGraphNodeTag>;
	/** Stable directed-graph edge handle. */
	using FArdaGraphEdgeHandle = TArdaGraphHandle<FArdaGraphEdgeTag>;

	/** Hashes every component of a graph handle; equality resolves hash collisions. */
	template <typename Tag>
	struct TArdaGraphHandleHash
	{
		/** Returns the hash used by graph indexes. */
		size_t operator()(TArdaGraphHandle<Tag> Handle) const noexcept
		{
			uint64_t Value = Handle.GetGraphIdentity() ^ (uint64_t(Handle.GetGeneration()) << 32) ^ Handle.GetIndex();
			Value = (Value ^ (Value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
			Value = (Value ^ (Value >> 27)) * UINT64_C(0x94d049bb133111eb);
			return static_cast<size_t>(Value ^ (Value >> 31));
		}
	};

	/** Default node identity hash. */
	using FArdaGraphNodeHandleHash = TArdaGraphHandleHash<FArdaGraphNodeTag>;
	/** Default edge identity hash. */
	using FArdaGraphEdgeHandleHash = TArdaGraphHandleHash<FArdaGraphEdgeTag>;

	/** Payload for a graph whose edges need no additional data. */
	struct FArdaGraphEmptyPayload
	{
	};

	/** Deterministic Kahn ordering and, when cyclic, a concrete closed cycle witness. */
	struct FArdaGraphTopologicalResult
	{
		/** Ordered acyclic prefix; contains every live node exactly once for a DAG. */
		eastl::vector<FArdaGraphNodeHandle> mOrder;
		/** A closed directed path (first == last), or empty when the graph is acyclic. */
		eastl::vector<FArdaGraphNodeHandle> mCycle;
		/** Sorted nodes withheld by cycles, including nodes downstream from cycles. */
		eastl::vector<FArdaGraphNodeHandle> mBlockedNodes;

		/** Returns whether all nodes were ordered successfully. */
		[[nodiscard]] bool IsAcyclic() const noexcept
		{
			return mBlockedNodes.empty();
		}
	};

	/** Returns a process-local graph identity without depending on a rendering subsystem. */
	inline uint64_t AllocateArdaGraphIdentity() noexcept
	{
		static eastl::atomic<uint64_t> Next{1};
		return Next.fetch_add(1, eastl::memory_order_relaxed);
	}

	/** Mutable simple directed graph; self-edges are allowed and duplicate endpoint pairs are idempotent.
     * Nodes and edges own arbitrary payloads. Hashes index incoming/outgoing neighbors and endpoint pairs;
     * dense live lists and adjacency lists avoid reconstructing topology on every query. Edge removal is
     * constant expected time, node removal is proportional to its incident edges. Removal may reorder dense
     * lists; algorithm outputs use handle order and never depend on hash iteration. Payload addresses remain
     * stable until their record is removed. Mutation requires external synchronization.
     */
	template <typename NodePayload,
	    typename EdgePayload = FArdaGraphEmptyPayload,
	    typename NodeHasher = FArdaGraphNodeHandleHash>
	class TArdaDirectedGraph
	{
		struct FLineage
		{
			eastl::atomic<uint64_t> mNextNodeGeneration{1}, mNextEdgeGeneration{1};
		};

	public:
		/** Node data; topology is changed only through the graph's mutation methods. */
		struct FNode
		{
			/** Application-defined data owned by the node. */
			NodePayload mPayload;

			/** Constructs the payload in place. */
			template <typename... Args>
			explicit FNode(Args&&... Arguments)
			    : mPayload(eastl::forward<Args>(Arguments)...)
			{
			}

		private:
			friend class TArdaDirectedGraph;
			eastl::vector<FArdaGraphEdgeHandle> mIncomingEdges, mOutgoingEdges;
			eastl::unordered_map<FArdaGraphNodeHandle, FArdaGraphEdgeHandle, NodeHasher> mIncoming, mOutgoing;
		};

		/** Edge data with immutable endpoints. */
		struct FEdge
		{
			/** Source node identity. */
			const FArdaGraphNodeHandle mFrom;
			/** Destination node identity. */
			const FArdaGraphNodeHandle mTo;
			/** Application-defined data owned by the edge. */
			EdgePayload mPayload;

			/** Constructs an edge with its payload. */
			FEdge(FArdaGraphNodeHandle From, FArdaGraphNodeHandle To, EdgePayload Payload)
			    : mFrom(From),
			      mTo(To),
			      mPayload(eastl::move(Payload))
			{
			}

		private:
			friend class TArdaDirectedGraph;
			size_t mIncomingIndex = 0, mOutgoingIndex = 0;
		};

		/** Reports whether an endpoint pair created a new edge or reused an existing edge. */
		struct FEdgeInsertion
		{
			/** Existing/new handle, or invalid when either endpoint is stale or foreign. */
			FArdaGraphEdgeHandle mHandle;
			/** True only when this operation created the edge. */
			bool mbInserted = false;
		};

		/** Creates an empty graph with a fresh identity. */
		TArdaDirectedGraph()
		    : mIdentity(AllocateArdaGraphIdentity()),
		      mLineage(eastl::make_shared<FLineage>())
		{
		}

		/** Ordinary copying is disabled; use CloneSnapshot for deliberate identity sharing. */
		TArdaDirectedGraph(const TArdaDirectedGraph&) = delete;
		/** Ordinary copying is disabled; commit a snapshot by moving it into the graph. */
		TArdaDirectedGraph& operator=(const TArdaDirectedGraph&) = delete;

		/** Transfers all handles and leaves the source empty with a new graph identity. */
		TArdaDirectedGraph(TArdaDirectedGraph&& Other) noexcept
		    : TArdaDirectedGraph()
		{
			Swap(Other);
		}

		/** Transfers all handles and invalidates the destination's previous identities. */
		TArdaDirectedGraph& operator=(TArdaDirectedGraph&& Other) noexcept
		{
			if (this != &Other)
			{
				TArdaDirectedGraph Incoming(eastl::move(Other));
				Swap(Incoming);
			}
			return *this;
		}

		/** Exchanges complete graph identities, topology, and payload ownership. */
		void Swap(TArdaDirectedGraph& Other) noexcept
		{
			eastl::swap(mIdentity, Other.mIdentity);
			mLineage.swap(Other.mLineage);
			mNodeSlots.swap(Other.mNodeSlots);
			mEdgeSlots.swap(Other.mEdgeSlots);
			mFreeNodes.swap(Other.mFreeNodes);
			mFreeEdges.swap(Other.mFreeEdges);
			mNodes.swap(Other.mNodes);
			mEdges.swap(Other.mEdges);
			mPairs.swap(Other.mPairs);
		}

		/** Deep-copies an edit snapshot while deliberately preserving graph identity and handles.
         * Existing handles are accepted by both snapshots until edits invalidate them independently.
         * New identities consume generation numbers shared by the lineage, so abandoning an edit can
         * never revive its node or edge handles during a later edit. Generation exhaustion rejects insertion.
         * Payloads must be copy-constructible. Move the edited snapshot back to commit it atomically at
         * the caller's synchronization boundary; abandoning the snapshot leaves the original unchanged.
         */
		[[nodiscard]] TArdaDirectedGraph CloneSnapshot() const
		{
			static_assert(eastl::is_copy_constructible<NodePayload>::value &&
			        eastl::is_copy_constructible<EdgePayload>::value,
			    "Graph snapshots require copyable node and edge payloads.");
			TArdaDirectedGraph Copy;
			Copy.mIdentity = mIdentity;
			Copy.mLineage = mLineage;
			Copy.mNodes = mNodes;
			Copy.mEdges = mEdges;
			Copy.mPairs = mPairs;
			Copy.mFreeNodes = mFreeNodes;
			Copy.mFreeEdges = mFreeEdges;
			Copy.mNodeSlots.resize(mNodeSlots.size());
			Copy.mEdgeSlots.resize(mEdgeSlots.size());
			for (size_t Index = 0; Index < mNodeSlots.size(); ++Index)
			{
				const auto& Source = mNodeSlots[Index];
				auto& Destination = Copy.mNodeSlots[Index];
				Destination.mGeneration = Source.mGeneration;
				Destination.mLiveIndex = Source.mLiveIndex;
				if (Source.mValue)
				{
					Destination.mValue = eastl::make_unique<FNode>(static_cast<const FNode&>(*Source.mValue));
				}
			}
			for (size_t Index = 0; Index < mEdgeSlots.size(); ++Index)
			{
				const auto& Source = mEdgeSlots[Index];
				auto& Destination = Copy.mEdgeSlots[Index];
				Destination.mGeneration = Source.mGeneration;
				Destination.mLiveIndex = Source.mLiveIndex;
				if (Source.mValue)
				{
					Destination.mValue = eastl::make_unique<FEdge>(*Source.mValue);
				}
			}
			return Copy;
		}

		/** Returns the graph identity retained by all its live handles. */
		[[nodiscard]] uint64_t GetGraphIdentity() const noexcept
		{
			return mIdentity;
		}

		/** Returns the number of live nodes. */
		[[nodiscard]] size_t GetNodeCount() const noexcept
		{
			return mNodes.size();
		}

		/** Returns the number of live directed endpoint pairs. */
		[[nodiscard]] size_t GetEdgeCount() const noexcept
		{
			return mEdges.size();
		}

		/** Returns the maintained dense live-node list; deletion can reorder it. */
		[[nodiscard]] const eastl::vector<FArdaGraphNodeHandle>& GetNodes() const noexcept
		{
			return mNodes;
		}

		/** Returns the maintained dense live-edge list; deletion can reorder it. */
		[[nodiscard]] const eastl::vector<FArdaGraphEdgeHandle>& GetEdges() const noexcept
		{
			return mEdges;
		}

		/** Looks up a live local node without accepting stale or foreign handles. */
		[[nodiscard]] FNode* TryGetNode(FArdaGraphNodeHandle Handle) noexcept
		{
			return ContainsNode(Handle) ? mNodeSlots[Handle.GetIndex()].mValue.get() : nullptr;
		}

		/** Looks up a live local node without accepting stale or foreign handles. */
		[[nodiscard]] const FNode* TryGetNode(FArdaGraphNodeHandle Handle) const noexcept
		{
			return ContainsNode(Handle) ? mNodeSlots[Handle.GetIndex()].mValue.get() : nullptr;
		}

		/** Looks up a live local edge without accepting stale or foreign handles. */
		[[nodiscard]] FEdge* TryGetEdge(FArdaGraphEdgeHandle Handle) noexcept
		{
			return ContainsEdge(Handle) ? mEdgeSlots[Handle.GetIndex()].mValue.get() : nullptr;
		}

		/** Looks up a live local edge without accepting stale or foreign handles. */
		[[nodiscard]] const FEdge* TryGetEdge(FArdaGraphEdgeHandle Handle) const noexcept
		{
			return ContainsEdge(Handle) ? mEdgeSlots[Handle.GetIndex()].mValue.get() : nullptr;
		}

		/** Tests a complete node identity and liveness. */
		[[nodiscard]] bool ContainsNode(FArdaGraphNodeHandle Handle) const noexcept
		{
			return Handle.GetGraphIdentity() == mIdentity && Handle.GetIndex() < mNodeSlots.size() &&
			    mNodeSlots[Handle.GetIndex()].mGeneration == Handle.GetGeneration() &&
			    bool(mNodeSlots[Handle.GetIndex()].mValue);
		}

		/** Tests a complete edge identity and liveness. */
		[[nodiscard]] bool ContainsEdge(FArdaGraphEdgeHandle Handle) const noexcept
		{
			return Handle.GetGraphIdentity() == mIdentity && Handle.GetIndex() < mEdgeSlots.size() &&
			    mEdgeSlots[Handle.GetIndex()].mGeneration == Handle.GetGeneration() &&
			    bool(mEdgeSlots[Handle.GetIndex()].mValue);
		}

		/** Constructs a payload; returns invalid when slot indices or lineage generations are exhausted. */
		template <typename... Args>
		FArdaGraphNodeHandle EmplaceNode(Args&&... Arguments)
		{
			if (mFreeNodes.empty() && mNodeSlots.size() >= UINT32_MAX)
			{
				return {};
			}
			const uint32_t Generation = AcquireGeneration(mLineage->mNextNodeGeneration);
			if (!Generation)
			{
				return {};
			}
			auto Value = eastl::make_unique<FNode>(eastl::forward<Args>(Arguments)...);
			const uint32_t Index = AcquireSlot(mNodeSlots, mFreeNodes);
			auto& Slot = mNodeSlots[Index];
			Slot.mGeneration = Generation;
			Slot.mValue = eastl::move(Value);
			Slot.mLiveIndex = mNodes.size();
			const FArdaGraphNodeHandle Handle{Index, Slot.mGeneration, mIdentity};
			mNodes.push_back(Handle);
			return Handle;
		}

		/** Adds one owned payload and returns its stable node handle. */
		FArdaGraphNodeHandle AddNode(NodePayload Payload)
		{
			return EmplaceNode(eastl::move(Payload));
		}

		/** Finds an endpoint pair using its hash index, without creating a missing edge. */
		[[nodiscard]] FArdaGraphEdgeHandle FindEdge(FArdaGraphNodeHandle From, FArdaGraphNodeHandle To) const
		{
			if (!ContainsNode(From) || !ContainsNode(To))
			{
				return {};
			}
			const auto Found = mPairs.find({From, To});
			return Found == mPairs.end() ? FArdaGraphEdgeHandle{} : Found->second;
		}

		/** Finds an incoming edge through the destination's neighbor index. */
		[[nodiscard]] FArdaGraphEdgeHandle FindIncomingEdge(FArdaGraphNodeHandle Node, FArdaGraphNodeHandle From) const
		{
			const auto* Record = TryGetNode(Node);
			if (!Record)
			{
				return {};
			}
			const auto Found = Record->mIncoming.find(From);
			return Found == Record->mIncoming.end() ? FArdaGraphEdgeHandle{} : Found->second;
		}

		/** Finds an outgoing edge through the source's neighbor index. */
		[[nodiscard]] FArdaGraphEdgeHandle FindOutgoingEdge(FArdaGraphNodeHandle Node, FArdaGraphNodeHandle To) const
		{
			const auto* Record = TryGetNode(Node);
			if (!Record)
			{
				return {};
			}
			const auto Found = Record->mOutgoing.find(To);
			return Found == Record->mOutgoing.end() ? FArdaGraphEdgeHandle{} : Found->second;
		}

		/** Returns maintained incoming edge handles, or an empty list for a stale/foreign node. */
		[[nodiscard]] const eastl::vector<FArdaGraphEdgeHandle>& GetIncomingEdges(
		    FArdaGraphNodeHandle Node) const noexcept
		{
			const auto* Record = TryGetNode(Node);
			return Record ? Record->mIncomingEdges : EmptyEdges();
		}

		/** Returns maintained outgoing edge handles, or an empty list for a stale/foreign node. */
		[[nodiscard]] const eastl::vector<FArdaGraphEdgeHandle>& GetOutgoingEdges(
		    FArdaGraphNodeHandle Node) const noexcept
		{
			const auto* Record = TryGetNode(Node);
			return Record ? Record->mOutgoingEdges : EmptyEdges();
		}

		/** Adds an edge or returns the existing pair unchanged; invalid endpoints never mutate the graph. */
		FEdgeInsertion AddEdge(FArdaGraphNodeHandle From, FArdaGraphNodeHandle To, EdgePayload Payload = {})
		{
			if (!ContainsNode(From) || !ContainsNode(To))
			{
				return {};
			}
			if (const auto Existing = FindEdge(From, To))
			{
				return {Existing, false};
			}
			if (mFreeEdges.empty() && mEdgeSlots.size() >= UINT32_MAX)
			{
				return {};
			}
			const uint32_t Generation = AcquireGeneration(mLineage->mNextEdgeGeneration);
			if (!Generation)
			{
				return {};
			}
			auto Value = eastl::make_unique<FEdge>(From, To, eastl::move(Payload));
			const uint32_t Index = AcquireSlot(mEdgeSlots, mFreeEdges);
			auto& Slot = mEdgeSlots[Index];
			Slot.mGeneration = Generation;
			Slot.mValue = eastl::move(Value);
			Slot.mLiveIndex = mEdges.size();
			const FArdaGraphEdgeHandle Handle{Index, Slot.mGeneration, mIdentity};
			auto& Source = *mNodeSlots[From.GetIndex()].mValue;
			auto& Destination = *mNodeSlots[To.GetIndex()].mValue;
			Slot.mValue->mOutgoingIndex = Source.mOutgoingEdges.size();
			Slot.mValue->mIncomingIndex = Destination.mIncomingEdges.size();
			Source.mOutgoingEdges.push_back(Handle);
			Destination.mIncomingEdges.push_back(Handle);
			Source.mOutgoing.emplace(To, Handle);
			Destination.mIncoming.emplace(From, Handle);
			mPairs.emplace(FEdgeKey{From, To}, Handle);
			mEdges.push_back(Handle);
			return {Handle, true};
		}

		/** Removes one live edge and all three hash indexes in expected constant time. */
		bool RemoveEdge(FArdaGraphEdgeHandle Handle)
		{
			auto* Edge = TryGetEdge(Handle);
			if (!Edge)
			{
				return false;
			}
			auto& Source = *mNodeSlots[Edge->mFrom.GetIndex()].mValue;
			auto& Destination = *mNodeSlots[Edge->mTo.GetIndex()].mValue;
			const auto LastOutgoing = Source.mOutgoingEdges.back();
			Source.mOutgoingEdges[Edge->mOutgoingIndex] = LastOutgoing;
			mEdgeSlots[LastOutgoing.GetIndex()].mValue->mOutgoingIndex = Edge->mOutgoingIndex;
			Source.mOutgoingEdges.pop_back();
			Source.mOutgoing.erase(Edge->mTo);
			const auto LastIncoming = Destination.mIncomingEdges.back();
			Destination.mIncomingEdges[Edge->mIncomingIndex] = LastIncoming;
			mEdgeSlots[LastIncoming.GetIndex()].mValue->mIncomingIndex = Edge->mIncomingIndex;
			Destination.mIncomingEdges.pop_back();
			Destination.mIncoming.erase(Edge->mFrom);
			mPairs.erase(FEdgeKey{Edge->mFrom, Edge->mTo});
			auto& Slot = mEdgeSlots[Handle.GetIndex()];
			const auto Last = mEdges.back();
			mEdges[Slot.mLiveIndex] = Last;
			mEdgeSlots[Last.GetIndex()].mLiveIndex = Slot.mLiveIndex;
			mEdges.pop_back();
			ReleaseSlot(Slot, Handle.GetIndex(), mFreeEdges);
			return true;
		}

		/** Removes a node and every incident edge; stale/foreign handles are harmless. */
		bool RemoveNode(FArdaGraphNodeHandle Handle)
		{
			auto* Node = TryGetNode(Handle);
			if (!Node)
			{
				return false;
			}
			while (!Node->mOutgoingEdges.empty())
			{
				RemoveEdge(Node->mOutgoingEdges.back());
			}
			while (!Node->mIncomingEdges.empty())
			{
				RemoveEdge(Node->mIncomingEdges.back());
			}
			auto& Slot = mNodeSlots[Handle.GetIndex()];
			const auto Last = mNodes.back();
			mNodes[Slot.mLiveIndex] = Last;
			mNodeSlots[Last.GetIndex()].mLiveIndex = Slot.mLiveIndex;
			mNodes.pop_back();
			ReleaseSlot(Slot, Handle.GetIndex(), mFreeNodes);
			return true;
		}

		/** Removes all records while retaining slot generations so old handles cannot become live again. */
		void Clear()
		{
			while (!mNodes.empty())
			{
				RemoveNode(mNodes.back());
			}
		}

		/** Returns reachable nodes including Start, sorted by handle; an invalid Start produces an empty list. */
		[[nodiscard]] eastl::vector<FArdaGraphNodeHandle> GetReachableNodes(FArdaGraphNodeHandle Start) const
		{
			eastl::vector<FArdaGraphNodeHandle> Found;
			if (!ContainsNode(Start))
			{
				return Found;
			}
			eastl::unordered_set<FArdaGraphNodeHandle, NodeHasher> Visited;
			Visited.insert(Start);
			Found.push_back(Start);
			for (size_t Index = 0; Index < Found.size(); ++Index)
			{
				for (const auto Edge : GetOutgoingEdges(Found[Index]))
				{
					const auto To = TryGetEdge(Edge)->mTo;
					if (Visited.insert(To).second)
					{
						Found.push_back(To);
					}
				}
			}
			eastl::sort(Found.begin(), Found.end());
			return Found;
		}

		/** Tests directed reachability, including a zero-edge path from a live node to itself. */
		[[nodiscard]] bool IsReachable(FArdaGraphNodeHandle From, FArdaGraphNodeHandle To) const
		{
			if (!ContainsNode(From) || !ContainsNode(To))
			{
				return false;
			}
			eastl::vector<FArdaGraphNodeHandle> Pending{From};
			eastl::unordered_set<FArdaGraphNodeHandle, NodeHasher> Visited;
			Visited.insert(From);
			for (size_t Index = 0; Index < Pending.size(); ++Index)
			{
				if (Pending[Index] == To)
				{
					return true;
				}
				for (const auto Edge : GetOutgoingEdges(Pending[Index]))
				{
					const auto Next = TryGetEdge(Edge)->mTo;
					if (Visited.insert(Next).second)
					{
						Pending.push_back(Next);
					}
				}
			}
			return false;
		}

		/** Orders all nodes in O(E + V log V), breaking independent-node ties by handle.
         * Cycles return a sorted blocked set and an iterative-DFS closed witness without recursion.
         */
		[[nodiscard]] FArdaGraphTopologicalResult TopologicalSort() const
		{
			FArdaGraphTopologicalResult Result;
			eastl::unordered_map<FArdaGraphNodeHandle, size_t, NodeHasher> Remaining;
			eastl::vector<FArdaGraphNodeHandle> Ready;
			const auto Later = [](auto Left, auto Right)
			{
				return Right < Left;
			};
			for (const auto Node : mNodes)
			{
				const size_t Count = GetIncomingEdges(Node).size();
				Remaining.emplace(Node, Count);
				if (!Count)
				{
					Ready.push_back(Node);
				}
			}
			eastl::make_heap(Ready.begin(), Ready.end(), Later);
			while (!Ready.empty())
			{
				eastl::pop_heap(Ready.begin(), Ready.end(), Later);
				const auto Node = Ready.back();
				Ready.pop_back();
				Result.mOrder.push_back(Node);
				for (const auto Edge : GetOutgoingEdges(Node))
				{
					const auto To = TryGetEdge(Edge)->mTo;
					if (--Remaining.find(To)->second == 0)
					{
						Ready.push_back(To);
						eastl::push_heap(Ready.begin(), Ready.end(), Later);
					}
				}
			}
			if (Result.mOrder.size() != mNodes.size())
			{
				for (const auto Node : mNodes)
				{
					if (Remaining.find(Node)->second)
					{
						Result.mBlockedNodes.push_back(Node);
					}
				}
				eastl::sort(Result.mBlockedNodes.begin(), Result.mBlockedNodes.end());
				Result.mCycle = FindCycle(Result.mBlockedNodes);
			}
			return Result;
		}

	private:
		static uint32_t AcquireGeneration(eastl::atomic<uint64_t>& Counter) noexcept
		{
			auto Next = Counter.load(eastl::memory_order_relaxed);
			while (Next <= UINT32_MAX)
			{
				if (Counter.compare_exchange_weak(Next, Next + 1, eastl::memory_order_relaxed))
				{
					return static_cast<uint32_t>(Next);
				}
			}
			return 0;
		}

		template <typename Record>
		struct TSlot
		{
			eastl::unique_ptr<Record> mValue;
			uint32_t mGeneration = 1;
			size_t mLiveIndex = 0;
		};

		struct FEdgeKey
		{
			FArdaGraphNodeHandle mFrom, mTo;

			bool operator==(const FEdgeKey& Other) const noexcept
			{
				return mFrom == Other.mFrom && mTo == Other.mTo;
			}
		};

		struct FEdgeKeyHash
		{
			size_t operator()(const FEdgeKey& Key) const
			{
				const size_t From = NodeHasher{}(Key.mFrom), To = NodeHasher{}(Key.mTo);
				return From ^ (To + size_t(0x9e3779b9u) + (From << 6) + (From >> 2));
			}
		};

		template <typename Record>
		static uint32_t AcquireSlot(eastl::vector<TSlot<Record>>& Slots, eastl::vector<uint32_t>& Free)
		{
			if (!Free.empty())
			{
				const auto Index = Free.back();
				Free.pop_back();
				return Index;
			}
			Slots.emplace_back();
			return static_cast<uint32_t>(Slots.size() - 1);
		}

		template <typename Record>
		static void ReleaseSlot(TSlot<Record>& Slot, uint32_t Index, eastl::vector<uint32_t>& Free)
		{
			Slot.mValue.reset();
			// Retire an exhausted generation permanently instead of reviving an ancient handle.
			if (Slot.mGeneration != UINT32_MAX)
			{
				++Slot.mGeneration;
				Free.push_back(Index);
			}
		}

		static const eastl::vector<FArdaGraphEdgeHandle>& EmptyEdges() noexcept
		{
			static const eastl::vector<FArdaGraphEdgeHandle> Empty;
			return Empty;
		}

		eastl::vector<FArdaGraphNodeHandle> FindCycle(const eastl::vector<FArdaGraphNodeHandle>& Blocked) const
		{
			struct FFrame
			{
				FArdaGraphNodeHandle mNode;
				eastl::vector<FArdaGraphNodeHandle> mNext;
				size_t mIndex = 0;
			};

			eastl::unordered_map<FArdaGraphNodeHandle, uint8_t, NodeHasher> Color;
			eastl::vector<FFrame> Stack;
			const auto Push = [&](FArdaGraphNodeHandle Node)
			{
				FFrame Frame;
				Frame.mNode = Node;
				Color[Node] = 1;
				for (const auto Edge : GetOutgoingEdges(Node))
				{
					Frame.mNext.push_back(TryGetEdge(Edge)->mTo);
				}
				eastl::sort(Frame.mNext.begin(), Frame.mNext.end());
				Stack.push_back(eastl::move(Frame));
			};
			for (const auto Start : Blocked)
			{
				if (Color[Start])
				{
					continue;
				}
				Push(Start);
				while (!Stack.empty())
				{
					auto& Frame = Stack.back();
					if (Frame.mIndex == Frame.mNext.size())
					{
						Color[Frame.mNode] = 2;
						Stack.pop_back();
						continue;
					}
					const auto Next = Frame.mNext[Frame.mIndex++];
					if (Color[Next] == 0)
					{
						Push(Next);
					}
					else if (Color[Next] == 1)
					{
						eastl::vector<FArdaGraphNodeHandle> Cycle;
						bool bInside = false;
						for (const auto& Entry : Stack)
						{
							bInside |= Entry.mNode == Next;
							if (bInside)
							{
								Cycle.push_back(Entry.mNode);
							}
						}
						Cycle.push_back(Next);
						return Cycle;
					}
				}
			}
			return {};
		}

		uint64_t mIdentity;
		eastl::shared_ptr<FLineage> mLineage;
		eastl::vector<TSlot<FNode>> mNodeSlots;
		eastl::vector<TSlot<FEdge>> mEdgeSlots;
		eastl::vector<uint32_t> mFreeNodes, mFreeEdges;
		eastl::vector<FArdaGraphNodeHandle> mNodes;
		eastl::vector<FArdaGraphEdgeHandle> mEdges;
		eastl::unordered_map<FEdgeKey, FArdaGraphEdgeHandle, FEdgeKeyHash> mPairs;
	};
}
