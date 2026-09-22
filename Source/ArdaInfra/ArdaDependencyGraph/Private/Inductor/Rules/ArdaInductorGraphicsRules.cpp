#include "Inductor/Compiler/ArdaInductorCompilerInternal.h"
#include "Inductor/Rules/ArdaInductorBindingRules.h"

namespace arda
{
	namespace
	{
		EArdaRHIShaderStage ShaderStage(EArdaGraphicsStage Stage)
		{
			switch (Stage)
			{
			case EArdaGraphicsStage::Vertex:
				return EArdaRHIShaderStage::Vertex;
			case EArdaGraphicsStage::Hull:
				return EArdaRHIShaderStage::Hull;
			case EArdaGraphicsStage::Domain:
				return EArdaRHIShaderStage::Domain;
			case EArdaGraphicsStage::Geometry:
				return EArdaRHIShaderStage::Geometry;
			case EArdaGraphicsStage::Amplification:
				return EArdaRHIShaderStage::Amplification;
			case EArdaGraphicsStage::Mesh:
				return EArdaRHIShaderStage::Mesh;
			case EArdaGraphicsStage::Pixel:
				return EArdaRHIShaderStage::Pixel;
			default:
				return EArdaRHIShaderStage::None;
			}
		}

		bool AcceptsProducer(EArdaGraphicsStage Stage, EArdaGraphicsStage Producer)
		{
			switch (Stage)
			{
			case EArdaGraphicsStage::Hull:
				return Producer == EArdaGraphicsStage::Vertex;
			case EArdaGraphicsStage::Domain:
				return Producer == EArdaGraphicsStage::Hull;
			case EArdaGraphicsStage::Geometry:
				return Producer == EArdaGraphicsStage::Vertex || Producer == EArdaGraphicsStage::Domain;
			case EArdaGraphicsStage::Mesh:
				return Producer == EArdaGraphicsStage::Amplification;
			case EArdaGraphicsStage::Raster:
				return Producer == EArdaGraphicsStage::Vertex || Producer == EArdaGraphicsStage::Domain ||
				    Producer == EArdaGraphicsStage::Geometry || Producer == EArdaGraphicsStage::Mesh;
			case EArdaGraphicsStage::Pixel:
				return Producer == EArdaGraphicsStage::Raster;
			default:
				return false;
			}
		}

		FArdaRHIStatus ValidateStage(const FArdaDependencyGraph::FArdaImpl& Graph, const FArdaDependencyNode& Node)
		{
			using namespace inductor_detail;
			const auto& D = Node.mDesc;
			const auto Stage = D.mGraphicsStage;
			if (D.mRayStage != EArdaRHIShaderStage::None || !D.mRayInputs.empty())
			{
				return Error("A graphics stage cannot also declare ray-tracing stage state.");
			}
			const auto ExpectedShader = ShaderStage(Stage);
			if (ExpectedShader == EArdaRHIShaderStage::None && Stage != EArdaGraphicsStage::Raster)
			{
				return Error("Unknown graphics stage.");
			}
			if (!Node.mDefinition || Node.mDefinition->mKind != EArdaDependencyNodeKind::Graphics ||
			    !Node.mDefinition->mConfigureGraphics || !Node.mDefinition->mCloneGraphicsParameters)
			{
				return Error("Graphics stages require a graphics definition and a ConfigureGraphics callback.");
			}
			if (!D.mPipelines.empty() || !D.mShaderTables.empty() || D.mWorkspaceBytes || D.mTransientWorkspaceBytes)
			{
				return Error("Graphics stages cannot request independent pipelines, ray tables or private workspaces.");
			}
			if (Stage != EArdaGraphicsStage::Pixel && (D.mbSideEffect || !D.mColorTargets.empty() || D.mDepthTarget))
			{
				return Error("Only a pixel terminal may declare draw side effects and framebuffer attachments.");
			}
			if (Stage != EArdaGraphicsStage::Vertex && D.mGraphicsInputLayout)
			{
				return Error("Only a vertex stage may declare the vertex input layout.");
			}
			const bool EmptyInput =
			    !D.mGraphicsInput.GetProducer() && D.mGraphicsInput.GetKind() == EArdaGraphStageValueKind::None;
			if (Stage == EArdaGraphicsStage::Vertex || Stage == EArdaGraphicsStage::Amplification)
			{
				if (!EmptyInput)
				{
					return Error("Vertex and amplification entry stages cannot consume another graphics stage.");
				}
			}
			else if (Stage != EArdaGraphicsStage::Mesh || !EmptyInput)
			{
				const auto* Producer = Graph.mTopology.TryGetNode(D.mGraphicsInput.GetProducer());
				if (!Producer || !AcceptsProducer(Stage, Producer->mPayload.mDesc.mGraphicsStage) ||
				    D.mGraphicsInput.GetKind() != GetArdaGraphicsStageOutput(Producer->mPayload.mDesc.mGraphicsStage))
				{
					return Error(
					    "Graphics state has a missing, foreign or incompatible producer for its pipeline stage.");
				}
			}
			uint32_t ShaderCount = 0, ConfigurationCount = 0;
			for (const auto& Contribution : D.mPipelineStages)
			{
				if (!Contribution.mGroup.empty() || !Contribution.mExportName.empty() ||
				    !Contribution.mHitGroupName.empty() || Contribution.mLocalBindingLayout)
				{
					return Error("Typed graphics stages cannot declare named pipeline branches or ray exports.");
				}
				if (Contribution.mShader)
				{
					if (Stage == EArdaGraphicsStage::Raster || Contribution.mShader->GetStage() != ExpectedShader)
					{
						return Error("A graphics stage contributes a shader for a different stage.");
					}
					++ShaderCount;
				}
				if (Contribution.mConfiguration)
				{
					const auto& C = *Contribution.mConfiguration;
					const auto& G = C.mGraphics.mDesc;
					const auto& M = C.mMeshlet.mDesc;
					if (Stage != EArdaGraphicsStage::Raster ||
					    (C.mKind != EArdaPipelineStateKind::Graphics && C.mKind != EArdaPipelineStateKind::Meshlet) ||
					    G.mVertexShader || G.mPixelShader || G.mGeometryShader || G.mHullShader || G.mDomainShader ||
					    G.mInputLayout || M.mAmplificationShader || M.mMeshShader || M.mPixelShader ||
					    ++ConfigurationCount > 1)
					{
						return Error(
						    "Raster state accepts one graphics configuration without shaders or a vertex input layout.");
					}
					if (C.mKind == EArdaPipelineStateKind::Meshlet
					        ? M.mTopology >= EArdaRHIPrimitiveTopology::PatchList
					        : G.mTopology > EArdaRHIPrimitiveTopology::PatchList ||
					            (G.mTopology == EArdaRHIPrimitiveTopology::PatchList
					                    ? G.mPatchControlPoints == 0 || G.mPatchControlPoints > 32
					                    : G.mPatchControlPoints != 0))
					{
						return Error(
						    "Raster topology or tessellation control-point count is invalid for its pipeline family.");
					}
				}
			}
			if (ShaderCount != (Stage == EArdaGraphicsStage::Raster ? 0u : 1u))
			{
				return Error("A programmable graphics node must contribute exactly one shader of its declared stage.");
			}
			if (Stage == EArdaGraphicsStage::Raster && (!D.mShaderBindings.empty() || !D.mBindlessTables.empty()))
			{
				return Error("Rasterization has no programmable shader bindings.");
			}
			for (const auto& B : D.mShaderBindings)
			{
				if (!B.mMetadata || !B.mMetadata->GetStatus())
				{
					return Error("Graphics stage shader metadata is invalid.");
				}
				eastl::vector<FArdaFlattenedShaderParameterMember> Members;
				B.mMetadata->GetFlattenedMembers(Members);
				for (const auto& M : Members)
				{
					if (M.mMember->mKind != EArdaShaderParameterKind::Value && M.mMember->mVisibility != ExpectedShader)
					{
						return Error("Graphics stage shader parameters must use the visibility of their owning stage.");
					}
				}
			}
			for (const auto& B : D.mBindlessTables)
			{
				if (B.mLayout.mVisibility != ExpectedShader)
				{
					return Error("Graphics stage bindless tables must use the visibility of their owning stage.");
				}
			}
			return {};
		}

		template <class T>
		void Append(eastl::vector<T>& Destination, const eastl::vector<T>& Source)
		{
			Destination.insert(Destination.end(), Source.begin(), Source.end());
		}

		bool Overlaps(const FArdaDependencyAccess& A,
		    const FArdaDependencyAccess& B,
		    const FArdaDependencyResourceDesc& Resource)
		{
			if (Resource.mExternalAccelerationStructure)
			{
				return true;
			}
			const auto Intersects = [](uint64_t StartA, uint64_t CountA, uint64_t StartB, uint64_t CountB)
			{
				return StartA < StartB + CountB && StartB < StartA + CountA;
			};
			if (!Resource.mbTexture)
			{
				const auto RA = A.mBufferRange.Resolve(Resource.mBuffer), RB = B.mBufferRange.Resolve(Resource.mBuffer);
				return Intersects(RA.mByteOffset, RA.mByteSize, RB.mByteOffset, RB.mByteSize);
			}
			const auto RA = A.mTextureRange.Resolve(Resource.mTexture), RB = B.mTextureRange.Resolve(Resource.mTexture);
			return Intersects(RA.mBaseMipLevel, RA.mMipLevelCount, RB.mBaseMipLevel, RB.mMipLevelCount) &&
			    Intersects(RA.mBaseArraySlice, RA.mArraySliceCount, RB.mBaseArraySlice, RB.mArraySliceCount) &&
			    Intersects(RA.mBasePlane, RA.mPlaneCount, RB.mBasePlane, RB.mPlaneCount);
		}
	}

	FArdaRHIStatus FArdaAssembleGraphicsStagesRule::Apply(FArdaInductorGraph& Graph) const
	{
		using namespace inductor_detail;
		auto& G = FArdaInductorGraphAccess::State(Graph);
		eastl::unordered_map<uint32_t, FArdaDependencyNodeDesc> Authored;
		eastl::unordered_map<uint32_t, eastl::vector<FArdaDependencyAccess>> Accesses;
		bool Lowered = false;
		for (auto Handle : G.mTopology.GetNodes())
		{
			const auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
			if (Node.mDesc.mGraphicsStage == EArdaGraphicsStage::None)
			{
				if (Node.mDesc.mGraphicsInput.GetProducer() ||
				    Node.mDesc.mGraphicsInput.GetKind() != EArdaGraphStageValueKind::None)
				{
					return Error("Only a declared graphics stage may consume a logical stage value.");
				}
				continue;
			}
			if (Node.mbGraphicsLowered)
			{
				Lowered = true;
				continue;
			}
			if (auto S = ValidateStage(G, Node); !S)
			{
				S.mMessage = Node.mName + ": " + S.mMessage;
				return S;
			}
			Authored.emplace(Handle.GetIndex(), Node.mDesc);
		}
		if (Authored.empty())
		{
			return {};
		}
		if (Lowered)
		{
			return Error("Graphics assembly cannot mix authored and already lowered stages.");
		}
		// A declaration must belong to a complete draw or to an explicitly materialized
		// stage prefix. Validate this before culling so unused, incomplete pipelines are errors.
		eastl::unordered_set<uint32_t> Connected;
		for (auto Handle : G.mTopology.GetNodes())
		{
			const auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
			if (Authored.count(Handle.GetIndex()) &&
			    (Node.mDesc.mGraphicsStage == EArdaGraphicsStage::Pixel || Node.mbStageMaterialized))
			{
				auto Cursor = Handle;
				while (Cursor && Connected.insert(Cursor.GetIndex()).second)
				{
					Cursor = Authored.at(Cursor.GetIndex()).mGraphicsInput.GetProducer();
				}
			}
		}
		FArdaGraphNodeHandle Incomplete;
		for (auto Handle : G.mTopology.GetNodes())
		{
			if (!Authored.count(Handle.GetIndex()) || Connected.count(Handle.GetIndex()))
			{
				continue;
			}
			// Prefer the missing pixel consumer over its otherwise well-connected shader ancestors.
			if (!Incomplete || Authored.at(Handle.GetIndex()).mGraphicsStage == EArdaGraphicsStage::Raster)
			{
				Incomplete = Handle;
			}
		}
		if (Incomplete)
		{
			const auto& Node = G.mTopology.TryGetNode(Incomplete)->mPayload;
			const auto Stage = Node.mDesc.mGraphicsStage;
			const char* Missing = Stage == EArdaGraphicsStage::Raster ? "rasterization requires a pixel-stage consumer"
			    : Stage == EArdaGraphicsStage::Hull ? "hull output requires domain, rasterization and pixel stages"
			    : Stage == EArdaGraphicsStage::Amplification
			    ? "amplification output requires mesh, rasterization and pixel stages"
			    : "shader output requires rasterization and a pixel-stage consumer";
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    ("Incomplete graphics pipeline at '" + Node.mName + "': " + Missing +
			        " or explicit FetchBuffer materialization.")
			        .c_str());
		}
		// Validate every stage, including branches that culling will later discard. Bindings
		// are temporarily described on their owner so malformed dead resources cannot escape.
		for (auto Handle : G.mTopology.GetNodes())
		{
			auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
			if (!Authored.count(Handle.GetIndex()))
			{
				continue;
			}
			Node.mDesc.mbPipelineStageOnly = false;
			Node.mDesc.mPipelines.push_back({"default", 0, EArdaPipelineStateKind::Graphics});
			if (auto S = DescribeArdaShaderBindings(G, Node.mDesc); !S)
			{
				return S;
			}
			Accesses.emplace(Handle.GetIndex(), Node.mDesc.mAccesses);
		}
		// Usage is resolved after all graphics and ray stage descriptions have been assembled.
		if (auto S = ValidateResourceAccesses(G, true); !S)
		{
			return S;
		}
		for (auto Handle : G.mTopology.GetNodes())
		{
			auto& Node = G.mTopology.TryGetNode(Handle)->mPayload;
			const auto Found = Authored.find(Handle.GetIndex());
			if (Found == Authored.end())
			{
				continue;
			}
			const auto& Original = Found->second;
			Node.mbGraphicsLowered = true;
			if (Original.mGraphicsInput)
			{
				(void)G.mTopology.AddEdge(Original.mGraphicsInput.GetProducer(), Handle, {});
			}
			if (Original.mGraphicsStage != EArdaGraphicsStage::Pixel)
			{
				Node.mDesc = {};
				Node.mDesc.mGraphicsStage = Original.mGraphicsStage;
				Node.mDesc.mGraphicsInput = Original.mGraphicsInput;
				Node.mDesc.mbPipelineStageOnly = true;
				continue;
			}
			const auto RasterHandle = Original.mGraphicsInput.GetProducer();
			const auto& Raster = Authored.at(RasterHandle.GetIndex());
			auto StageHandle = Handle;
			bool Meshlet = false;
			eastl::unordered_set<uint32_t> Seen;
			while (StageHandle)
			{
				if (!Seen.insert(StageHandle.GetIndex()).second)
				{
					return Error("Graphics stage dataflow contains a cycle.");
				}
				Node.mGraphicsStages.push_back(StageHandle);
				const auto& D = Authored.at(StageHandle.GetIndex());
				Meshlet |= D.mGraphicsStage == EArdaGraphicsStage::Mesh;
				StageHandle = D.mGraphicsInput.GetProducer();
			}
			eastl::reverse(Node.mGraphicsStages.begin(), Node.mGraphicsStages.end());
			for (size_t I = 0; I < Node.mGraphicsStages.size(); ++I)
			{
				for (size_t J = 0; J < I; ++J)
				{
					for (const auto& A : Accesses.at(Node.mGraphicsStages[I].GetIndex()))
					{
						for (const auto& B : Accesses.at(Node.mGraphicsStages[J].GetIndex()))
						{
							if (A.mResource.mIndex == B.mResource.mIndex &&
							    (A.mAccess != EArdaDependencyAccess::Read ||
							        B.mAccess != EArdaDependencyAccess::Read) &&
							    Overlaps(A, B, G.mResources[A.mResource.mIndex]))
							{
								return Error(
								    "Overlapping resource writes across graphics stages require separate work; a draw cannot insert an inter-stage memory barrier.");
							}
						}
					}
				}
			}
			for (auto StageHandle : Node.mGraphicsStages)
			{
				const auto& Stage = G.mTopology.TryGetNode(StageHandle)->mPayload;
				auto Clone = Stage.mDefinition->mCloneGraphicsParameters(Stage.mParameters.get());
				if (!Clone)
				{
					return Clone.mStatus;
				}
				Node.mGraphicsStageParameters.push_back(eastl::move(Clone.mValue));
			}
			auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Configuration->mKind = Meshlet ? EArdaPipelineStateKind::Meshlet : EArdaPipelineStateKind::Graphics;
			for (const auto& C : Raster.mPipelineStages)
			{
				if (C.mConfiguration)
				{
					if (C.mConfiguration->mKind != Configuration->mKind)
					{
						if (!Raster.mbAutomaticRasterFamily || !Meshlet ||
						    C.mConfiguration->mKind != EArdaPipelineStateKind::Graphics)
						{
							return Error(
							    "Raster configuration mixes traditional and meshlet graphics pipeline families.");
						}
						const auto& Source = C.mConfiguration->mGraphics.mDesc;
						if (Source.mTopology >= EArdaRHIPrimitiveTopology::PatchList)
						{
							return Error("Meshlet rasterization cannot consume tessellation patch topology.");
						}
						auto& Target = Configuration->mMeshlet.mDesc;
						Target.mTopology = Source.mTopology;
						Target.mBindingLayouts = Source.mBindingLayouts;
						Target.mBlendState = Source.mBlendState;
						Target.mRasterState = Source.mRasterState;
						Target.mDepthStencilState = Source.mDepthStencilState;
						Target.mColorFormats = Source.mColorFormats;
						Target.mDepthFormat = Source.mDepthFormat;
						Target.mSampleCount = Source.mSampleCount;
						continue;
					}
					*Configuration = *C.mConfiguration;
				}
			}
			Node.mDesc = Original;
			Node.mDesc.mbPipelineStageOnly = false;
			Node.mDesc.mPipelines.push_back({"default", 0, Configuration->mKind, {}, Configuration});
			for (auto Provider : Node.mGraphicsStages)
			{
				if (Provider == Handle)
				{
					continue;
				}
				const auto* D = &Authored.at(Provider.GetIndex());
				Node.mDesc.mbDeclarativeParameters |= D->mbDeclarativeParameters;
				if (D->mGraphicsStage == EArdaGraphicsStage::Vertex)
				{
					Configuration->mGraphics.mDesc.mInputLayout = D->mGraphicsInputLayout;
				}
				Append(Node.mDesc.mAccesses, D->mAccesses);
				Append(Node.mDesc.mShaderBindings, D->mShaderBindings);
				Append(Node.mDesc.mBindlessTables, D->mBindlessTables);
				Append(Node.mDesc.mPipelineStages, D->mPipelineStages);
				if (UINT32_MAX - Node.mDesc.mEstimatedCost < D->mEstimatedCost)
				{
					return Error("Assembled graphics cost overflows its scheduling hint.");
				}
				Node.mDesc.mEstimatedCost += D->mEstimatedCost;
			}
			for (auto& C : Node.mDesc.mPipelineStages)
			{
				C.mConfiguration.reset();
			}
			if (auto S = DescribeArdaShaderBindings(G, Node.mDesc); !S)
			{
				return S;
			}
			FArdaInductorPipelineNode Projection;
			Projection.mNodeId = Handle.GetIndex();
			Projection.mContributions = Node.mDesc.mPipelineStages;
			auto Request = Node.mDesc.mPipelines.front();
			Request.mTerminalNodeId = Handle.GetIndex();
			if (auto Pattern = InferArdaInductorPipeline({Projection}, Request); !Pattern)
			{
				return Pattern.mStatus;
			}
		}
		return {};
	}
}
