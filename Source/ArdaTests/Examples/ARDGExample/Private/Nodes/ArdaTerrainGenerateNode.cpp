#include "ArdaARDGExamplePch.h"
#include "Nodes/ArdaTerrainGenerateNode.h"
#include "ArdaTerrainNodeInternal.h"

namespace arda
{
	class FArdaGenerateTerrainShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_SRV(mSettings, 0, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_TEXTURE_UAV(mHeightmap, 0, 0, arda::EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaGenerateTerrainShader);
	};

	struct FArdaTerrainGenerateNode::FArdaState
	{
		FArdaRHIDeviceRef mDevice;
		FArdaGlobalShaderMap mShaderMap;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;

		FArdaRHIStatus Initialize()
		{
			// Load the node-owned shader types through the common device shader map.
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
			mConfiguration = eastl::move(Configuration);
			return {};
		}
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaGenerateTerrainShader,
	    "/ArdaTests/ARDGExample/ArdaTerrain.hlsl",
	    "TerrainGenerateCS",
	    "GenerateNoiseHeightmapCS",
	    arda::EArdaRHIShaderStage::Compute)

	FArdaRHIStatus FArdaTerrainGenerateNode::DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
	{
		FArdaRHITextureDesc D;
		D.mWidth = ArdaTerrainHeightmapWidth;
		D.mHeight = ArdaTerrainHeightmapHeight;
		D.mFormat = EArdaRHIFormat::R32Float;
		D.mUsage = EArdaRHITextureUsage::ShaderResource | EArdaRHITextureUsage::UnorderedAccess;
		return C.Texture(P.mHeightmap, "Heightmap", D);
	}

	FArdaDependencyNodeMetadata FArdaTerrainGenerateNode::GetMetadata()
	{
		return {"example.terrain.generate", 1};
	}

	eastl::string FArdaTerrainGenerateNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;

		Key.Resource(P.mSettings);
		Key.Resource(P.mHeightmap);

		return Key.Build();
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaTerrainGenerateNode::FArdaState>> FArdaTerrainGenerateNode::Prepare(
	    FArdaRHIDeviceRef Device)
	{
		if (!Device)
		{
			return {{},
			    FArdaRHIStatus::Error(EArdaRHIResult::InvalidState, "This node requires an initialized device.")};
		}

		auto State = eastl::make_shared<FArdaState>();
		State->mDevice = eastl::move(Device);

		// Publish prepared state only after all node-owned setup succeeds.
		auto Status = State->Initialize();
		if (!Status)
		{
			return {{}, eastl::move(Status)};
		}

		return {eastl::move(State), {}};
	}

	FArdaDependencyNodeDesc FArdaTerrainGenerateNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.BindShader<FArdaGenerateTerrainShader::FArdaParameters>(
		    {{"mSettings", P.mSettings}, {"mHeightmap", P.mHeightmap}});
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute, {}, Prepared.mConfiguration}};
		D.mEstimatedCost = 8;
		return D;
	}

	FArdaRHIStatus FArdaTerrainGenerateNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		// Bind the executor-prepared pipeline and shader arguments before dispatching this operation.
		if (auto S = C.SetComputeState(); !S)
		{
			return S;
		}

		C.GetCommands().Dispatch(DivideRoundUp(ArdaTerrainHeightmapWidth, 8),
		    DivideRoundUp(ArdaTerrainHeightmapHeight, 8),
		    1);
		return {};
	}
}
