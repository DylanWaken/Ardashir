#include "ArdaARDGExamplePch.h"
#include "Nodes/ArdaTerrainTriangulateNode.h"
#include "ArdaTerrainNodeInternal.h"

namespace arda
{
	class FArdaTriangulateTerrainShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_TEXTURE_SRV(mHeightmap, 0, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mTerrainVertices, 0, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mTerrainIndices, 1, 0, arda::EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaTriangulateTerrainShader);
	};

	struct FArdaTerrainTriangulateNode::FArdaState
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

			const auto* Shader = mShaderMap.Find(FArdaTriangulateTerrainShader::GetStaticType());
			if (!Shader)
			{
				return TerrainShaderError(mShaderMap);
			}

			auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Configuration->mKind = EArdaPipelineStateKind::Compute;
			Configuration->mCompute =
			    FArdaComputePipelineStateInitializer::FromGlobalShader(*Shader, "Triangulate terrain");
			mConfiguration = eastl::move(Configuration);
			return {};
		}
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaTriangulateTerrainShader,
	    "/ArdaTests/ARDGExample/ArdaTerrain.hlsl",
	    "TerrainTriangulateCS",
	    "TriangulateTerrainCS",
	    arda::EArdaRHIShaderStage::Compute)

	FArdaRHIStatus FArdaTerrainTriangulateNode::DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
	{
		FArdaRHIBufferDesc D;
		D.mByteSize = uint64_t(ArdaTerrainVertexCount) * sizeof(FArdaTerrainVertex);
		D.mStructureStride = sizeof(FArdaTerrainVertex);
		D.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::Vertex;
		if (auto S = C.Buffer(P.mVertices, "Vertices", D); !S)
		{
			return S;
		}
		D.mByteSize = uint64_t(ArdaTerrainIndexCount) * sizeof(uint32_t);
		D.mStructureStride = sizeof(uint32_t);
		D.mUsage = EArdaRHIBufferUsage::Structured | EArdaRHIBufferUsage::UnorderedAccess | EArdaRHIBufferUsage::Index;
		return C.Buffer(P.mIndices, "Indices", D);
	}

	FArdaDependencyNodeMetadata FArdaTerrainTriangulateNode::GetMetadata()
	{
		return {"example.terrain.triangulate", 1};
	}

	eastl::string FArdaTerrainTriangulateNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;

		Key.Resource(P.mHeightmap);
		Key.Resource(P.mVertices);
		Key.Resource(P.mIndices);

		return Key.Build();
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaTerrainTriangulateNode::FArdaState>> FArdaTerrainTriangulateNode::
	    Prepare(FArdaRHIDeviceRef Device)
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

	FArdaDependencyNodeDesc FArdaTerrainTriangulateNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.BindShader<FArdaTriangulateTerrainShader::FArdaParameters>(
		    {{"mHeightmap", P.mHeightmap}, {"mTerrainVertices", P.mVertices}, {"mTerrainIndices", P.mIndices}});
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute, {}, Prepared.mConfiguration}};
		D.mEstimatedCost = 8;
		return D;
	}

	FArdaRHIStatus FArdaTerrainTriangulateNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		// Bind the executor-prepared pipeline and shader arguments before dispatching this operation.
		if (auto S = C.SetComputeState(); !S)
		{
			return S;
		}

		C.GetCommands().Dispatch(DivideRoundUp(ArdaTerrainHeightmapWidth - 1, 8),
		    DivideRoundUp(ArdaTerrainHeightmapHeight - 1, 8),
		    1);
		return {};
	}
}
