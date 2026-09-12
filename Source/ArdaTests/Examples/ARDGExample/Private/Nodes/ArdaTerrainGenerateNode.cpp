#include "ArdaARDGExamplePch.h"
#include "Nodes/ArdaTerrainGenerateNode.h"
#include "ArdaTerrainNodeInternal.h"

namespace arda
{
	class FArdaGenerateTerrainShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
			ARDA_SHADER_BUFFER_SRV(mSettings, 0, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_TEXTURE_UAV(mHeightmap, 0, 0, arda::EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaGenerateTerrainShader);
	};

	struct FArdaTerrainGenerateNode::FState
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
			const auto* Shader = mShaderMap.Find(FArdaGenerateTerrainShader::GetStaticType());
			if (!Shader)
			{
				return TerrainShaderError(mShaderMap);
			}
			auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Configuration->mKind = EArdaPipelineStateKind::Compute;
			Configuration->mCompute =
			    FArdaComputePipelineStateInitializer::FromGlobalShader(*Shader, "Generate terrain");
			mLayout = Shader->GetBindingLayouts()[0];
			mConfiguration = eastl::move(Configuration);
			return {};
		}
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaGenerateTerrainShader,
	    "/ArdaTests/ARDGExample/ArdaTerrain.hlsl",
	    "TerrainGenerateCS",
	    "GenerateNoiseHeightmapCS",
	    arda::EArdaRHIShaderStage::Compute)

	FArdaDependencyNodeMetadata FArdaTerrainGenerateNode::GetMetadata()
	{
		return {"example.terrain.generate", 1};
	}

	eastl::string FArdaTerrainGenerateNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string Key;

		AppendResource(Key, P.mSettings);
		AppendResource(Key, P.mHeightmap);

		return Key;
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaTerrainGenerateNode::FState>> FArdaTerrainGenerateNode::Prepare(
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

	FArdaDependencyNodeDesc FArdaTerrainGenerateNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mSettings, EArdaDependencyAccess::Read, EArdaRHIResourceState::ShaderResource},
		    {P.mHeightmap, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess}};
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute, {}, Prepared.mConfiguration}};
		D.mEstimatedCost = 8;
		return D;
	}

	FArdaRHIStatus FArdaTerrainGenerateNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		FArdaGenerateTerrainShader::FParameters Shader;
		Shader.mSettings = C.GetBuffer(P.mSettings);
		Shader.mHeightmap = C.GetTexture(P.mHeightmap);
		return DispatchHeightmap(C, Shader, Prepared.mLayout);
	}
}
