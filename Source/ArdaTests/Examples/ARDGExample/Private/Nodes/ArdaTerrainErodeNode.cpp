#include "ArdaARDGExamplePch.h"
#include "Nodes/ArdaTerrainErodeNode.h"
#include "ArdaTerrainNodeInternal.h"

namespace arda
{
	class FArdaErodeTerrainShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)
			ARDA_SHADER_TEXTURE_SRV(mSource, 0, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_TEXTURE_UAV(mHeightmap, 0, 0, arda::EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaErodeTerrainShader);
	};

	struct FArdaTerrainErodeNode::FState
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
			const auto* Shader = mShaderMap.Find(FArdaErodeTerrainShader::GetStaticType());
			if (!Shader)
			{
				return TerrainShaderError(mShaderMap);
			}
			auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Configuration->mKind = EArdaPipelineStateKind::Compute;
			Configuration->mCompute = FArdaComputePipelineStateInitializer::FromGlobalShader(*Shader, "Erode terrain");
			mLayout = Shader->GetBindingLayouts()[0];
			mConfiguration = eastl::move(Configuration);
			return {};
		}
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaErodeTerrainShader,
	    "/ArdaTests/ARDGExample/ArdaTerrain.hlsl",
	    "TerrainErodeCS",
	    "ErodeHeightmapCS",
	    arda::EArdaRHIShaderStage::Compute)

	FArdaDependencyNodeMetadata FArdaTerrainErodeNode::GetMetadata()
	{
		return {"example.terrain.erode", 1};
	}

	eastl::string FArdaTerrainErodeNode::GetCanonicalKey(const FParameters& P)
	{
		eastl::string Key;

		AppendResource(Key, P.mSource);
		AppendResource(Key, P.mHeightmap);

		return Key;
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaTerrainErodeNode::FState>> FArdaTerrainErodeNode::Prepare(
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

	FArdaDependencyNodeDesc FArdaTerrainErodeNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mAccesses = {{P.mSource, EArdaDependencyAccess::Read, EArdaRHIResourceState::ShaderResource},
		    {P.mHeightmap, EArdaDependencyAccess::Write, EArdaRHIResourceState::UnorderedAccess}};
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute, {}, Prepared.mConfiguration}};
		D.mEstimatedCost = 8;
		return D;
	}

	FArdaRHIStatus FArdaTerrainErodeNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		FArdaErodeTerrainShader::FParameters Shader;
		Shader.mSource = C.GetTexture(P.mSource);
		Shader.mHeightmap = C.GetTexture(P.mHeightmap);
		return DispatchHeightmap(C, Shader, Prepared.mLayout);
	}
}
