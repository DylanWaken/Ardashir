#include "ArdaARDGExamplePch.h"
#include "Nodes/ArdaTerrainTriangulateNode.h"
#include "ArdaTerrainNodeInternal.h"

namespace arda
{
	class FArdaTriangulateTerrainShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
			ARDA_SHADER_TEXTURE_SRV(mHeightmap, 0, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mTerrainVertices, 0, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mTerrainIndices, 1, 0, arda::EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaTriangulateTerrainShader);
	};

	struct FArdaTerrainTriangulateNode::FState
	{
		FArdaRHIDeviceRef mDevice;
		FArdaGlobalShaderMap mShaderMap;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;
		FArdaRHIBindingLayoutRef mLayout;

		FArdaRHIStatus Initialize()
		{
			if (!mShaderMap.Initialize(mDevice))
			{
				return TerrainShaderError(mShaderMap);
			}
			const auto* Shader = mShaderMap.Find(FArdaTriangulateTerrainShader::GetStaticType());
			if (!Shader)
			{
				return TerrainShaderError(mShaderMap);
			}
			auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Configuration->mKind = EArdaPipelineStateKind::Compute;
			Configuration->mCompute =
			    FArdaComputePipelineStateInitializer::FromGlobalShader(*Shader, "Triangulate terrain");
			mLayout = Shader->GetBindingLayouts()[0];
			mConfiguration = eastl::move(Configuration);
			return {};
		}
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaTriangulateTerrainShader,
	    "/ArdaTests/ARDGExample/ArdaTerrain.hlsl",
	    "TerrainTriangulateCS",
	    "TriangulateTerrainCS",
	    arda::EArdaRHIShaderStage::Compute)

	FArdaDependencyNodeMetadata FArdaTerrainTriangulateNode::GetMetadata()
	{
		return {"example.terrain.triangulate", 1};
	}

	eastl::string FArdaTerrainTriangulateNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string Key;

		AppendResource(Key, P.mHeightmap);
		AppendResource(Key, P.mVertices);
		AppendResource(Key, P.mIndices);

		return Key;
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaTerrainTriangulateNode::FState>> FArdaTerrainTriangulateNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "This node requires an initialized device.")};
		}
		auto State = eastl::make_shared<FState>();
		State->mDevice = eastl::move(Device);
		auto Status = State->Initialize();
		if (!Status)
		{
			return {{}, eastl::move(Status)};
		}
		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaTerrainTriangulateNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mHeightmap, EArdaDependencyAccess::Read, EArdaRHIResourceState::ShaderResource},
		    {P.mVertices, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess},
		    {P.mIndices, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess}};
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute, {}, Prepared.mConfiguration}};
		D.mEstimatedCost = 8;
		return D;
	}

	FArdaRHIStatus FArdaTerrainTriangulateNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		FArdaTriangulateTerrainShader::FParameters ShaderParameters;
		ShaderParameters.mHeightmap = C.GetTexture(P.mHeightmap);
		ShaderParameters.mTerrainVertices = C.GetBuffer(P.mVertices);
		ShaderParameters.mTerrainIndices = C.GetBuffer(P.mIndices);
		FArdaRHIBindingSetRef Bindings;
		auto Status = CreateBindings(C, ShaderParameters, Prepared.mLayout, Bindings);
		if (!Status)
		{
			return Status;
		}
		FArdaRHIComputeState State;
		State.mPipeline = C.GetPipeline()->mCompute;
		State.mBindings = {Bindings};
		Status = C.GetCommands().SetComputeState(State);
		if (!Status)
		{
			return Status;
		}
		C.GetCommands().Dispatch(DivideRoundUp(ArdaTerrainHeightmapWidth - 1, 8),
		    DivideRoundUp(ArdaTerrainHeightmapHeight - 1, 8),
		    1);
		return {};
	}
}
