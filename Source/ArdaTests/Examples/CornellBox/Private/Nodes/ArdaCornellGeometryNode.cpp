#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellGeometryNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
	class FArdaGenerateCornellGeometryShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaParameters)
			ARDA_SHADER_BUFFER_UAV(mVertices, 0, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mIndices, 1, 0, arda::EArdaRHIShaderStage::Compute)
			ARDA_SHADER_BUFFER_UAV(mMaterials, 2, 0, arda::EArdaRHIShaderStage::Compute)
		ARDA_END_SHADER_PARAMETER_STRUCT()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaGenerateCornellGeometryShader);
	};

	struct FArdaCornellGeometryNode::FArdaState
	{
		FArdaRHIDeviceRef mDevice;
		FArdaGlobalShaderMap mShaderMap;
		eastl::vector<FArdaInductorPipelineContribution> mStages;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;

		FArdaRHIStatus Initialize()
		{
			// Load the node-owned shader types through the common device shader map.
			if (!mShaderMap.Initialize(mDevice))
			{
				return CornellShaderError(mShaderMap);
			}

			const auto* Shader0 = mShaderMap.Find(FArdaGenerateCornellGeometryShader::GetStaticType());
			if (!Shader0)
			{
				return CornellShaderError(mShaderMap);
			}
			mStages.push_back(CornellShaderStage(Shader0));

			return {};
		}
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaGenerateCornellGeometryShader,
	    "/ArdaTests/CornellBox/CornellGeometry.hlsl",
	    "CornellGeometryCS",
	    "GenerateCornellGeometryCS",
	    arda::EArdaRHIShaderStage::Compute)

	FArdaDependencyNodeMetadata FArdaCornellGeometryNode::GetMetadata()
	{
		return {"cornell.geometry", 1};
	}

	eastl::string FArdaCornellGeometryNode::GetCanonicalKey(const FArdaParameters& P)
	{
		FArdaDependencyKeyBuilder Key;
		Key.Resource(P.mVertices);
		Key.Resource(P.mIndices);
		Key.Resource(P.mMaterials);
		Key.Value(P.mGroupCountX);
		Key.Value(P.mGroupCountY);
		Key.Value(P.mWorkspaceBytes);
		return Key.Build();
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaCornellGeometryNode::FArdaState>> FArdaCornellGeometryNode::Prepare(
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

	FArdaDependencyNodeDesc FArdaCornellGeometryNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;
		D.BindShader<FArdaGenerateCornellGeometryShader::FArdaParameters>(
		    {{"mVertices", P.mVertices}, {"mIndices", P.mIndices}, {"mMaterials", P.mMaterials}});

		D.mPipelineStages = Prepared.mStages;
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute, {}, Prepared.mConfiguration}};

		return D;
	}

	FArdaRHIStatus FArdaCornellGeometryNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		// Bind the executor-prepared pipeline and shader arguments before dispatching this operation.
		if (auto S = C.SetComputeState(); !S)
		{
			return S;
		}

		C.GetCommands().Dispatch(P.mGroupCountX, P.mGroupCountY, 1);
		return {};
	}
}
