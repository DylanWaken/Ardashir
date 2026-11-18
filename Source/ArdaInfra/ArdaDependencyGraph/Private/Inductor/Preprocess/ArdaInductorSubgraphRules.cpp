#include "Inductor/Preprocess/ArdaInductorSubgraphRules.h"

#include "Inductor/Preprocess/ArdaInductorSubgraphRulesInternal.h"
#include "Inductor/Rules/ArdaInductorGraphInternal.h"

#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaGraphEditOrdering.h"
#include "Graph/Node/ArdaSubgraph.h"
#include "Graph/Resources/ArdaGraphResourceValidation.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <exception>

namespace arda
{
	namespace
	{
		FArdaRHIStatus MergeSubgraphUsage(FArdaDependencyResourceDesc& Storage,
		    const FArdaDependencyResourceDesc& Declaration)
		{
			if (Storage.mbTexture)
			{
				auto& Actual = Storage.mTexture;
				const auto& Required = Declaration.mTexture;
				const auto Usage = ArdaExplicitResourceUsage(Required.mUsage);
				if (Storage.IsExternal())
				{
					if ((Actual.mUsage & Usage) != Usage || (Required.mbCudaInterop && !Actual.mbCudaInterop))
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
						    "Subgraph output requires usage unavailable on its imported texture storage.");
					}
				}
				else
				{
					Actual.mUsage |= Required.mUsage;
					Actual.mbCudaInterop |= Required.mbCudaInterop;
				}
			}
			else
			{
				auto& Actual = Storage.mBuffer;
				const auto& Required = Declaration.mBuffer;
				const auto Usage = ArdaExplicitResourceUsage(Required.mUsage);
				if (Storage.IsExternal())
				{
					if ((Actual.mUsage & Usage) != Usage || (Required.mbCudaInterop && !Actual.mbCudaInterop))
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
						    "Subgraph output requires usage unavailable on its imported buffer storage.");
					}
				}
				else
				{
					Actual.mUsage |= Required.mUsage;
					Actual.mbCudaInterop |= Required.mbCudaInterop;
				}
			}
			return {};
		}

		bool ContainsSubgraphAccess(const FArdaDependencyAccess& Port,
		    const FArdaDependencyAccess& Access,
		    const FArdaDependencyResourceDesc& Resource)
		{
			if (Resource.mExternalKind == EArdaGraphExternalResourceKind::AccelerationStructure)
			{
				return true;
			}
			const auto Contains = [](uint64_t OuterFirst, uint64_t OuterCount, uint64_t First, uint64_t Count)
			{
				return First >= OuterFirst && First - OuterFirst <= OuterCount &&
				    Count <= OuterCount - (First - OuterFirst);
			};
			if (!Resource.mbTexture)
			{
				const auto Outer = Port.mBufferRange.Resolve(Resource.mBuffer);
				const auto Inner = Access.mBufferRange.Resolve(Resource.mBuffer);
				return Contains(Outer.mByteOffset, Outer.mByteSize, Inner.mByteOffset, Inner.mByteSize);
			}
			const auto Outer = Port.mTextureRange.Resolve(Resource.mTexture);
			const auto Inner = Access.mTextureRange.Resolve(Resource.mTexture);
			return Contains(Outer.mBaseMipLevel, Outer.mMipLevelCount, Inner.mBaseMipLevel, Inner.mMipLevelCount) &&
			    Contains(Outer.mBaseArraySlice,
			        Outer.mArraySliceCount,
			        Inner.mBaseArraySlice,
			        Inner.mArraySliceCount) &&
			    Contains(Outer.mBasePlane, Outer.mPlaneCount, Inner.mBasePlane, Inner.mPlaneCount);
		}
	}

	FArdaRHIStatus FArdaSubgraphPreprocessor::Expand(FArdaDependencyGraph& Graph)
	{
		auto& G = *Graph.mImpl;
		const auto Invalid = [](const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		};
		if (!G.mbEditing || !G.mbPreprocessing || G.mbExpandingSubgraphs)
		{
			return Invalid(
			    "Subgraph expansion requires an active preprocessing rule and cannot recurse through rule application.");
		}
		auto Roots = G.mTopology.GetNodes();
		if (eastl::none_of(Roots.begin(),
		        Roots.end(),
		        [&](auto Handle)
		        {
			        const auto& Definition = G.mTopology.TryGetNode(Handle)->mPayload.mDefinition;
			        return Definition && Definition->mExpand;
		        }))
		{
			return {};
		}
		if (!G.mTopology.TopologicalSort().IsAcyclic())
		{
			return Invalid("Subgraph expansion requires an acyclic input graph.");
		}

		struct FArdaSubgraphExpansionScope
		{
			bool& mFlag;
			bool& mEditing;

			explicit FArdaSubgraphExpansionScope(bool& Flag, bool& Editing)
			    : mFlag(Flag),
			      mEditing(Editing)
			{
				mFlag = true;
				mEditing = true;
			}

			~FArdaSubgraphExpansionScope()
			{
				mFlag = false;
				mEditing = false;
			}
		} Scope(G.mbExpandingSubgraphs, G.mbPreprocessEditing);

		const auto Remap = [&](FArdaDependencyResourceHandle From, FArdaDependencyResourceHandle To)
		{
			for (auto& Mapping : G.mCompiledResourceRemaps)
			{
				if (Mapping.first == From || Mapping.second == From)
				{
					Mapping.second = To;
				}
			}
			G.mCompiledResourceRemaps[From] = To;
		};
		eastl::sort(Roots.begin(),
		    Roots.end(),
		    [&](auto A, auto B)
		    {
			    return G.mTopology.TryGetNode(A)->mPayload.mAttachmentOrder <
			        G.mTopology.TryGetNode(B)->mPayload.mAttachmentOrder;
		    });
		eastl::function<FArdaRHIStatus(FArdaGraphNodeHandle, uint32_t, eastl::vector<FArdaGraphNodeHandle>&)>
		    ExpandNode;
		ExpandNode = [&](FArdaGraphNodeHandle Handle,
		                 uint32_t Depth,
		                 eastl::vector<FArdaGraphNodeHandle>& Leaves) -> FArdaRHIStatus
		{
			const auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
			if (!Node.mDefinition || !Node.mDefinition->mExpand)
			{
				Leaves.push_back(Handle);
				return {};
			}
			// Retain the interface: recording children can invalidate topology storage.
			const auto Parent = Node;
			const auto ParentDescription = Parent.mDesc;
			if (Depth >= 64)
			{
				return Invalid("Subgraph recursion exceeded 64 levels; check for a recursive definition cycle.");
			}
			const auto FirstOrder = G.mNextAttachmentOrder;
			const auto FirstResource = G.mResources.size();
			const auto FirstManualEdge = G.mManualEdges.size();
			FArdaGraphEdit Edit(Graph, Parent.mName + "/");
			Edit.mSubgraphAccesses = ParentDescription.mAccesses;
			FArdaSubgraphContext Context(Edit, Parent.mOutputs);
			try
			{
				Edit.ReportError(Parent.mDefinition->mExpand(Context, Parent.mSourceParameters.get()));
			}
			catch (const std::exception& Error)
			{
				Edit.ReportError(FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, Error.what()));
			}
			catch (...)
			{
				Edit.ReportError(
				    FArdaRHIStatus::Error(EArdaRHIResult::BackendFailure, "Subgraph Build threw an exception."));
			}
			if (auto Status = Context.ValidateOutputs(); !Status)
			{
				Status.mMessage = "Subgraph '" + Parent.mName + "': " + Status.mMessage;
				return Status;
			}
			for (size_t Index = FirstResource; Index < G.mResources.size(); ++Index)
			{
				const auto& Resource = G.mResources[Index];
				if (Resource.IsExternal())
				{
					return Invalid("Subgraph native resources must be supplied through declared parent inputs.");
				}
			}
			for (size_t Index = FirstManualEdge; Index < G.mManualEdges.size(); ++Index)
			{
				const auto& Edge = G.mManualEdges[Index];
				if (G.mTopology.TryGetNode(Edge.first)->mPayload.mAttachmentOrder < FirstOrder ||
				    G.mTopology.TryGetNode(Edge.second)->mPayload.mAttachmentOrder < FirstOrder)
				{
					return Invalid("Subgraph ordering may reference only operations created by its content.");
				}
			}
			eastl::vector<FArdaDependencyResourceHandle> PassthroughOutputs;
			for (const auto& Binding : Context.mOutputBindings)
			{
				const auto Destination = G.ResolveResource(Binding.first);
				const auto Source = G.ResolveResource(Binding.second);
				if (Destination == Source)
				{
					continue;
				}
				const bool bPassthrough = Source.mIndex < FirstResource;
				if (bPassthrough &&
				    !eastl::any_of(Context.mInputs.begin(),
				        Context.mInputs.end(),
				        [&](const auto& Input)
				        {
					        return G.ResolveResource(Input.mResource) == Source;
				        }))
				{
					return Invalid(
					    "A published subgraph output must be parent storage, a declared input or a new content transient.");
				}
				auto& DestinationDescription = G.mResources[Destination.mIndex];
				auto& SourceDescription = G.mResources[Source.mIndex];
				if ((!bPassthrough && SourceDescription.IsExternal()) ||
				    SourceDescription.mbTexture != DestinationDescription.mbTexture ||
				    (SourceDescription.mExternalKind == EArdaGraphExternalResourceKind::AccelerationStructure) !=
				        (DestinationDescription.mExternalKind ==
				            EArdaGraphExternalResourceKind::AccelerationStructure) ||
				    (!bPassthrough &&
				        (SourceDescription.mbPersistent != DestinationDescription.mbPersistent ||
				            SourceDescription.mBufferInitializer != DestinationDescription.mBufferInitializer)))
				{
					return Invalid("Subgraph outputs require compatible transient storage.");
				}
				if (SourceDescription.mbTexture)
				{
					const auto& A = SourceDescription.mTexture;
					auto& B = DestinationDescription.mTexture;
					if (A.mDimension != B.mDimension || A.mWidth != B.mWidth || A.mHeight != B.mHeight ||
					    A.mDepth != B.mDepth || A.mArraySize != B.mArraySize || A.mMipLevels != B.mMipLevels ||
					    A.mFormat != B.mFormat || A.mSampleCount != B.mSampleCount)
					{
						return Invalid("Subgraph output texture shape differs from its parent declaration.");
					}
				}
				else
				{
					const auto& A = SourceDescription.mBuffer;
					auto& B = DestinationDescription.mBuffer;
					if (A.mByteSize != B.mByteSize || A.mStructureStride != B.mStructureStride ||
					    A.mFormat != B.mFormat || A.mCpuAccess != B.mCpuAccess)
					{
						return Invalid("Subgraph output buffer shape differs from its parent declaration.");
					}
				}
				if (auto Status = bPassthrough ? MergeSubgraphUsage(SourceDescription, DestinationDescription)
				                               : MergeSubgraphUsage(DestinationDescription, SourceDescription);
				    !Status)
				{
					return Status;
				}
				if (bPassthrough)
				{
					if (DestinationDescription.IsExternal() || DestinationDescription.mbPersistent ||
					    DestinationDescription.mName.find("Node output ") != 0)
					{
						return Invalid("Input passthrough requires a node-owned transient output declaration.");
					}
					SourceDescription.mbOutput |= DestinationDescription.mbOutput;
					DestinationDescription.mbOutput = false;
					PassthroughOutputs.push_back(Destination);
					Remap(Destination, Source);
				}
				else
				{
					DestinationDescription.mbOutput |= SourceDescription.mbOutput;
					SourceDescription.mbOutput = false;
					Remap(Source, Destination);
				}
			}
			eastl::vector<FArdaGraphNodeHandle> Children;
			for (auto Child : G.mTopology.GetNodes())
			{
				auto& Node = G.mTopology.TryGetNode(Child)->mPayload;
				if (Node.mAttachmentOrder < FirstOrder)
				{
					continue;
				}
				Children.push_back(Child);
				auto& Description = G.mTopology.TryGetNode(Child)->mPayload.mDesc;
				Description.mbSideEffect |= ParentDescription.mbSideEffect;
				G.NormalizeDescription(Description);
				const auto HasLocalStageProducer = [&](FArdaGraphStageValue Input)
				{
					if (!Input)
					{
						return true;
					}
					const auto* Producer = G.mTopology.TryGetNode(G.ResolveNode(Input.GetProducer()));
					return Producer && Producer->mPayload.mAttachmentOrder >= FirstOrder;
				};
				if (!HasLocalStageProducer(Description.mGraphicsInput) ||
				    !eastl::all_of(Description.mRayInputs.begin(), Description.mRayInputs.end(), HasLocalStageProducer))
				{
					return Invalid("Subgraph logical stage inputs must be produced inside the same content body.");
				}
				for (auto& Output : Node.mOutputs)
				{
					Output.mResource = G.ResolveResource(Output.mResource);
				}
				for (const auto& Access : Description.mAccesses)
				{
					const auto Resource = G.ResolveResource(Access.mResource);
					if (Resource.mIndex >= FirstResource)
					{
						continue;
					}
					const bool bDeclared = eastl::any_of(ParentDescription.mAccesses.begin(),
					    ParentDescription.mAccesses.end(),
					    [&](const auto& Port)
					    {
						    return eastl::find(PassthroughOutputs.begin(), PassthroughOutputs.end(), Port.mResource) ==
						        PassthroughOutputs.end() &&
						        G.ResolveResource(Port.mResource) == Resource &&
						        (Port.mAccess != EArdaDependencyAccess::Read || Port.mAccess == Access.mAccess) &&
						        ContainsSubgraphAccess(Port, Access, G.mResources[Resource.mIndex]);
					    });
					if (!bDeclared)
					{
						return Invalid(
						    "Subgraph content accessed an undeclared parent resource or exceeded its access role.");
					}
				}
			}
			for (const auto& Output : Parent.mOutputs)
			{
				if (eastl::find(PassthroughOutputs.begin(), PassthroughOutputs.end(), Output.mResource) !=
				    PassthroughOutputs.end())
				{
					continue;
				}
				const bool bWritten = eastl::any_of(Children.begin(),
				    Children.end(),
				    [&](auto Child)
				    {
					    const auto& Accesses = G.mTopology.TryGetNode(Child)->mPayload.mDesc.mAccesses;
					    return eastl::any_of(Accesses.begin(),
					        Accesses.end(),
					        [&](const auto& Access)
					        {
						        return G.ResolveResource(Access.mResource) == G.ResolveResource(Output.mResource) &&
						            Access.mAccess != EArdaDependencyAccess::Read;
					        });
				    });
				if (!bWritten)
				{
					return Invalid("Subgraph declared an output that no content operation writes.");
				}
			}
			eastl::sort(Children.begin(),
			    Children.end(),
			    [&](auto A, auto B)
			    {
				    return G.mTopology.TryGetNode(A)->mPayload.mAttachmentOrder <
				        G.mTopology.TryGetNode(B)->mPayload.mAttachmentOrder;
			    });
			eastl::vector<FArdaGraphNodeHandle> Expanded;
			for (auto Child : Children)
			{
				if (auto Status = ExpandNode(Child, Depth + 1, Expanded); !Status)
				{
					return Status;
				}
			}
			eastl::vector<FArdaGraphNodeHandle> Before;
			eastl::vector<FArdaGraphNodeHandle> After;
			for (const auto& Edge : G.mManualEdges)
			{
				if (Edge.second == Handle)
				{
					Before.push_back(Edge.first);
				}
				if (Edge.first == Handle)
				{
					After.push_back(Edge.second);
				}
			}
			// Custom preprocessing may add explicit topology edges without authoring through Edit.Order.
			for (const auto Edge : G.mTopology.GetIncomingEdges(Handle))
			{
				const auto Producer = G.mTopology.TryGetEdge(Edge)->mFrom;
				if (eastl::find(Before.begin(), Before.end(), Producer) == Before.end())
				{
					Before.push_back(Producer);
				}
			}
			for (const auto Edge : G.mTopology.GetOutgoingEdges(Handle))
			{
				const auto Consumer = G.mTopology.TryGetEdge(Edge)->mTo;
				if (eastl::find(After.begin(), After.end(), Consumer) == After.end())
				{
					After.push_back(Consumer);
				}
			}
			G.mManualEdges.erase(eastl::remove_if(G.mManualEdges.begin(),
			                         G.mManualEdges.end(),
			                         [&](const auto& Edge)
			                         {
				                         return Edge.first == Handle || Edge.second == Handle;
			                         }),
			    G.mManualEdges.end());
			for (auto& Edit : G.mCommittedEdits)
			{
				const auto Position = eastl::find(Edit.mNodes.begin(), Edit.mNodes.end(), Handle);
				if (Position != Edit.mNodes.end())
				{
					const auto Offset = Position - Edit.mNodes.begin();
					Edit.mNodes.erase(Position);
					Edit.mNodes.insert(Edit.mNodes.begin() + Offset, Expanded.begin(), Expanded.end());
				}
			}
			G.mTopology.RemoveNode(Handle);
			G.mNames.erase(Parent.mName);
			G.mSubgraphAdmissions.push_back({Parent.mName,
			    Parent.mDefinition,
			    Parent.mSourceParameters,
			    ParentDescription.mRequirements,
			    Parent.mArchivedParameters,
			    Parent.mExternalParameterNames,
			    Parent.mbUnboundExternalParameters});
			G.mNodePreparations.erase(Handle);
			const auto Connect = [&](auto From, auto To)
			{
				G.mManualEdges.push_back({From, To});
				(void)G.mTopology.AddEdge(From, To, {});
			};
			for (auto Predecessor : Before)
			{
				for (auto Child : Expanded)
				{
					Connect(Predecessor, Child);
				}
				if (Expanded.empty())
				{
					for (auto Successor : After)
					{
						Connect(Predecessor, Successor);
					}
				}
			}
			for (auto Successor : After)
			{
				for (auto Child : Expanded)
				{
					Connect(Child, Successor);
				}
			}
			Leaves.insert(Leaves.end(), Expanded.begin(), Expanded.end());
			return {};
		};
		eastl::vector<FArdaGraphNodeHandle> Order;
		for (auto Root : Roots)
		{
			if (auto Status = ExpandNode(Root, 0, Order); !Status)
			{
				return Status;
			}
		}
		uint64_t AttachmentOrder = 1;
		for (auto Node : Order)
		{
			auto& Payload = G.mTopology.TryGetNode(Node)->mPayload;
			Payload.mAttachmentOrder = AttachmentOrder++;
			G.NormalizeDescription(Payload.mDesc);
			for (auto& Output : Payload.mOutputs)
			{
				Output.mResource = G.ResolveResource(Output.mResource);
			}
		}
		G.mNextAttachmentOrder = AttachmentOrder;
		NormalizeArdaGraphEditRevisions(G);
		return {};
	}

	FArdaRHIStatus FArdaExpandSubgraphsRule::Preprocess(FArdaInductorGraph& Graph) const
	{
		auto* Owner = FArdaInductorGraphAccess::Owner(Graph);
		if (!Owner)
		{
			for (const auto Handle : Graph.GetTopology().GetNodes())
			{
				const auto& Definition = Graph.GetTopology().TryGetNode(Handle)->mPayload.mDefinition;
				if (Definition && Definition->mExpand)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
					    "Subgraph expansion requires the preprocessor-owned graph authoring context.");
				}
			}
			return {};
		}
		return FArdaSubgraphPreprocessor::Expand(*Owner);
	}
}
