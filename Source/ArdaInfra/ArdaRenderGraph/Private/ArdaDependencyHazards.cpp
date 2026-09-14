#include "ArdaDependencyHazards.h"

#include <EASTL/map.h>
#include <EASTL/set.h>
#include <EASTL/sort.h>

namespace arda
{
	namespace
	{
		struct FArdaAccessBoundary
		{
			uint64_t mPosition;
			uint32_t mNode;
			int64_t mReads;
			int64_t mWrites;
		};

		struct FArdaActiveAccess
		{
			int64_t mReads = 0;
			int64_t mWrites = 0;
		};

		using FArdaActiveAccesses = eastl::map<uint32_t, FArdaActiveAccess>;

		bool AppendBoundaries(eastl::vector<FArdaAccessBoundary>& Boundaries,
		    uint32_t Node,
		    const FArdaDependencyAccess& Access,
		    const FArdaDependencyResourceDesc& Desc)
		{
			if (Access.mAccess != EArdaDependencyAccess::Read && Access.mAccess != EArdaDependencyAccess::Write &&
			    Access.mAccess != EArdaDependencyAccess::ReadWrite)
			{
				return false;
			}
			const int64_t Reads = Access.mAccess != EArdaDependencyAccess::Write;
			const int64_t Writes = Access.mAccess != EArdaDependencyAccess::Read;
			const auto Append = [&](uint64_t First, uint64_t Count)
			{
				Boundaries.push_back({First, Node, Reads, Writes});
				Boundaries.push_back({First + Count, Node, -Reads, -Writes});
			};
			if (Desc.mExternalAccelerationStructure)
			{
				Append(0, 1);
				return true;
			}
			if (!Desc.mbTexture)
			{
				const auto& Range = Access.mBufferRange;
				if (Range.mByteOffset >= Desc.mBuffer.mByteSize || !Range.mByteSize ||
				    (Range.mByteSize != ArdaRHIWholeBuffer &&
				        Range.mByteSize > Desc.mBuffer.mByteSize - Range.mByteOffset))
				{
					return false;
				}
				Append(Range.mByteOffset, Range.Resolve(Desc.mBuffer).mByteSize);
				return true;
			}

			const auto& Range = Access.mTextureRange;
			const auto& Texture = Desc.mTexture;
			const auto Valid = [](uint32_t Base, uint32_t Count, uint32_t Total)
			{
				return Base < Total && Count && (Count == ArdaRHIAllSubresources || Count <= Total - Base);
			};
			if (!Valid(Range.mBaseMipLevel, Range.mMipLevelCount, Texture.mMipLevels) ||
			    !Valid(Range.mBaseArraySlice, Range.mArraySliceCount, Texture.mArraySize) ||
			    !Valid(Range.mBasePlane, Range.mPlaneCount, GetArdaRHIFormatPlaneCount(Texture.mFormat)))
			{
				return false;
			}
			const auto Resolved = Range.Resolve(Texture);
			for (uint32_t Plane = Resolved.mBasePlane; Plane < Resolved.mBasePlane + Resolved.mPlaneCount; ++Plane)
			{
				for (uint32_t Slice = Resolved.mBaseArraySlice;
				    Slice < Resolved.mBaseArraySlice + Resolved.mArraySliceCount;
				    ++Slice)
				{
					// Mips are consecutive within a slice/plane; no per-texel expansion is needed.
					Append((uint64_t(Plane) * Texture.mArraySize + Slice) * Texture.mMipLevels + Resolved.mBaseMipLevel,
					    Resolved.mMipLevelCount);
				}
			}
			return true;
		}

		void AddEdge(FArdaDependencyTopology& Topology,
		    FArdaGraphNodeHandle Before,
		    FArdaGraphNodeHandle After,
		    bool bValue)
		{
			if (!Before || Before == After)
			{
				return;
			}
			const auto Edge = Topology.AddEdge(Before, After);
			auto& Payload = Topology.TryGetEdge(Edge.mHandle)->mPayload;
			Payload.mbResource |= bValue;
			Payload.mbHazard |= !bValue;
		}

		bool ResolveRead(FArdaDependencyTopology& Topology,
		    const eastl::vector<FArdaGraphNodeHandle>& Nodes,
		    const eastl::set<uint32_t>& Writers,
		    uint32_t ReaderIndex,
		    bool bExternal)
		{
			const auto Reader = Nodes[ReaderIndex];
			if (Writers.size() <= 1)
			{
				// Preserve consumer-first authoring wherever this region has a unique producer.
				const auto Writer = Writers.empty() ? FArdaGraphNodeHandle{} : Nodes[*Writers.begin()];
				AddEdge(Topology, Writer, Reader, true);
				return bExternal || (Writer && Writer != Reader);
			}

			// A read consumes the preceding value and must finish before the following overwrite.
			auto Next = Writers.lower_bound(ReaderIndex);
			const bool bHasPrevious = Next != Writers.begin();
			if (bHasPrevious)
			{
				auto Previous = Next;
				AddEdge(Topology, Nodes[*--Previous], Reader, true);
			}
			if (Next != Writers.end() && *Next == ReaderIndex)
			{
				++Next; // A ReadWrite node consumes its input before producing its own value.
			}
			if (Next != Writers.end())
			{
				AddEdge(Topology, Reader, Nodes[*Next], false);
			}
			return bExternal || bHasPrevious;
		}

		void OrderWriterNeighbors(FArdaDependencyTopology& Topology,
		    const eastl::vector<FArdaGraphNodeHandle>& Nodes,
		    const eastl::set<uint32_t>& Writers,
		    uint32_t Changed)
		{
			auto Next = Writers.lower_bound(Changed);
			if (Next == Writers.end())
			{
				return;
			}
			if (Next != Writers.begin())
			{
				auto Previous = Next;
				AddEdge(Topology, Nodes[*--Previous], Nodes[*Next], false);
			}
			if (*Next == Changed)
			{
				const auto Current = *Next++;
				if (Next != Writers.end())
				{
					AddEdge(Topology, Nodes[Current], Nodes[*Next], false);
				}
			}
		}
	}

	FArdaRHIStatus ResolveArdaDependencyResourceEdges(const FArdaDependencyGraph::FArdaImpl& Graph,
	    FArdaDependencyTopology& Topology,
	    bool bValidate)
	{
		auto Nodes = Graph.mTopology.GetNodes();
		eastl::sort(Nodes.begin(),
		    Nodes.end(),
		    [&](const auto A, const auto B)
		    {
			    const auto Left = Graph.mTopology.TryGetNode(A)->mPayload.mAttachmentOrder;
			    const auto Right = Graph.mTopology.TryGetNode(B)->mPayload.mAttachmentOrder;
			    return Left != Right ? Left < Right : A < B;
		    });
		eastl::vector<eastl::vector<FArdaAccessBoundary>> Resources(Graph.mResources.size());
		for (uint32_t Index = 0; Index < Nodes.size(); ++Index)
		{
			for (const auto& Access : Graph.mTopology.TryGetNode(Nodes[Index])->mPayload.mDesc.mAccesses)
			{
				const bool bValid = Graph.HasResource(Access.mResource) &&
				    AppendBoundaries(Resources[Access.mResource.mIndex],
				        Index,
				        Access,
				        Graph.mResources[Access.mResource.mIndex]);
				if (!bValid && bValidate)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "Invalid dependency resource access range.");
				}
			}
		}
		for (size_t Index = 0; Index < Resources.size(); ++Index)
		{
			auto& Boundaries = Resources[Index];
			eastl::sort(Boundaries.begin(),
			    Boundaries.end(),
			    [](const auto& A, const auto& B)
			    {
				    return A.mPosition < B.mPosition;
			    });
			const auto& Desc = Graph.mResources[Index];
			const bool bExternal = Desc.mExternalBuffer || Desc.mExternalTexture || Desc.mExternalAccelerationStructure;
			FArdaActiveAccesses Active;
			eastl::set<uint32_t> Writers;
			eastl::set<uint32_t> Readers;
			eastl::vector<uint32_t> Changed;
			eastl::vector<uint32_t> ChangedWriters;
			for (size_t Next = 0; Next < Boundaries.size();)
			{
				Changed.clear();
				ChangedWriters.clear();
				const auto Position = Boundaries[Next].mPosition;
				do
				{
					const auto& Event = Boundaries[Next++];
					auto& Counts = Active[Event.mNode];
					Counts.mReads += Event.mReads;
					Counts.mWrites += Event.mWrites;
					Changed.push_back(Event.mNode);
				} while (Next < Boundaries.size() && Boundaries[Next].mPosition == Position);
				if (Next == Boundaries.size())
				{
					break;
				}
				eastl::sort(Changed.begin(), Changed.end());
				Changed.erase(eastl::unique(Changed.begin(), Changed.end()), Changed.end());
				for (const auto Node : Changed)
				{
					const auto Counts = Active[Node];
					const bool bWasWriter = Writers.count(Node) != 0;
					if (bWasWriter != (Counts.mWrites != 0))
					{
						ChangedWriters.push_back(Node);
						if (Counts.mWrites)
						{
							Writers.insert(Node);
						}
						else
						{
							Writers.erase(Node);
						}
					}
					if (Counts.mReads)
					{
						Readers.insert(Node);
					}
					else
					{
						Readers.erase(Node);
					}
					if (!Counts.mReads && !Counts.mWrites)
					{
						Active.erase(Node);
					}
				}

				bool bInitialized = true;
				if (!ChangedWriters.empty())
				{
					for (const auto Writer : ChangedWriters)
					{
						OrderWriterNeighbors(Topology, Nodes, Writers, Writer);
					}
					for (const auto Reader : Readers)
					{
						bInitialized &= ResolveRead(Topology, Nodes, Writers, Reader, bExternal);
					}
				}
				else
				{
					// Nested read ranges must not rescan every other reader at each byte boundary.
					for (const auto Node : Changed)
					{
						if (Readers.count(Node))
						{
							bInitialized &= ResolveRead(Topology, Nodes, Writers, Node, bExternal);
						}
					}
				}
				if (!bInitialized && bValidate)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "A node reads an unproduced range. Repeated writes require a preceding producer in attachment order or external initial contents.");
				}
			}
		}
		return {};
	}
}
