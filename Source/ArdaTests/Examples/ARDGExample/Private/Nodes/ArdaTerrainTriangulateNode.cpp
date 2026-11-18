#include "ArdaExampleNodeState.h"
#include "ArdaARDGExamplePch.h"
#include "Nodes/ArdaTerrainTriangulateNode.h"
#include "ArdaTerrainNodeInternal.h"

namespace arda
{
	ARDA_DEFINE_GRAPH_NODE(FArdaTerrainTriangulateNode, TerrainTriangulate);

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

	TArdaRHIResult<eastl::shared_ptr<const FArdaTerrainTriangulateNode::FArdaState>> FArdaTerrainTriangulateNode::
	    Prepare(FArdaRHIDeviceRef Device)
	{
		return PrepareExampleNodeState<FArdaState>(eastl::move(Device));
	}

	FArdaDependencyNodeDesc FArdaTerrainTriangulateNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
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
