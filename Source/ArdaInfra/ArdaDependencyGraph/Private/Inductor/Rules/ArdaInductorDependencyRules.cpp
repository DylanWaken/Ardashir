#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "Inductor/Rules/ArdaDependencyHazards.h"
#include "Inductor/Rules/ArdaInductorResourceStates.h"

namespace arda::inductor_detail
{
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

	FArdaRHIStatus ValidateResourceAccesses(const FArdaDependencyGraph::FArdaImpl& G, bool AllowAutomaticUsage)
	{
		if (!AllowAutomaticUsage)
		{
			if (auto Status = ValidateResourceUsage(G); !Status)
			{
				return Status;
			}
		}
		for (auto H : G.mTopology.GetNodes())
		{
			const auto& Node = G.mTopology.TryGetNode(H)->mPayload;
			if (!Node.mDefinition)
			{
				return Error("A graph node has no executable definition.");
			}
			if (Node.mDesc.mbPipelineStageOnly && Node.mDesc.mGraphicsStage == EArdaGraphicsStage::None &&
			    Node.mDesc.mRayStage == EArdaRHIShaderStage::None &&
			    (!Node.mDesc.mAccesses.empty() || !Node.mDesc.mColorTargets.empty() || Node.mDesc.mDepthTarget ||
			        !Node.mDesc.mPipelines.empty()))
			{
				return Error(
				    "Stage-only nodes may contribute shaders but cannot access physical resources or request pipelines.");
			}
			eastl::unordered_map<uint32_t, EArdaRHIResourceState> BufferStates;
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
					    ((A.mAccess != EArdaDependencyAccess::Read) !=
					        (A.mState == EArdaRHIResourceState::AccelStructWrite)) ||
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
					auto [State, Inserted] = BufferStates.emplace(A.mResource.mIndex, A.mState);
					if (!Inserted && State->second != A.mState)
					{
						if (IsWriteState(State->second) || IsWriteState(A.mState))
						{
							return Error(
							    "One node requests incompatible buffer states. Disjoint ranges still share native buffer state.");
						}
						State->second = State->second | A.mState;
					}
				}
				else
				{
					if (!StrictTextureRange(A.mTextureRange, D.mTexture))
					{
						return Error("Node texture range is empty or exceeds its logical resource.");
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
				if (!(AllowAutomaticUsage && HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::AutoDeduction)) &&
				    (Depth ? !HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::DepthStencil)
				           : !HasAnyFlags(Desc.mUsage, EArdaRHITextureUsage::RenderTarget)))
				{
					return false;
				}
				return eastl::any_of(Node.mDesc.mAccesses.begin(),
				    Node.mDesc.mAccesses.end(),
				    [&](const auto& Access)
				    {
					    const bool State = Depth ? ((Access.mAccess != EArdaDependencyAccess::Read &&
					                                    Access.mState == EArdaRHIResourceState::DepthWrite) ||
					                                   (Access.mAccess == EArdaDependencyAccess::Read &&
					                                       Access.mState == EArdaRHIResourceState::DepthRead))
					                             : (Access.mAccess != EArdaDependencyAccess::Read &&
					                                   Access.mState == EArdaRHIResourceState::RenderTarget);
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

		return {};
	}
}

namespace arda
{
	FArdaRHIStatus FArdaValidateResourceAccessesRule::Apply(FArdaInductorGraph& Graph) const
	{
		return inductor_detail::ValidateResourceAccesses(FArdaInductorGraphAccess::State(Graph));
	}

	FArdaRHIStatus FArdaResolveDependenciesRule::Apply(FArdaInductorGraph& Graph) const
	{
		auto& G = FArdaInductorGraphAccess::State(Graph);
		if (auto S = inductor_detail::ValidateResourceAccesses(G); !S)
		{
			return S;
		}
		const auto Edges = G.mTopology.GetEdges();
		for (auto E : Edges)
		{
			const auto& Payload = G.mTopology.TryGetEdge(E)->mPayload;
			if (Payload.mbResource || Payload.mbHazard)
			{
				G.mTopology.RemoveEdge(E);
			}
		}
		for (auto E : G.mManualEdges)
		{
			if (G.mTopology.ContainsNode(E.first) && G.mTopology.ContainsNode(E.second))
			{
				(void)G.mTopology.AddEdge(E.first, E.second, {});
			}
		}
		return ResolveArdaDependencyResourceEdges(G, G.mTopology);
	}

	FArdaRHIStatus FArdaValidateAcyclicRule::Apply(FArdaInductorGraph& Graph) const
	{
		return Graph.GetTopology().TopologicalSort().IsAcyclic()
		    ? FArdaRHIStatus{}
		    : inductor_detail::Error("Dependency cycle in preprocessed graph.");
	}
}
