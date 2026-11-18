#include "Inductor/Compiler/ArdaInductorNodePreparation.h"
#include "Graph/Node/ArdaDependencyGraphImpl.h"
#include "Graph/Node/ArdaNodeDeclarationOrigins.h"
#include "Inductor/Rules/ArdaInductorBindingRules.h"
#include "Inductor/Rules/ArdaInductorRequirements.h"

namespace arda
{
	namespace
	{
		using FArdaDescriptions =
		    eastl::unordered_map<FArdaGraphNodeHandle, FArdaDependencyNodeDesc, FArdaGraphNodeHandleHash>;

		template <class T>
		void Append(eastl::vector<T>& Into, const eastl::vector<T>& From)
		{
			Into.insert(Into.end(), From.begin(), From.end());
		}

		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> RehydrateConfiguration(
		    const eastl::shared_ptr<const FArdaInductorPipelineConfiguration>& Semantic,
		    const eastl::shared_ptr<const FArdaInductorPipelineConfiguration>& Native)
		{
			if (!Semantic)
			{
				return {};
			}
			auto Into = eastl::make_shared<FArdaInductorPipelineConfiguration>(*Semantic);
			const FArdaInductorPipelineConfiguration Empty;
			const auto& Source = Native ? *Native : Empty;
			Into->mCompute.mDesc.mComputeShader = Source.mCompute.mDesc.mComputeShader;
			Into->mCompute.mDesc.mBindingLayouts = Source.mCompute.mDesc.mBindingLayouts;
			Into->mGraphics.mDesc.mInputLayout = Source.mGraphics.mDesc.mInputLayout;
			Into->mGraphics.mDesc.mVertexShader = Source.mGraphics.mDesc.mVertexShader;
			Into->mGraphics.mDesc.mHullShader = Source.mGraphics.mDesc.mHullShader;
			Into->mGraphics.mDesc.mDomainShader = Source.mGraphics.mDesc.mDomainShader;
			Into->mGraphics.mDesc.mGeometryShader = Source.mGraphics.mDesc.mGeometryShader;
			Into->mGraphics.mDesc.mPixelShader = Source.mGraphics.mDesc.mPixelShader;
			Into->mGraphics.mDesc.mBindingLayouts = Source.mGraphics.mDesc.mBindingLayouts;
			Into->mMeshlet.mDesc.mAmplificationShader = Source.mMeshlet.mDesc.mAmplificationShader;
			Into->mMeshlet.mDesc.mMeshShader = Source.mMeshlet.mDesc.mMeshShader;
			Into->mMeshlet.mDesc.mPixelShader = Source.mMeshlet.mDesc.mPixelShader;
			Into->mMeshlet.mDesc.mBindingLayouts = Source.mMeshlet.mDesc.mBindingLayouts;
			Into->mRayTracing.mDesc.mGlobalBindingLayouts = Source.mRayTracing.mDesc.mGlobalBindingLayouts;
			for (size_t Index = 0; Index < Into->mRayTracing.mDesc.mShaders.size(); ++Index)
			{
				auto& Shader = Into->mRayTracing.mDesc.mShaders[Index];
				const auto& Sources = Source.mRayTracing.mDesc.mShaders;
				auto Found = eastl::find_if(Sources.begin(),
				    Sources.end(),
				    [&](const auto& Item)
				    {
					    return Item.mExportName == Shader.mExportName;
				    });
				const auto NativeShader = Found != Sources.end() ? *Found
				    : Index < Sources.size()                     ? Sources[Index]
				                                                 : FArdaRHIRayTracingPipelineShaderDesc{};
				Shader.mShader = NativeShader.mShader;
				Shader.mLocalBindingLayout = NativeShader.mLocalBindingLayout;
			}
			for (size_t Index = 0; Index < Into->mRayTracing.mDesc.mHitGroups.size(); ++Index)
			{
				auto& Group = Into->mRayTracing.mDesc.mHitGroups[Index];
				const auto& Sources = Source.mRayTracing.mDesc.mHitGroups;
				auto Found = eastl::find_if(Sources.begin(),
				    Sources.end(),
				    [&](const auto& Item)
				    {
					    return Item.mExportName == Group.mExportName;
				    });
				const auto NativeGroup = Found != Sources.end() ? *Found
				    : Index < Sources.size()                    ? Sources[Index]
				                                                : FArdaRHIRayTracingHitGroupDesc{};
				Group.mClosestHitShader = NativeGroup.mClosestHitShader;
				Group.mAnyHitShader = NativeGroup.mAnyHitShader;
				Group.mIntersectionShader = NativeGroup.mIntersectionShader;
				Group.mLocalBindingLayout = NativeGroup.mLocalBindingLayout;
			}
			Into->mWorkGraph.mDesc.mShaders = Source.mWorkGraph.mDesc.mShaders;
			Into->mWorkGraph.mDesc.mGlobalBindingLayouts = Source.mWorkGraph.mDesc.mGlobalBindingLayouts;
			return Into;
		}

		void RehydrateBindings(FArdaDependencyShaderBindings& Into, const FArdaDependencyShaderBindings* Native)
		{
			if (Native)
			{
				Into.mMetadata = Native->mMetadata;
			}
			for (auto& Resource : Into.mResources)
			{
				Resource.mSampler = {};
				if (!Native)
				{
					continue;
				}
				for (const auto& Source : Native->mResources)
				{
					if (Source.mMember == Resource.mMember && Source.mArrayElement == Resource.mArrayElement)
					{
						Resource.mSampler = Source.mSampler;
						break;
					}
				}
			}
		}

		template <class T>
		const T* FindDeclaration(const FArdaNodeDeclarationOrigin& Origin,
		    const FArdaDescriptions& Descriptions,
		    eastl::vector<T> FArdaDependencyNodeDesc::* Member)
		{
			const auto Node = Descriptions.find(Origin.mNode);
			if (Node == Descriptions.end())
			{
				return nullptr;
			}
			const auto& Values = Node->second.*Member;
			return Origin.mIndex < Values.size() ? &Values[Origin.mIndex] : nullptr;
		}

		const FArdaDependencyShaderBindings* FindBindings(const FArdaNodeDeclarationOrigin& Origin,
		    const FArdaDescriptions& Descriptions)
		{
			if (Origin.mParentIndex == UINT32_MAX)
			{
				return FindDeclaration(Origin, Descriptions, &FArdaDependencyNodeDesc::mShaderBindings);
			}
			const auto Node = Descriptions.find(Origin.mNode);
			if (Node == Descriptions.end() || Origin.mParentIndex >= Node->second.mShaderTables.size())
			{
				return nullptr;
			}
			const auto& Records = Node->second.mShaderTables[Origin.mParentIndex].mRecords;
			return Origin.mIndex < Records.size() ? &Records[Origin.mIndex].mLocalBindings : nullptr;
		}

		FArdaRHIStatus MissingDeclaration()
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
			    "Preprocessing produced missing declaration provenance; invoke a registered node or preserve its declaration identity.");
		}

		FArdaRHIStatus PreserveSemanticDescription(FArdaDependencyNodeDesc& Into,
		    const FArdaDependencyNodeDesc& Semantic,
		    const FArdaDescriptions& Authored,
		    const FArdaDescriptions& Assembled)
		{
			auto Native = eastl::move(Into);
			Into = Semantic;
			Into.mbArchivedShaderAccesses = false;
			Into.mGraphicsInputLayout = Native.mGraphicsInputLayout;
			for (auto& Bindings : Into.mShaderBindings)
			{
				const auto* Source = FindBindings(Bindings.mDeclaration, Authored);
				if (!Source)
				{
					return MissingDeclaration();
				}
				RehydrateBindings(Bindings, Source);
			}
			for (auto& Table : Into.mBindlessTables)
			{
				const auto* SourceTable =
				    FindDeclaration(Table.mDeclaration, Authored, &FArdaDependencyNodeDesc::mBindlessTables);
				if (!SourceTable)
				{
					return MissingDeclaration();
				}
				for (auto& Entry : Table.mEntries)
				{
					Entry.mSampler = {};
					for (const auto& Source : SourceTable->mEntries)
					{
						if (Source.mSlot == Entry.mSlot && Source.mArrayElement == Entry.mArrayElement &&
						    Source.mType == Entry.mType)
						{
							Entry.mSampler = Source.mSampler;
							break;
						}
					}
				}
			}
			for (auto& Table : Into.mShaderTables)
			{
				for (auto& Record : Table.mRecords)
				{
					auto& Bindings = Record.mLocalBindings;
					const auto* Source = FindBindings(Bindings.mDeclaration, Authored);
					if (!Source && (Bindings.mMetadata || !Bindings.mResources.empty() || !Bindings.mValues.empty()))
					{
						return MissingDeclaration();
					}
					RehydrateBindings(Bindings, Source);
				}
			}
			for (auto& Stage : Into.mPipelineStages)
			{
				const auto* Source =
				    FindDeclaration(Stage.mDeclaration, Authored, &FArdaDependencyNodeDesc::mPipelineStages);
				if (!Source)
				{
					return MissingDeclaration();
				}
				Stage.mShader = Source->mShader;
				Stage.mLocalBindingLayout = Source->mLocalBindingLayout;
				Stage.mBindingLayouts = Source->mBindingLayouts;
				Stage.mConfiguration = RehydrateConfiguration(Stage.mConfiguration, Source->mConfiguration);
			}
			for (auto& Pipeline : Into.mPipelines)
			{
				const auto* Source = FindDeclaration(Pipeline.mDeclaration,
				    Pipeline.mDeclaration.mParentIndex == UINT32_MAX ? Authored : Assembled,
				    &FArdaDependencyNodeDesc::mPipelines);
				if (!Source)
				{
					return MissingDeclaration();
				}
				Pipeline.mTerminalNodeId = 0;
				Pipeline.mConfiguration = RehydrateConfiguration(Pipeline.mConfiguration, Source->mConfiguration);
				Pipeline.mBindingLayouts = Source->mBindingLayouts;
				Pipeline.mLocalBindingLayouts = Source->mLocalBindingLayouts;
			}
			for (auto& Requirement : Into.mRequirements.mEnvironment)
			{
				for (const auto& Source : Native.mRequirements.mEnvironment)
				{
					if (Requirement.mName == Source.mName)
					{
						Requirement.mCheck = Source.mCheck;
						break;
					}
				}
			}
			return {};
		}

		FArdaRHIStatus ComposeGraphicsDeclarations(const FArdaDependencyNode& Node,
		    const FArdaDescriptions& Authored,
		    FArdaDependencyNodeDesc& Desc)
		{
			bool Meshlet = false;
			const FArdaDependencyNodeDesc* Raster = nullptr;
			for (const auto Handle : Node.mGraphicsStages)
			{
				const auto Found = Authored.find(Handle);
				if (Found == Authored.end())
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "An assembled draw references a missing stage declaration.");
				}
				Meshlet |= Found->second.mGraphicsStage == EArdaGraphicsStage::Mesh;
				if (Found->second.mGraphicsStage == EArdaGraphicsStage::Raster)
				{
					Raster = &Found->second;
				}
			}
			if (!Raster)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "An assembled draw has no raster declaration.");
			}
			auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Configuration->mKind = Meshlet ? EArdaPipelineStateKind::Meshlet : EArdaPipelineStateKind::Graphics;
			for (const auto& Contribution : Raster->mPipelineStages)
			{
				if (!Contribution.mConfiguration)
				{
					continue;
				}
				const auto& Source = *Contribution.mConfiguration;
				if (Source.mKind == Configuration->mKind)
				{
					*Configuration = Source;
				}
				else if (Raster->mbAutomaticRasterFamily && Meshlet && Source.mKind == EArdaPipelineStateKind::Graphics)
				{
					const auto& From = Source.mGraphics.mDesc;
					auto& Into = Configuration->mMeshlet.mDesc;
					Into.mTopology = From.mTopology;
					Into.mBindingLayouts = From.mBindingLayouts;
					Into.mBlendState = From.mBlendState;
					Into.mRasterState = From.mRasterState;
					Into.mDepthStencilState = From.mDepthStencilState;
					Into.mColorFormats = From.mColorFormats;
					Into.mDepthFormat = From.mDepthFormat;
					Into.mSampleCount = From.mSampleCount;
				}
				else
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "Target-device raster declaration differs from the preprocessed pipeline family.");
				}
			}
			Desc.mPipelines.clear();
			Desc.mPipelines.push_back({"default", 0, Configuration->mKind, {}, Configuration});
			Desc.mPipelines.back().mDeclaration = {Node.mHandle, 0, 0};
			for (const auto Handle : Node.mGraphicsStages)
			{
				if (Handle == Node.mHandle)
				{
					continue;
				}
				const auto& Stage = Authored.at(Handle);
				if (Stage.mGraphicsStage == EArdaGraphicsStage::Vertex)
				{
					Configuration->mGraphics.mDesc.mInputLayout = Stage.mGraphicsInputLayout;
				}
				Append(Desc.mShaderBindings, Stage.mShaderBindings);
				Append(Desc.mBindlessTables, Stage.mBindlessTables);
				Append(Desc.mPipelineStages, Stage.mPipelineStages);
			}
			for (auto& Contribution : Desc.mPipelineStages)
			{
				Contribution.mConfiguration.reset();
			}
			return {};
		}
	}

	FArdaRHIStatus InitializeArdaInductorNodes(FArdaDependencyGraph::FArdaImpl& Graph)
	{
		Graph.mCompile.mNodes.clear();
		FArdaDescriptions Authored;
		for (const auto Handle : Graph.mTopology.GetNodes())
		{
			const auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
			if (!Node.mDefinition || !Node.mSourceParameters || !Node.mDefinition->mPrepare)
			{
				return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
				    "A graph node has no registered execution preparation or public parameters.");
			}
			auto Prepared = Node.mDefinition->mPrepare(Graph.mCompile.mExecution->mDevice, Node.mSourceParameters);
			if (!Prepared || !Prepared.mValue)
			{
				return Prepared ? FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
				                      "Node execution preparation returned no instance.")
				                : Prepared.mStatus;
			}
			auto& State = Graph.mCompile.mNodes[Handle];
			State.mParameters = eastl::move(Prepared.mValue);
			auto Described = Node.mDefinition->mDescribe(State.mParameters.get());
			if (!Described)
			{
				return Described.mStatus;
			}
			Graph.NormalizeDescription(Described.mValue);
			StampArdaNodeDeclarationOrigins(Described.mValue, Handle);
			Described.mValue.mRequirements = Node.mDefinition->mGetRequirements(Node.mSourceParameters.get());
			Authored.emplace(Handle, eastl::move(Described.mValue));
		}
		Graph.mbUsingCompiledDescriptions = true;
		FArdaDescriptions Assembled;
		for (const auto Handle : Graph.mTopology.GetNodes())
		{
			const auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
			auto& State = Graph.mCompile.mNodes.at(Handle);
			State.mDesc = Node.mDesc.mbPipelineStageOnly ? FArdaDependencyNodeDesc{} : Authored.at(Handle);
			if (!Node.mGraphicsStages.empty())
			{
				if (auto Status = ComposeGraphicsDeclarations(Node, Authored, State.mDesc); !Status)
				{
					return Status;
				}
			}
			else if (Node.mDesc.mRayStage == EArdaRHIShaderStage::RayGeneration && Node.mbRayTracingLowered)
			{
				if (State.mDesc.mPipelines.empty())
				{
					State.mDesc.mPipelines.push_back({"default", 0, EArdaPipelineStateKind::RayTracing});
					State.mDesc.mPipelines.back().mDeclaration = {Handle, 0, 0};
				}
				eastl::unordered_set<FArdaGraphNodeHandle, FArdaGraphNodeHandleHash> Included;
				for (const auto Input : Node.mDesc.mRayInputs)
				{
					if (!Included.insert(Input.GetProducer()).second)
					{
						continue;
					}
					const auto Provider = Authored.find(Input.GetProducer());
					if (Provider == Authored.end())
					{
						return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
						    "An assembled ray dispatch references a missing shader declaration.");
					}
					Append(State.mDesc.mPipelineStages, Provider->second.mPipelineStages);
					Append(State.mDesc.mShaderBindings, Provider->second.mShaderBindings);
					Append(State.mDesc.mBindlessTables, Provider->second.mBindlessTables);
				}
			}
			State.mDesc.mRequirements = Authored.at(Handle).mRequirements;
			Assembled.emplace(Handle, State.mDesc);
		}
		for (const auto Handle : Graph.mTopology.GetNodes())
		{
			const auto& Node = Graph.mTopology.TryGetNode(Handle)->mPayload;
			auto& State = Graph.mCompile.mNodes.at(Handle);
			if (auto Status = PreserveSemanticDescription(State.mDesc, Node.mDesc, Authored, Assembled); !Status)
			{
				Status.mMessage = "Node '" + Node.mName + "': " + Status.mMessage;
				return Status;
			}
			Graph.NormalizeDescription(State.mDesc);
			InferArdaNodeRequirements(Graph, Node.mDefinition->mKind, State.mDesc);
			if (Graph.mCompile.mExecution->mDevice)
			{
				if (auto Status = State.mDesc.mRequirements.Check(Graph.mCompile.mExecution->mDevice.Get()); !Status)
				{
					return Status;
				}
			}
			if (auto Status = DescribeArdaShaderBindings(Graph, State.mDesc); !Status)
			{
				Status.mMessage = "Node '" + Node.mName + "': " + Status.mMessage;
				return Status;
			}
			for (const auto StageHandle : Node.mGraphicsStages)
			{
				const auto* Stage = Graph.mTopology.TryGetNode(StageHandle);
				const auto Found = Graph.mCompile.mNodes.find(StageHandle);
				if (!Stage || Found == Graph.mCompile.mNodes.end() ||
				    !Stage->mPayload.mDefinition->mCloneGraphicsParameters)
				{
					return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument,
					    "An assembled draw references an unavailable graphics stage.");
				}
				auto Clone = Stage->mPayload.mDefinition->mCloneGraphicsParameters(Found->second.mParameters.get());
				if (!Clone)
				{
					return Clone.mStatus;
				}
				State.mGraphicsStageParameters.push_back(eastl::move(Clone.mValue));
			}
		}
		return {};
	}
}
