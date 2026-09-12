#include "ArdaDependencyGraphInternal.h"
#include "ArdaInductorAdaptiveSchedule.h"
#include <cmath>
#include <EASTL/sort.h>
#include <EASTL/unordered_set.h>
#include <limits>

namespace arda
{
	namespace
	{
		using FHandle = FArdaGraphNodeHandle;

		FArdaRHIStatus Error(const char* Text)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Text);
		}

		bool Writes(const FArdaDependencyAccess& A)
		{
			return A.mAccess != EArdaDependencyAccess::Read;
		}

		bool Reads(const FArdaDependencyAccess& A)
		{
			return A.mAccess != EArdaDependencyAccess::Write;
		}

		bool IsDeclaration(const FArdaDependencyNode& Node)
		{
			return Node.mDesc.mbPipelineStageOnly ||
			    (Node.mDefinition->mKind == EArdaDependencyNodeKind::Synchronization && !Node.mDefinition->mRecord &&
			        Node.mDesc.mAccesses.empty());
		}

		bool AddBytes(uint64_t& A, uint64_t B)
		{
			if (B > UINT64_MAX - A)
			{
				return false;
			}
			A += B;
			return true;
		}

		bool StrictTextureRange(const FArdaRHITextureSubresourceRange& Range, const FArdaRHITextureDesc& Desc)
		{
			const auto Valid = [](uint32_t Base, uint32_t Count, uint32_t Total)
			{
				return Base < Total && Count && (Count == ArdaRHIAllSubresources || Count <= Total - Base);
			};
			return Valid(Range.mBaseMipLevel, Range.mMipLevelCount, Desc.mMipLevels) &&
			    Valid(Range.mBaseArraySlice, Range.mArraySliceCount, Desc.mArraySize) &&
			    Valid(Range.mBasePlane, Range.mPlaneCount, GetArdaRHIFormatPlaneCount(Desc.mFormat));
		}

		bool ContainsTextureRange(const FArdaRHITextureSubresourceRange& Outer,
		    const FArdaRHITextureSubresourceRange& Inner)
		{
			const auto Contains = [](uint32_t Base, uint32_t Count, uint32_t InnerBase, uint32_t InnerCount)
			{
				return InnerBase >= Base && InnerBase - Base <= Count && InnerCount <= Count - (InnerBase - Base);
			};
			return Contains(Outer.mBaseMipLevel, Outer.mMipLevelCount, Inner.mBaseMipLevel, Inner.mMipLevelCount) &&
			    Contains(Outer.mBaseArraySlice,
			        Outer.mArraySliceCount,
			        Inner.mBaseArraySlice,
			        Inner.mArraySliceCount) &&
			    Contains(Outer.mBasePlane, Outer.mPlaneCount, Inner.mBasePlane, Inner.mPlaneCount);
		}

		void MergeBufferWrites(eastl::vector<FArdaRHIBufferRange>& Ranges)
		{
			eastl::sort(Ranges.begin(),
			    Ranges.end(),
			    [](const auto& A, const auto& B)
			    {
				    return A.mByteOffset < B.mByteOffset;
			    });
			size_t Count = 0;
			for (const auto Range : Ranges)
			{
				if (Count && Range.mByteOffset <= Ranges[Count - 1].mByteOffset + Ranges[Count - 1].mByteSize)
				{
					auto& Last = Ranges[Count - 1];
					Last.mByteSize =
					    eastl::max(Last.mByteOffset + Last.mByteSize, Range.mByteOffset + Range.mByteSize) -
					    Last.mByteOffset;
				}
				else
				{
					Ranges[Count++] = Range;
				}
			}
			Ranges.resize(Count);
		}

		bool CoversBufferRead(const eastl::vector<FArdaRHIBufferRange>& Writes, FArdaRHIBufferRange Read)
		{
			const auto After = eastl::upper_bound(Writes.begin(),
			    Writes.end(),
			    Read.mByteOffset,
			    [](uint64_t Offset, const auto& Range)
			    {
				    return Offset < Range.mByteOffset;
			    });
			if (After == Writes.begin())
			{
				return false;
			}
			const auto& Produced = *(After - 1);
			return Read.mByteOffset + Read.mByteSize <= Produced.mByteOffset + Produced.mByteSize;
		}

		FArdaRHIStatus ResolveDependencies(FArdaDependencyGraph::FImpl& G)
		{
			auto Edges = G.mTopology.GetEdges();
			for (auto E : Edges)
			{
				G.mTopology.RemoveEdge(E);
			}
			for (auto E : G.mManualEdges)
			{
				(void)G.mTopology.AddEdge(E.first, E.second, {});
			}
			eastl::vector<FHandle> Writers(G.mResources.size());
			eastl::vector<eastl::vector<FArdaRHIBufferRange>> BufferWrites(G.mResources.size());
			eastl::vector<eastl::vector<FArdaRHITextureSubresourceRange>> TextureWrites(G.mResources.size());
			for (auto H : G.mTopology.GetNodes())
			{
				const auto& Node = G.mTopology.TryGetNode(H)->mPayload;
				if (Node.mDesc.mbPipelineStageOnly &&
				    (!Node.mDesc.mAccesses.empty() || !Node.mDesc.mColorTargets.empty() || Node.mDesc.mDepthTarget ||
				        !Node.mDesc.mPipelines.empty()))
				{
					return Error(
					    "Stage-only nodes may contribute shaders but cannot access physical resources or request pipelines.");
				}
				for (const auto& A : Node.mDesc.mAccesses)
				{
					if (!G.HasResource(A.mResource))
					{
						return Error("Node references a foreign or missing resource version.");
					}
					const auto& D = G.mResources[A.mResource.mIndex];
					if (A.mAccess != EArdaDependencyAccess::Read && A.mAccess != EArdaDependencyAccess::Write &&
					    A.mAccess != EArdaDependencyAccess::ReadWrite)
					{
						return Error("Node resource access has an invalid direction.");
					}
					if (A.mState == EArdaRHIResourceState::Unknown)
					{
						return Error("Node resource access requires an explicit RHI state.");
					}
					if (D.mExternalAccelerationStructure)
					{
						if ((A.mState != EArdaRHIResourceState::AccelStructRead &&
						        A.mState != EArdaRHIResourceState::AccelStructWrite) ||
						    (Writes(A) != (A.mState == EArdaRHIResourceState::AccelStructWrite)) ||
						    A.mBufferRange.mByteOffset || A.mBufferRange.mByteSize != ArdaRHIWholeBuffer ||
						    Node.mDefinition->mKind == EArdaDependencyNodeKind::Copy ||
						    Node.mDefinition->mKind == EArdaDependencyNodeKind::Cuda)
						{
							return Error(
							    "Acceleration structure access requires a whole-object AS read or write on a graphics/compute node.");
						}
					}
					else if (!D.mbTexture)
					{
						if (A.mBufferRange.mByteOffset >= D.mBuffer.mByteSize || !A.mBufferRange.mByteSize ||
						    (A.mBufferRange.mByteSize != ArdaRHIWholeBuffer &&
						        A.mBufferRange.mByteSize > D.mBuffer.mByteSize - A.mBufferRange.mByteOffset))
						{
							return Error("Node buffer range exceeds its logical resource.");
						}
					}
					else
					{
						if (!StrictTextureRange(A.mTextureRange, D.mTexture))
						{
							return Error("Node texture range is empty or exceeds its logical resource.");
						}
					}
					if (Writes(A))
					{
						auto& W = Writers[A.mResource.mIndex];
						if (W && W != H)
						{
							return Error(
							    "A logical resource version has multiple producers. Declare a separate output version for each node.");
						}
						W = H;
						if (D.mbTexture)
						{
							TextureWrites[A.mResource.mIndex].push_back(A.mTextureRange.Resolve(D.mTexture));
						}
						else if (!D.mExternalAccelerationStructure)
						{
							BufferWrites[A.mResource.mIndex].push_back(A.mBufferRange.Resolve(D.mBuffer));
						}
					}
				}
				const auto HasFramebufferAccess = [&](FArdaDependencyResourceHandle Resource, bool Depth)
				{
					if (!G.HasResource(Resource) || !G.mResources[Resource.mIndex].mbTexture)
					{
						return false;
					}
					const auto& Desc = G.mResources[Resource.mIndex].mTexture;
					if (Depth ? !HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::DepthStencil)
					          : !HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::RenderTarget))
					{
						return false;
					}
					return eastl::any_of(Node.mDesc.mAccesses.begin(),
					    Node.mDesc.mAccesses.end(),
					    [&](const auto& Access)
					    {
						    const bool State = Depth
						        ? ((Writes(Access) && Access.mState == EArdaRHIResourceState::DepthWrite) ||
						              (!Writes(Access) && Access.mState == EArdaRHIResourceState::DepthRead))
						        : (Writes(Access) && Access.mState == EArdaRHIResourceState::RenderTarget);
						    return Access.mResource == Resource && State;
					    });
				};
				for (auto R : Node.mDesc.mColorTargets)
				{
					if (!HasFramebufferAccess(R, false))
					{
						return Error(
						    "Pipeline color targets require a declared RenderTarget write selecting the attachment subresources.");
					}
				}
				if (Node.mDesc.mDepthTarget && !HasFramebufferAccess(Node.mDesc.mDepthTarget, true))
				{
					return Error(
					    "Pipeline depth targets require a declared depth access selecting the attachment subresources.");
				}
			}
			for (auto& Ranges : BufferWrites)
			{
				MergeBufferWrites(Ranges);
			}
			for (auto H : G.mTopology.GetNodes())
			{
				for (const auto& A : G.mTopology.TryGetNode(H)->mPayload.mDesc.mAccesses)
				{
					if (!Reads(A))
					{
						continue;
					}
					const auto& D = G.mResources[A.mResource.mIndex];
					auto W = Writers[A.mResource.mIndex];
					if (!W || W == H)
					{
						if (!D.mExternalBuffer && !D.mExternalTexture && !D.mExternalAccelerationStructure)
						{
							return Error(
							    "A node reads an unproduced resource version. Inputs must have a producer or external storage.");
						}
						continue;
					}
					if (!D.mExternalBuffer && !D.mExternalTexture && !D.mExternalAccelerationStructure)
					{
						const auto Index = A.mResource.mIndex;
						const bool Covered = D.mbTexture
						    ? eastl::any_of(TextureWrites[Index].begin(),
						          TextureWrites[Index].end(),
						          [&](const auto& Range)
						          {
							          return ContainsTextureRange(Range, A.mTextureRange.Resolve(D.mTexture));
						          })
						    : CoversBufferRead(BufferWrites[Index], A.mBufferRange.Resolve(D.mBuffer));
						if (!Covered)
						{
							return Error(
							    "A node reads bytes or texture subresources outside its producer's declared writes.");
						}
					}
					auto E = G.mTopology.AddEdge(W, H, {true});
					G.mTopology.TryGetEdge(E.mHandle)->mPayload.mbResource = true;
				}
			}
			if (!G.mTopology.TopologicalSort().IsAcyclic())
			{
				return Error("Dependency cycle: resource values and explicit synchronization must form a DAG.");
			}
			return {};
		}

		eastl::vector<FHandle> LiveNodes(FArdaDependencyGraph::FImpl& G, FArdaInductorCompileResult& Result)
		{
			eastl::unordered_set<uint32_t> Live;
			eastl::vector<FHandle> Stack;
			for (auto H : G.mTopology.GetNodes())
			{
				const auto& N = G.mTopology.TryGetNode(H)->mPayload;
				bool Root = N.mDesc.mbSideEffect;
				for (const auto& A : N.mDesc.mAccesses)
				{
					const auto& R = G.mResources[A.mResource.mIndex];
					Root |= Writes(A) &&
					    (R.mbOutput || R.mbPersistent || R.mExternalBuffer || R.mExternalTexture ||
					        R.mExternalAccelerationStructure);
				}
				if (Root)
				{
					Stack.push_back(H);
				}
			}
			while (!Stack.empty())
			{
				auto H = Stack.back();
				Stack.pop_back();
				if (!Live.insert(H.GetIndex()).second)
				{
					continue;
				}
				for (auto E : G.mTopology.GetIncomingEdges(H))
				{
					Stack.push_back(G.mTopology.TryGetEdge(E)->mFrom);
				}
			}
			eastl::vector<FHandle> Nodes;
			for (auto H : G.mTopology.GetNodes())
			{
				if (Live.count(H.GetIndex()))
				{
					Nodes.push_back(H);
				}
				else
				{
					Result.mCulledNodes.push_back(H);
				}
			}
			eastl::sort(Nodes.begin(),
			    Nodes.end(),
			    [&](auto A, auto B)
			    {
				    return G.mTopology.TryGetNode(A)->mPayload.mName < G.mTopology.TryGetNode(B)->mPayload.mName;
			    });
			return Nodes;
		}

		FArdaRHIStatus ValidatePipelines(const FArdaDependencyGraph::FImpl& Graph, const eastl::vector<FHandle>& Live)
		{
			eastl::vector<FArdaInductorPipelineNode> Nodes;
			Nodes.reserve(Live.size());
			for (const auto Handle : Live)
			{
				FArdaInductorPipelineNode Node;
				Node.mNodeId = Handle.GetIndex();
				Node.mContributions = Graph.mTopology.TryGetNode(Handle)->mPayload.mDesc.mPipelineStages;
				for (const auto& Request : Graph.mTopology.TryGetNode(Handle)->mPayload.mDesc.mPipelines)
				{
					Node.mPipelineBoundaries.push_back(Request.mKind);
				}
				for (const auto Edge : Graph.mTopology.GetIncomingEdges(Handle))
				{
					Node.mDependencies.push_back(Graph.mTopology.TryGetEdge(Edge)->mFrom.GetIndex());
				}
				Nodes.push_back(eastl::move(Node));
			}
			for (const auto Handle : Live)
			{
				eastl::unordered_set<eastl::string> Slots;
				const auto& Requests = Graph.mTopology.TryGetNode(Handle)->mPayload.mDesc.mPipelines;
				for (const auto& Request : Requests)
				{
					if (!Slots.insert(Request.mSlot.empty() ? eastl::string("default") : Request.mSlot).second)
					{
						return Error("A node requests duplicate pipeline slots.");
					}
				}
				for (auto Request : Requests)
				{
					if (Request.mSlot.empty())
					{
						Request.mSlot = "default";
					}
					Request.mTerminalNodeId = Handle.GetIndex();
					const auto Pattern = InferArdaInductorPipeline(Nodes, Request);
					if (!Pattern)
					{
						return Pattern.mStatus;
					}
				}
			}
			return {};
		}

		double NodeCost(const FArdaDependencyGraph::FImpl& Graph, const FArdaDependencyNode& Node)
		{
			if (IsDeclaration(Node))
			{
				return 0;
			}
			const auto Cost = Graph.mCostOverrides.find(Node.mName);
			const auto Key = Graph.mCostOverrideKeys.find(Node.mName);
			const auto Definition = Graph.mCostOverrideDefinitions.find(Node.mName);
			return Cost != Graph.mCostOverrides.end() && Key != Graph.mCostOverrideKeys.end() &&
			        Definition != Graph.mCostOverrideDefinitions.end() &&
			        Definition->second.lock() == Node.mDefinition && Key->second == Node.mCanonicalKey
			    ? Cost->second
			    : double(Node.mDesc.mEstimatedCost);
		}

		void AssignQueues(const FArdaDependencyGraph::FImpl& G, FArdaInductorCompileResult& R);

		struct FScheduleSearch
		{
			FArdaDependencyGraph::FImpl& mGraph;
			eastl::vector<FHandle> mNodes;
			eastl::vector<FArdaInductorMemoryRequest> mRequests;
			eastl::unordered_map<uint32_t, size_t> mIndices;
			eastl::vector<eastl::vector<size_t>> mPred, mNext, mUses;
			eastl::vector<double> mCritical;
			uint64_t mWorkspace = 0;
			uint64_t mBestBytes = UINT64_MAX;
			double mBestCost = std::numeric_limits<double>::infinity();
			eastl::vector<FHandle> mBestOrder;
			FArdaInductorMemoryPlan mBestPlan;
			FArdaInductorCompileResult& mResult;
			uint64_t mStates = 0;
			bool mbComplete = false;
			bool mbExhausted = false;
			bool mbHasBestPlan = false;
			FArdaRHIStatus mLastPlanFailure;

			FScheduleSearch(FArdaDependencyGraph::FImpl& Graph,
			    eastl::vector<FHandle> Nodes,
			    FArdaInductorCompileResult& Result)
			    : mGraph(Graph),
			      mNodes(eastl::move(Nodes)),
			      mResult(Result)
			{
			}

			FArdaRHIStatus Initialize()
			{
				mPred.resize(mNodes.size());
				mNext.resize(mNodes.size());
				mUses.resize(mNodes.size());
				mCritical.resize(mNodes.size());
				for (size_t I = 0; I < mNodes.size(); ++I)
				{
					mIndices.emplace(mNodes[I].GetIndex(), I);
				}
				mRequests.resize(mGraph.mResources.size());
				for (uint32_t R = 0; R < mRequests.size(); ++R)
				{
					auto& Q = mRequests[R];
					const auto& D = mGraph.mResources[R];
					Q.mIdentifier = R;
					Q.mbUsed = false;
					Q.mKind = D.mExternalAccelerationStructure ? EArdaInductorMemoryKind::AccelerationStructure
					    : D.mbTexture                          ? EArdaInductorMemoryKind::Texture
					                                           : EArdaInductorMemoryKind::Buffer;
					Q.mBufferDesc = D.mBuffer;
					Q.mTextureDesc = D.mTexture;
					Q.mExternalBuffer = D.mExternalBuffer;
					Q.mExternalTexture = D.mExternalTexture;
					Q.mExternalAccelerationStructure = D.mExternalAccelerationStructure;
					Q.mbPersistent = D.mbPersistent || D.mbOutput;
					if (D.mbPersistent)
					{
						if (R < mGraph.mPersistentBuffers.size() && mGraph.mPersistentBuffers[R])
						{
							Q.mExternalBuffer = mGraph.mPersistentBuffers[R];
						}
						if (R < mGraph.mPersistentTextures.size() && mGraph.mPersistentTextures[R])
						{
							Q.mExternalTexture = mGraph.mPersistentTextures[R];
						}
					}
					// Both imports and persistent allocations are retained even when all users are culled.
					Q.mbUsed = Q.mExternalBuffer || Q.mExternalTexture || Q.mExternalAccelerationStructure;
				}
				// Parameters remain owned by the persistent IR when a node is culled, including adapter workspaces.
				for (const auto Handle : mGraph.mTopology.GetNodes())
				{
					if (!AddBytes(mWorkspace, mGraph.mTopology.TryGetNode(Handle)->mPayload.mDesc.mWorkspaceBytes))
					{
						return Error("Declared adapter workspace sum overflows.");
					}
				}
				for (size_t I = 0; I < mNodes.size(); ++I)
				{
					const auto& N = mGraph.mTopology.TryGetNode(mNodes[I])->mPayload;
					for (auto E : mGraph.mTopology.GetIncomingEdges(mNodes[I]))
					{
						auto P = mIndices.find(mGraph.mTopology.TryGetEdge(E)->mFrom.GetIndex());
						if (P == mIndices.end())
						{
							continue;
						}
						mPred[I].push_back(P->second);
						mNext[P->second].push_back(I);
					}
					for (const auto& A : N.mDesc.mAccesses)
					{
						const auto R = A.mResource.mIndex;
						mRequests[R].mbUsed = true;
						if (N.mDefinition->mKind == EArdaDependencyNodeKind::Cuda)
						{
							mRequests[R].mbAllowHeapPlacement = false;
						}
						if (eastl::find(mUses[I].begin(), mUses[I].end(), R) == mUses[I].end())
						{
							mUses[I].push_back(R);
						}
					}
					if (N.mDesc.mTransientWorkspaceBytes)
					{
						FArdaInductorMemoryRequest Scratch;
						Scratch.mIdentifier = static_cast<uint32_t>(mRequests.size());
						Scratch.mbUsed = true;
						Scratch.mKind = EArdaInductorMemoryKind::Buffer;
						Scratch.mBufferDesc.mByteSize = N.mDesc.mTransientWorkspaceBytes;
						Scratch.mBufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::Raw;
						Scratch.mBufferDesc.mbCudaInterop = N.mDefinition->mKind == EArdaDependencyNodeKind::Cuda;
						Scratch.mBufferDesc.mInitialState = EArdaRHIResourceState::Common;
						Scratch.mbAllowHeapPlacement = N.mDefinition->mKind != EArdaDependencyNodeKind::Cuda;
						mResult.mWorkspaceResourceIds.emplace(mNodes[I].GetIndex(), Scratch.mIdentifier);
						mUses[I].push_back(Scratch.mIdentifier);
						mRequests.push_back(eastl::move(Scratch));
					}
				}
				if (mGraph.mDevice)
				{
					if (auto S = DescribeArdaInductorMemory(*mGraph.mDevice, mRequests); !S)
					{
						return S;
					}
				}
				else
				{
					if (mGraph.mOptions.mMaxVramBytes)
					{
						return Error("A hard VRAM budget requires a device and native allocation requirements.");
					}
					for (auto& Q : mRequests)
					{
						if (Q.mbUsed)
						{
							uint64_t Bytes = Q.mExternalAccelerationStructure
							    ? eastl::max<uint64_t>(1,
							          Q.mExternalAccelerationStructure->GetMemoryAllocationInfo().mByteSize)
							    : Q.mBufferDesc.mByteSize;
							if (Q.mKind == EArdaInductorMemoryKind::Texture)
							{
								const auto& D = Q.mTextureDesc;
								const auto& F = GetArdaRHIFormatInfo(D.mFormat);
								Bytes = 0;
								for (uint32_t M = 0; M < D.mMipLevels; ++M)
								{
									uint64_t B =
									    (uint64_t(GetArdaRHITextureMipExtent(D.mWidth, M)) + F.mBlockWidth - 1) /
									    F.mBlockWidth;
									for (uint64_t X : {uint64_t((uint64_t(GetArdaRHITextureMipExtent(D.mHeight, M)) +
									                                F.mBlockHeight - 1) /
									                       F.mBlockHeight),
									         uint64_t(GetArdaRHITextureMipExtent(D.mDepth, M)),
									         uint64_t(D.mArraySize),
									         uint64_t(F.mBytesPerBlock),
									         uint64_t(D.mSampleCount)})
									{
										if (X && B > UINT64_MAX / X)
										{
											return Error("Logical texture size overflows.");
										}
										B *= X;
									}
									if (!AddBytes(Bytes, B))
									{
										return Error("Logical texture size overflows.");
									}
								}
							}
							Q.mRequirements.mSize = Bytes;
							Q.mRequirements.mAlignment = 1;
							Q.mRequirements.mMemoryTypeBits = 1;
						}
					}
				}
				auto Topo = mGraph.mTopology.TopologicalSort().mOrder;
				for (auto It = Topo.rbegin(); It != Topo.rend(); ++It)
				{
					auto Found = mIndices.find(It->GetIndex());
					if (Found == mIndices.end())
					{
						continue;
					}
					const auto I = Found->second;
					double Tail = 0;
					for (auto J : mNext[I])
					{
						Tail = eastl::max(Tail, mCritical[J]);
					}
					mCritical[I] = Tail + NodeCost(mGraph, mGraph.mTopology.TryGetNode(*It)->mPayload);
				}
				return {};
			}

			void Evaluate(const eastl::vector<size_t>& Order, bool Serialize)
			{
				// Under a cap, a larger parallel pool may still fit and be faster than
				// the allocator's serialization-for-reuse candidate. Evaluate both policies.
				if (Serialize && mGraph.mOptions.mObjective == EArdaInductorObjective::Efficiency)
				{
					Evaluate(Order, false);
				}
				++mResult.mSchedulesExamined;
				auto Q = mRequests;
				for (auto& R : Q)
				{
					R.mFirstUse = UINT32_MAX;
					R.mLastUse = 0;
					R.mFirstUseNodes.clear();
					R.mLastUseNodes.clear();
				}
				for (uint32_t P = 0; P < Order.size(); ++P)
				{
					for (auto R : mUses[Order[P]])
					{
						auto& V = Q[R];
						V.mFirstUse = eastl::min(V.mFirstUse, P);
						V.mLastUse = P;
						// Every user participates: cross-queue lifetimes require a dependency proof, not ordinal coincidence.
						V.mLastUseNodes.push_back(mNodes[Order[P]].GetIndex());
					}
				}
				for (auto& R : Q)
				{
					if (R.mbUsed)
					{
						if (R.mFirstUse == UINT32_MAX)
						{
							R.mFirstUse = R.mLastUse = 0;
						}
						else
						{
							R.mFirstUseNodes.push_back(mNodes[Order[R.mFirstUse]].GetIndex());
						}
					}
				}
				FArdaInductorMemoryOptions O;
				O.mFrameCount = mGraph.mOptions.mFramesInFlight;
				O.mbAllowSerialization = Serialize;
				O.mWorkspaceBytes = mWorkspace;
				O.mBudgetBytes = mGraph.mOptions.mMaxVramBytes ? mGraph.mOptions.mMaxVramBytes : UINT64_MAX;
				O.mHappensBeforeContext = this;
				O.mHappensBefore = [](void* Context, uint32_t A, uint32_t B)
				{
					auto& S = *static_cast<FScheduleSearch*>(Context);
					return S.mGraph.mTopology.IsReachable(S.mNodes[S.mIndices.at(A)], S.mNodes[S.mIndices.at(B)]);
				};
				auto P = PlanArdaInductorMemory(Q, O);
				if (!P)
				{
					mLastPlanFailure = P.mStatus;
					return;
				}
				FArdaInductorCompileResult Candidate;
				for (const auto I : Order)
				{
					Candidate.mExecutionOrder.push_back(mNodes[I]);
				}
				for (const auto& Edge : P.mValue.mAliasEdges)
				{
					Candidate.mMemoryDependencies.push_back(
					    {mNodes[mIndices.at(Edge.mProducer)], mNodes[mIndices.at(Edge.mConsumer)]});
				}
				AssignQueues(mGraph, Candidate);
				eastl::vector<double> Finish(mNodes.size(), 0);
				eastl::vector<eastl::vector<size_t>> ExtraPred(mNodes.size());
				for (const auto& Edge : P.mValue.mAliasEdges)
				{
					ExtraPred[mIndices.at(Edge.mConsumer)].push_back(mIndices.at(Edge.mProducer));
				}
				double QueueEnd[ArdaRHIQueueTypeCount]{};
				uint64_t Handoffs = 0;
				bool HavePrevious = false, PreviousCuda = false;
				for (size_t Position = 0; Position < Order.size(); ++Position)
				{
					const auto Index = Order[Position];
					const auto& Node = mGraph.mTopology.TryGetNode(mNodes[Index])->mPayload;
					double Ready = 0;
					for (const auto Pred : mPred[Index])
					{
						Ready = eastl::max(Ready, Finish[Pred]);
					}
					for (const auto Pred : ExtraPred[Index])
					{
						Ready = eastl::max(Ready, Finish[Pred]);
					}
					if (IsDeclaration(Node))
					{
						Finish[Index] = Ready;
						continue;
					}
					const auto QueueIndex = GetArdaRHIQueueIndex(Candidate.mQueues[Position]);
					Finish[Index] = eastl::max(Ready, QueueEnd[QueueIndex]) + NodeCost(mGraph, Node);
					QueueEnd[QueueIndex] = Finish[Index];
					const bool Cuda = Node.mDefinition->mKind == EArdaDependencyNodeKind::Cuda;
					if (HavePrevious && PreviousCuda != Cuda)
					{
						++Handoffs;
					}
					HavePrevious = true;
					PreviousCuda = Cuda;
				}
				double Cost = double(Handoffs) * mGraph.mOptions.mCudaHandoffCost;
				for (const auto& Activation : P.mValue.mAliasActivations)
				{
					if (Q[Activation.mResource].mKind == EArdaInductorMemoryKind::Texture)
					{
						Cost += 2.0 * mGraph.mOptions.mAliasSubmissionCost;
					}
				}
				Cost += *eastl::max_element(QueueEnd, QueueEnd + ArdaRHIQueueTypeCount);
				const bool Memory = mGraph.mOptions.mObjective == EArdaInductorObjective::Memory;
				const uint64_t Bytes = P.mValue.mTotalBytes;
				const bool Better = Memory ? (Bytes < mBestBytes || (Bytes == mBestBytes && Cost < mBestCost))
				                           : (Cost < mBestCost || (Cost == mBestCost && Bytes < mBestBytes));
				if (mbHasBestPlan && !Better)
				{
					return;
				}
				mBestBytes = Bytes;
				mBestCost = Cost;
				mResult.mEstimatedExecutionCost = Cost;
				mbHasBestPlan = true;
				mBestPlan = eastl::move(P.mValue);
				mBestOrder.clear();
				for (auto I : Order)
				{
					mBestOrder.push_back(mNodes[I]);
				}
				uint64_t Peak = mWorkspace;
				if (!AddBytes(Peak, mBestPlan.mExternalBytes))
				{
					Peak = UINT64_MAX;
				}
				for (uint32_t Pos = 0; Pos < Order.size(); ++Pos)
				{
					uint64_t Bytes = mWorkspace;
					if (!AddBytes(Bytes, mBestPlan.mExternalBytes))
					{
						Bytes = UINT64_MAX;
					}
					for (const auto& R : Q)
					{
						if (R.mbUsed && !R.mExternalBuffer && !R.mExternalTexture &&
						    !R.mExternalAccelerationStructure &&
						    (R.mbPersistent || (R.mFirstUse <= Pos && Pos <= R.mLastUse)))
						{
							const uint64_t Copies = R.mbPersistent ? 1 : mGraph.mOptions.mFramesInFlight;
							if (R.mRequirements.mSize > UINT64_MAX / Copies ||
							    !AddBytes(Bytes, R.mRequirements.mSize * Copies))
							{
								Bytes = UINT64_MAX;
							}
						}
					}
					Peak = eastl::max(Peak, Bytes);
				}
				mResult.mPeakLiveBytes = Peak;
			}

			void Greedy(bool Memory)
			{
				eastl::vector<size_t> Degree;
				for (const auto& P : mPred)
				{
					Degree.push_back(P.size());
				}
				eastl::vector<uint32_t> Remaining(mRequests.size());
				for (const auto& Uses : mUses)
				{
					for (auto R : Uses)
					{
						++Remaining[R];
					}
				}
				eastl::vector<bool> Seen(mRequests.size(), false), Done(mNodes.size(), false);
				eastl::vector<size_t> Order;
				while (Order.size() < mNodes.size())
				{
					size_t Best = SIZE_MAX;
					long double BestScore = -std::numeric_limits<long double>::infinity();
					for (size_t I = 0; I < mNodes.size(); ++I)
					{
						if (!Done[I] && !Degree[I])
						{
							long double Score = static_cast<long double>(mCritical[I]);
							if (Memory)
							{
								Score = 0;
								for (auto R : mUses[I])
								{
									const auto& Q = mRequests[R];
									if (Q.mbPersistent || Q.mExternalBuffer || Q.mExternalTexture ||
									    Q.mExternalAccelerationStructure)
									{
										continue;
									}
									if (!Seen[R])
									{
										Score -= Q.mRequirements.mSize;
									}
									if (Remaining[R] == 1)
									{
										Score += Q.mRequirements.mSize;
									}
								}
							}
							else if (!Order.empty() &&
							    mGraph.mTopology.TryGetNode(mNodes[Order.back()])->mPayload.mDefinition->mKind ==
							        EArdaDependencyNodeKind::Cuda &&
							    mGraph.mTopology.TryGetNode(mNodes[I])->mPayload.mDefinition->mKind ==
							        EArdaDependencyNodeKind::Cuda)
							{
								Score += double(mGraph.mOptions.mCudaHandoffCost) * 2;
							}
							if (Best == SIZE_MAX || Score > BestScore)
							{
								Best = I;
								BestScore = Score;
							}
						}
					}
					if (Best == SIZE_MAX)
					{
						return;
					}
					Done[Best] = true;
					Order.push_back(Best);
					for (auto J : mNext[Best])
					{
						--Degree[J];
					}
					for (auto R : mUses[Best])
					{
						Seen[R] = true;
						--Remaining[R];
					}
				}
				Evaluate(Order,
				    mGraph.mOptions.mMaxVramBytes || mGraph.mOptions.mObjective == EArdaInductorObjective::Memory);
			}

			void ImproveIncumbent()
			{
				if (!mbHasBestPlan || mGraph.mOptions.mSearchMode != EArdaInductorSearchMode::Bounded)
				{
					return;
				}
				// Spend at most a quarter of the search budget on whole-order neighborhoods.
				// This visits early decisions even when a large suffix would exhaust DFS.
				const uint64_t Limit = mGraph.mOptions.mMaxSearchStates / 4;
				mStates = 1;
				for (uint32_t Pass = 0; Pass < 4 && mStates < Limit; ++Pass)
				{
					bool Improved = false;
					for (size_t Position = 1; Position < mBestOrder.size() && mStates < Limit; ++Position)
					{
						eastl::vector<size_t> Order;
						for (const auto H : mBestOrder)
						{
							Order.push_back(mIndices.at(H.GetIndex()));
						}
						const auto Before = Order[Position - 1], After = Order[Position];
						// Adjacent vertices in a topological order cannot have an indirect path
						// between them. A direct edge is the only reason their swap is illegal.
						if (eastl::find(mPred[After].begin(), mPred[After].end(), Before) != mPred[After].end())
						{
							continue;
						}
						eastl::swap(Order[Position - 1], Order[Position]);
						const auto Previous = mBestOrder;
						++mStates;
						Evaluate(Order,
						    mGraph.mOptions.mMaxVramBytes ||
						        mGraph.mOptions.mObjective == EArdaInductorObjective::Memory);
						Improved |= Previous != mBestOrder;
					}
					if (!Improved)
					{
						break;
					}
				}
			}

			void Explore()
			{
				// Iterative Kahn backtracking does not impose a graph-depth or call-stack limit.
				// Explore near the incumbent first, retaining deterministic name order for ties.
				eastl::vector<size_t> Priority, Order, Degree, Cursor(mNodes.size() + 1, 0);
				eastl::vector<bool> Done(mNodes.size(), false);
				for (const auto H : mBestOrder)
				{
					Priority.push_back(mIndices.at(H.GetIndex()));
				}
				if (Priority.empty())
				{
					for (size_t I = 0; I < mNodes.size(); ++I)
					{
						Priority.push_back(I);
					}
				}
				for (const auto& Pred : mPred)
				{
					Degree.push_back(Pred.size());
				}
				const bool Serialize =
				    mGraph.mOptions.mMaxVramBytes || mGraph.mOptions.mObjective == EArdaInductorObjective::Memory;
				mStates = eastl::max(uint64_t(1), mStates);
				for (;;)
				{
					if (Order.size() == mNodes.size())
					{
						Evaluate(Order, Serialize);
					}
					else
					{
						auto& Next = Cursor[Order.size()];
						while (Next < Priority.size() && (Done[Priority[Next]] || Degree[Priority[Next]]))
						{
							++Next;
						}
						if (Next < Priority.size())
						{
							if (mGraph.mOptions.mSearchMode != EArdaInductorSearchMode::Exhaustive &&
							    mStates >= mGraph.mOptions.mMaxSearchStates)
							{
								mbExhausted = true;
								return;
							}
							const size_t I = Priority[Next++];
							Done[I] = true;
							Order.push_back(I);
							for (const auto N : mNext[I])
							{
								--Degree[N];
							}
							Cursor[Order.size()] = 0;
							++mStates;
							continue;
						}
					}
					if (Order.empty())
					{
						mbComplete = true;
						return;
					}
					const auto Previous = Order.back();
					Order.pop_back();
					Done[Previous] = false;
					for (const auto N : mNext[Previous])
					{
						++Degree[N];
					}
				}
			}
		};

		void AssignQueues(const FArdaDependencyGraph::FImpl& G, FArdaInductorCompileResult& R)
		{
			eastl::unordered_map<uint32_t, eastl::vector<FHandle>> MemorySuccessors;
			for (const auto& Edge : R.mMemoryDependencies)
			{
				MemorySuccessors[Edge.first.GetIndex()].push_back(Edge.second);
			}
			const auto IsOrdered = [&](FHandle From, FHandle To)
			{
				if (MemorySuccessors.empty())
				{
					return G.mTopology.IsReachable(From, To);
				}
				eastl::vector<FHandle> Pending{From};
				eastl::unordered_set<uint32_t> Seen;
				for (size_t Index = 0; Index < Pending.size(); ++Index)
				{
					const auto Node = Pending[Index];
					if (Node == To)
					{
						return true;
					}
					if (!Seen.insert(Node.GetIndex()).second)
					{
						continue;
					}
					for (const auto Edge : G.mTopology.GetOutgoingEdges(Node))
					{
						Pending.push_back(G.mTopology.TryGetEdge(Edge)->mTo);
					}
					const auto Added = MemorySuccessors.find(Node.GetIndex());
					if (Added != MemorySuccessors.end())
					{
						Pending.insert(Pending.end(), Added->second.begin(), Added->second.end());
					}
				}
				return false;
			};
			const auto QueueAvailable = [&](EArdaRHIQueueType Queue)
			{
				return !G.mDevice ||
				    (G.mDevice->GetCapabilities().IsQueueSupported(Queue) &&
				        G.mDevice->GetCapabilities().mQueues.mbGpuWaits);
			};
			const bool CopyAvailable = G.mOptions.mbEnableCopyQueue && QueueAvailable(EArdaRHIQueueType::Copy);
			const bool ComputeAvailable = G.mOptions.mbEnableAsyncCompute && QueueAvailable(EArdaRHIQueueType::Compute);
			// AS storage has no cross-family ownership API yet; compute commands remain valid on Graphics.
			const auto AsyncCandidate = [&](FHandle Handle)
			{
				const auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
				return Node.mDefinition->mKind == EArdaDependencyNodeKind::Compute &&
				    eastl::none_of(Node.mDesc.mAccesses.begin(),
				        Node.mDesc.mAccesses.end(),
				        [&](const auto& Access)
				        {
					        return bool(G.mResources[Access.mResource.mIndex].mExternalAccelerationStructure);
				        });
			};
			R.mQueues.assign(R.mExecutionOrder.size(), EArdaRHIQueueType::Graphics);
			R.mCudaBatches.clear();
			eastl::unordered_map<uint32_t, size_t> Position;
			for (size_t I = 0; I < R.mExecutionOrder.size(); ++I)
			{
				Position.emplace(R.mExecutionOrder[I].GetIndex(), I);
			}
			eastl::unordered_set<uint32_t> Visited;
			for (size_t I = 0; I < R.mExecutionOrder.size(); ++I)
			{
				auto H = R.mExecutionOrder[I];
				const auto& N = G.mTopology.TryGetNode(H)->mPayload;
				if (N.mDefinition->mKind == EArdaDependencyNodeKind::Copy && CopyAvailable)
				{
					R.mQueues[I] = EArdaRHIQueueType::Copy;
				}
				if (!AsyncCandidate(H) || !ComputeAvailable || Visited.count(H.GetIndex()))
				{
					continue;
				}
				eastl::vector<FHandle> Chain{H};
				Visited.insert(H.GetIndex());
				for (size_t C = 0; C < Chain.size(); ++C)
				{
					for (auto E : G.mTopology.GetOutgoingEdges(Chain[C]))
					{
						auto Next = G.mTopology.TryGetEdge(E)->mTo;
						if (Position.count(Next.GetIndex()) && AsyncCandidate(Next) &&
						    Visited.insert(Next.GetIndex()).second)
						{
							Chain.push_back(Next);
						}
					}
				}
				size_t First = I, Last = I, Join = R.mExecutionOrder.size();
				for (auto C : Chain)
				{
					First = eastl::min(First, Position[C.GetIndex()]);
					Last = eastl::max(Last, Position[C.GetIndex()]);
					for (auto E : G.mTopology.GetOutgoingEdges(C))
					{
						auto D = G.mTopology.TryGetEdge(E)->mTo;
						if (Position.count(D.GetIndex()) && !AsyncCandidate(D))
						{
							Join = eastl::min(Join, Position[D.GetIndex()]);
						}
					}
				}
				double IndependentCost = 0;
				// Independent graphics recorded before the first compute node can still overlap on the GPU.
				for (size_t J = 0; J < Join; ++J)
				{
					auto D = R.mExecutionOrder[J];
					if (G.mTopology.TryGetNode(D)->mPayload.mDefinition->mKind != EArdaDependencyNodeKind::Graphics)
					{
						continue;
					}
					bool Independent = true;
					for (auto C : Chain)
					{
						Independent &= !IsOrdered(C, D) && !IsOrdered(D, C);
					}
					if (Independent)
					{
						IndependentCost += NodeCost(G, G.mTopology.TryGetNode(D)->mPayload);
					}
				}
				if (Chain.size() >= G.mOptions.mMinimumAsyncChain && Join > Last &&
				    IndependentCost >= G.mOptions.mMinimumAsyncSlack)
				{
					for (auto C : Chain)
					{
						R.mQueues[Position[C.GetIndex()]] = EArdaRHIQueueType::Compute;
					}
				}
			}
			eastl::vector<FHandle> Batch;
			for (const auto Handle : R.mExecutionOrder)
			{
				const auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
				if (IsDeclaration(Node))
				{
					continue;
				}
				if (Node.mDefinition->mKind == EArdaDependencyNodeKind::Cuda)
				{
					Batch.push_back(Handle);
				}
				else if (!Batch.empty())
				{
					R.mCudaBatches.push_back(eastl::move(Batch));
					Batch.clear();
				}
			}
			if (!Batch.empty())
			{
				R.mCudaBatches.push_back(eastl::move(Batch));
			}
		}
	}

	double GetArdaInductorScheduleNodeCost(const FArdaDependencyGraph::FImpl& Snapshot, const FArdaDependencyNode& Node)
	{
		return NodeCost(Snapshot, Node);
	}

	TArdaRHIResult<FArdaInductorCompileResult> EvaluateArdaInductorFixedSchedule(const FArdaDependencyGraph::FImpl& G,
	    const FArdaInductorMemoryPlan& FixedPlan,
	    const eastl::vector<FArdaGraphNodeHandle>& Order,
	    bool AssignAutomaticQueues)
	{
		if (!FixedPlan.mbBudgetAccepted || FixedPlan.mFrameCount != G.mOptions.mFramesInFlight ||
		    FixedPlan.mTotalBytes != G.mCompile.mAllocatedBytes ||
		    (G.mOptions.mMaxVramBytes && FixedPlan.mTotalBytes > G.mOptions.mMaxVramBytes))
		{
			return {{},
			    Error(
			        "Adaptive scheduling requires the incumbent accepted allocation plan within the current budget.")};
		}
		if (Order.size() != G.mCompile.mExecutionOrder.size() || G.mCompile.mQueues.size() != Order.size())
		{
			return {{},
			    Error("Adaptive scheduling must preserve every incumbent node and its queue assignment domain.")};
		}
		eastl::unordered_map<uint32_t, size_t> Position;
		eastl::unordered_set<FArdaGraphNodeHandle, FArdaGraphNodeHandleHash> Incumbent;
		for (auto H : G.mCompile.mExecutionOrder)
		{
			Incumbent.insert(H);
		}
		for (size_t I = 0; I < Order.size(); ++I)
		{
			const auto H = Order[I];
			const auto* Record = G.mTopology.TryGetNode(H);
			if (!Record || !Record->mPayload.mDefinition || !Incumbent.count(H) ||
			    !Position.emplace(H.GetIndex(), I).second)
			{
				return {{}, Error("Adaptive candidate contains a duplicate, foreign or stale semantic node.")};
			}
			const double Cost = NodeCost(G, Record->mPayload);
			if (!std::isfinite(Cost) || Cost < 0)
			{
				return {{}, Error("Adaptive timing costs must be finite and nonnegative.")};
			}
			for (const auto& Access : Record->mPayload.mDesc.mAccesses)
			{
				if (!G.HasResource(Access.mResource))
				{
					return {{}, Error("Adaptive snapshot contains a stale resource access.")};
				}
			}
		}
		FArdaInductorCompileResult Candidate = G.mCompile;
		Candidate.mExecutionOrder = Order;
		eastl::vector<eastl::vector<size_t>> Predecessors(Order.size());
		const auto AddPredecessor = [&](FArdaGraphNodeHandle From, FArdaGraphNodeHandle To) -> bool
		{
			const auto P = Position.find(From.GetIndex()), C = Position.find(To.GetIndex());
			if (P == Position.end() || C == Position.end() || Order[P->second] != From || Order[C->second] != To ||
			    P->second >= C->second)
			{
				return false;
			}
			auto& Pred = Predecessors[C->second];
			if (eastl::find(Pred.begin(), Pred.end(), P->second) == Pred.end())
			{
				Pred.push_back(P->second);
			}
			return true;
		};
		for (const auto H : Order)
		{
			for (const auto E : G.mTopology.GetIncomingEdges(H))
			{
				if (!AddPredecessor(G.mTopology.TryGetEdge(E)->mFrom, H))
				{
					return {{}, Error("Adaptive candidate violates semantic dependencies.")};
				}
			}
		}
		for (const auto& Edge : Candidate.mMemoryDependencies)
		{
			if (!AddPredecessor(Edge.first, Edge.second))
			{
				return {{}, Error("Adaptive candidate violates a fixed memory dependency.")};
			}
		}
		for (const auto& Edge : FixedPlan.mAliasEdges)
		{
			const auto P = Position.find(Edge.mProducer), C = Position.find(Edge.mConsumer);
			if (P == Position.end() || C == Position.end() || !AddPredecessor(Order[P->second], Order[C->second]))
			{
				return {{}, Error("Adaptive candidate violates the accepted allocation's alias order.")};
			}
			const auto Pair = eastl::make_pair(Order[P->second], Order[C->second]);
			if (eastl::find(Candidate.mMemoryDependencies.begin(), Candidate.mMemoryDependencies.end(), Pair) ==
			    Candidate.mMemoryDependencies.end())
			{
				return {{}, Error("The incumbent compile is missing a fixed native alias dependency.")};
			}
		}
		// Native alias edges terminate at the accepted first user. Keep that boundary
		// fixed: moving a different independent user ahead of it would preserve a
		// linear lifetime interval while losing the cross-queue happens-before proof.
		eastl::unordered_set<uint32_t> FixedFirstUsers;
		for (const auto& Activation : FixedPlan.mAliasActivations)
		{
			FixedFirstUsers.insert(Activation.mResource);
			FixedFirstUsers.insert(Activation.mPreviousResources.begin(), Activation.mPreviousResources.end());
		}
		for (const auto& Slot : FixedPlan.mSlots)
		{
			if (Slot.mResources.size() > 1)
			{
				FixedFirstUsers.insert(Slot.mResources.begin(), Slot.mResources.end());
			}
		}
		for (const auto Identifier : FixedFirstUsers)
		{
			if (Identifier >= FixedPlan.mRequests.size())
			{
				return {{}, Error("Adaptive alias boundary references missing storage.")};
			}
			const auto& Request = FixedPlan.mRequests[Identifier];
			if (Request.mFirstUseNodes.size() != 1 || !Position.count(Request.mFirstUseNodes.front()))
			{
				return {{}, Error("Adaptive alias storage must retain its accepted first-use boundary.")};
			}
			const auto FirstPosition = Position.at(Request.mFirstUseNodes.front());
			for (const auto User : Request.mLastUseNodes)
			{
				if (!Position.count(User) || Position.at(User) < FirstPosition)
				{
					return {{}, Error("Adaptive candidate moves an alias user ahead of its activation boundary.")};
				}
			}
		}
		if (AssignAutomaticQueues)
		{
			AssignQueues(G, Candidate);
		}
		else if (Order != G.mCompile.mExecutionOrder)
		{
			return {{}, Error("An incumbent queue cost can only be evaluated in its original order.")};
		}
		eastl::vector<double> Finish(Order.size(), 0);
		double QueueEnd[ArdaRHIQueueTypeCount]{};
		uint64_t Handoffs = 0;
		bool HavePrevious = false, PreviousCuda = false;
		for (size_t I = 0; I < Order.size(); ++I)
		{
			double Ready = 0;
			for (const auto P : Predecessors[I])
			{
				Ready = eastl::max(Ready, Finish[P]);
			}
			const auto& Node = G.mTopology.TryGetNode(Order[I])->mPayload;
			if (IsDeclaration(Node))
			{
				Finish[I] = Ready;
				continue;
			}
			const auto Queue = GetArdaRHIQueueIndex(Candidate.mQueues[I]);
			if (Queue >= ArdaRHIQueueTypeCount)
			{
				return {{}, Error("Adaptive schedule has an invalid queue.")};
			}
			Finish[I] = eastl::max(Ready, QueueEnd[Queue]) + NodeCost(G, Node);
			if (!std::isfinite(Finish[I]))
			{
				return {{}, Error("Adaptive critical-path cost overflowed.")};
			}
			QueueEnd[Queue] = Finish[I];
			const bool Cuda = Node.mDefinition->mKind == EArdaDependencyNodeKind::Cuda;
			if (HavePrevious && PreviousCuda != Cuda)
			{
				++Handoffs;
			}
			HavePrevious = true;
			PreviousCuda = Cuda;
		}
		double Cost = *eastl::max_element(QueueEnd, QueueEnd + ArdaRHIQueueTypeCount) +
		    double(Handoffs) * G.mOptions.mCudaHandoffCost;
		for (const auto& Activation : FixedPlan.mAliasActivations)
		{
			if (Activation.mResource >= FixedPlan.mRequests.size())
			{
				return {{}, Error("Adaptive alias activation references missing storage.")};
			}
			if (FixedPlan.mRequests[Activation.mResource].mKind == EArdaInductorMemoryKind::Texture)
			{
				Cost += 2.0 * G.mOptions.mAliasSubmissionCost;
			}
		}
		if (!std::isfinite(Cost))
		{
			return {{}, Error("Adaptive schedule cost overflowed.")};
		}
		Candidate.mEstimatedExecutionCost = Cost;
		return {eastl::move(Candidate), {}};
	}

	FArdaRHIStatus FArdaInductor::Compile(FArdaDependencyGraph& Graph)
	{
		auto& G = *Graph.mImpl;
		if (!G.mbEditing)
		{
			return Error("ArdaInductor compiles only an active graph edit.");
		}
		if (auto S = ResolveDependencies(G); !S)
		{
			return S;
		}
		FArdaInductorCompileResult Result;
		Result.mRevision = G.mCompile.mRevision + 1;
		auto Nodes = LiveNodes(G, Result);
		if (auto Status = ValidatePipelines(G, Nodes); !Status)
		{
			return Status;
		}
		FScheduleSearch Search(G, eastl::move(Nodes), Result);
		if (auto S = Search.Initialize(); !S)
		{
			return S;
		}
		Search.Greedy(false);
		Search.Greedy(true);
		Search.ImproveIncumbent();
		if (G.mOptions.mSearchMode != EArdaInductorSearchMode::Greedy)
		{
			Search.Explore();
		}
		Result.mSearchStatesExamined = Search.mStates;
		Result.mbSearchComplete = Search.mbComplete;
		Result.mbSearchExhausted = Search.mbExhausted;
		if (!Search.mbHasBestPlan)
		{
			if (!Search.mbHasBestPlan)
			{
				if (!Search.mLastPlanFailure && Search.mLastPlanFailure.mCode != EArdaRHIResult::InvalidState)
				{
					return Search.mLastPlanFailure;
				}
				return Error(!Search.mbComplete
				        ? "No VRAM-feasible allocation schedule found within the configured search limit."
				        : "No VRAM-feasible schedule exists for the admitted resource allocation model.");
			}
		}
		Result.mExecutionOrder = Search.mBestOrder;
		Result.mAllocatedBytes = Search.mBestPlan.mTotalBytes;
		uint64_t UniqueBytes = 0;
		for (const auto& Q : Search.mBestPlan.mRequests)
		{
			if (Q.mbUsed && !Q.mExternalBuffer && !Q.mExternalTexture && !Q.mExternalAccelerationStructure)
			{
				const uint64_t Copies = Q.mbPersistent ? 1 : G.mOptions.mFramesInFlight;
				if (Q.mRequirements.mSize > UINT64_MAX / Copies ||
				    !AddBytes(UniqueBytes, Q.mRequirements.mSize * Copies))
				{
					UniqueBytes = UINT64_MAX;
				}
			}
		}
		Result.mAliasedBytes =
		    UniqueBytes >= Search.mBestPlan.mOwnedBytes ? UniqueBytes - Search.mBestPlan.mOwnedBytes : 0;
		for (const auto& E : Search.mBestPlan.mAliasEdges)
		{
			auto P = Search.mNodes[Search.mIndices.at(E.mProducer)], C = Search.mNodes[Search.mIndices.at(E.mConsumer)];
			Result.mMemoryDependencies.push_back({P, C});
		}
		AssignQueues(G, Result);
		// Publish only a fully materialized, lowered plan. The previous logical compilation remains available on failure.
		auto Previous = G.mCompile;
		G.mCompile = Result;
		if (G.mDevice)
		{
			// Edits are quiescent. Retire the previous transient slot before committing another
			// allocation plan, so compilation itself cannot temporarily double the graph budget.
			G.mRuntime.reset();
			G.mDevice->RunGarbageCollection();
			if (auto S = FArdaInductorRuntime::Prepare(G, eastl::move(Search.mBestPlan)); !S)
			{
				G.mCompile = eastl::move(Previous);
				return S;
			}
			G.mPersistentBuffers.resize(G.mResources.size());
			G.mPersistentTextures.resize(G.mResources.size());
			for (size_t I = 0; I < G.mResources.size(); ++I)
			{
				if (G.mResources[I].mbPersistent)
				{
					G.mPersistentBuffers[I] = G.mRuntime->mFrames.front()->mMemory.mBuffers[I];
					G.mPersistentTextures[I] = G.mRuntime->mFrames.front()->mMemory.mTextures[I];
				}
			}
		}
		return {};
	}
}
