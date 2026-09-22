#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"

namespace arda::inductor_detail
{
	FArdaRHIStatus FArdaScheduleSearch::Initialize()
	{
		if (auto Status = ValidateResourceUsage(mGraph); !Status)
		{
			return Status;
		}
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
			Q.mKind = D.mExternalAccelerationStructure ? EArdaMemoryKind::AccelerationStructure
			    : D.mbTexture                          ? EArdaMemoryKind::Texture
			                                           : EArdaMemoryKind::Buffer;
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
				FArdaMemoryRequest Scratch;
				Scratch.mIdentifier = static_cast<uint32_t>(mRequests.size());
				Scratch.mbUsed = true;
				Scratch.mKind = EArdaMemoryKind::Buffer;
				Scratch.mBufferDesc.mByteSize = N.mDesc.mTransientWorkspaceBytes;
				Scratch.mBufferDesc.mUsage = EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::Raw;
				Scratch.mTextureDesc.mUsage = EArdaRHITextureUsage::None;
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
			if (auto S = DescribeArdaMemory(*mGraph.mDevice, mRequests); !S)
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
					    ? eastl::max<uint64_t>(1, Q.mExternalAccelerationStructure->GetMemoryAllocationInfo().mByteSize)
					    : Q.mBufferDesc.mByteSize;
					if (Q.mKind == EArdaMemoryKind::Texture)
					{
						const auto& D = Q.mTextureDesc;
						const auto& F = GetArdaRHIFormatInfo(D.mFormat);
						Bytes = 0;
						for (uint32_t M = 0; M < D.mMipLevels; ++M)
						{
							uint64_t B =
							    (uint64_t(GetArdaRHITextureMipExtent(D.mWidth, M)) + F.mBlockWidth - 1) / F.mBlockWidth;
							for (uint64_t X :
							    {uint64_t((uint64_t(GetArdaRHITextureMipExtent(D.mHeight, M)) + F.mBlockHeight - 1) /
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

}

namespace arda
{
	FArdaRHIStatus FArdaPlanMemoryRule::Apply(const FArdaInductorGraph& Graph, FArdaInductorCompileResult& Result) const
	{
		using namespace inductor_detail;
		auto& G = FArdaInductorGraphAccess::State(Graph);
		if (auto S = ValidateNodeOrder(G, Result); !S)
		{
			return S;
		}
		auto Planned = Result;
		Planned.mWorkspaceResourceIds.clear();
		Planned.mSchedulesExamined = 0;
		FArdaScheduleSearch Search(G, Result.mExecutionOrder, Planned);
		if (auto S = Search.Initialize(); !S)
		{
			return S;
		}
		eastl::vector<size_t> Order;
		for (size_t I = 0; I < Search.mNodes.size(); ++I)
		{
			Order.push_back(I);
		}
		if (mbAllowSerialization)
		{
			Search.mObjective = EArdaInductorObjective::Memory;
		}
		Search.Evaluate(Order,
		    mbAllowSerialization || G.mOptions.mMaxVramBytes || Search.mObjective == EArdaInductorObjective::Memory);
		if (auto S = PublishSchedule(Search, Planned); !S)
		{
			return S;
		}
		Planned.mSchedulesExamined = eastl::max(Result.mSchedulesExamined, Planned.mSchedulesExamined);
		Planned.mSearchStatesExamined = Result.mSearchStatesExamined;
		Planned.mbSearchComplete = Result.mbSearchComplete;
		Planned.mbSearchExhausted = Result.mbSearchExhausted;
		Result = eastl::move(Planned);
		return {};
	}
}
