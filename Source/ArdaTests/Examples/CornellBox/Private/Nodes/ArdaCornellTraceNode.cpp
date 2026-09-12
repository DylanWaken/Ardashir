#include "ArdaCornellBoxPch.h"
#include "Nodes/ArdaCornellTraceNode.h"
#include "ArdaCornellNodeInternal.h"

namespace arda
{
#define ARDA_CORNELL_RAY_PARAMETERS()                                                                                  \
	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FParameters)                                                                    \
		ARDA_SHADER_ACCELERATION_STRUCTURE(mScene, 0, 0, arda::EArdaRHIShaderStage::AllRayTracing)                     \
		ARDA_SHADER_BUFFER_SRV(mVertices, 1, 0, arda::EArdaRHIShaderStage::AllRayTracing)                              \
		ARDA_SHADER_BUFFER_SRV(mIndices, 2, 0, arda::EArdaRHIShaderStage::AllRayTracing)                               \
		ARDA_SHADER_BUFFER_SRV(mMaterials, 3, 0, arda::EArdaRHIShaderStage::AllRayTracing)                             \
		ARDA_SHADER_BUFFER_UAV(mSampleRadiance, 0, 0, arda::EArdaRHIShaderStage::AllRayTracing)                        \
		ARDA_SHADER_UNIFORM_BUFFER(mFrame, 0, 0, arda::EArdaRHIShaderStage::AllRayTracing)                             \
	ARDA_END_SHADER_PARAMETER_STRUCT()

	class FArdaCornellRayGenerationShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_CORNELL_RAY_PARAMETERS()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaCornellRayGenerationShader);
	};

	class FArdaCornellMissShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_CORNELL_RAY_PARAMETERS()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaCornellMissShader);
	};

	class FArdaCornellClosestHitShader final : public arda::FArdaGlobalShader
	{
	public:
		ARDA_CORNELL_RAY_PARAMETERS()
		ARDA_DECLARE_GLOBAL_SHADER(FArdaCornellClosestHitShader);
	};

#undef ARDA_CORNELL_RAY_PARAMETERS

	struct FArdaCornellTraceNode::FState
	{
		FArdaRHIDeviceRef mDevice;
		FArdaGlobalShaderMap mShaderMap;
		eastl::vector<FArdaInductorPipelineContribution> mStages;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mConfiguration;

		FArdaRHIStatus Initialize()
		{
			if (!mShaderMap.Initialize(mDevice))
			{
				return CornellShaderError(mShaderMap);
			}
			const auto* Shader0 = mShaderMap.Find(FArdaCornellRayGenerationShader::GetStaticType());
			if (!Shader0)
			{
				return CornellShaderError(mShaderMap);
			}
			mStages.push_back(CornellShaderStage(Shader0, "CornellRayGen"));
			const auto* Shader1 = mShaderMap.Find(FArdaCornellMissShader::GetStaticType());
			if (!Shader1)
			{
				return CornellShaderError(mShaderMap);
			}
			mStages.push_back(CornellShaderStage(Shader1, "CornellMiss"));
			const auto* Shader2 = mShaderMap.Find(FArdaCornellClosestHitShader::GetStaticType());
			if (!Shader2)
			{
				return CornellShaderError(mShaderMap);
			}
			mStages.push_back(CornellShaderStage(Shader2, "", "CornellHitGroup"));
			auto Ray = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Ray->mKind = EArdaPipelineStateKind::RayTracing;
			Ray->mRayTracing.mDesc.mMaxPayloadSize = 32;
			Ray->mRayTracing.mDesc.mMaxAttributeSize = 8;
			Ray->mRayTracing.mDesc.mMaxRecursionDepth = 1;
			mConfiguration = eastl::move(Ray);
			return {};
		}
	};

	struct FArdaCornellTraceNode::FInstanceState
	{
		FArdaRHIRayTracingPipelineRef mTablePipeline;
		FArdaRHIShaderTableRef mShaderTable;
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaCornellRayGenerationShader,
	    "/ArdaTests/CornellBox/CornellPathTracer.hlsl",
	    "CornellRayGen",
	    "CornellRayGen",
	    arda::EArdaRHIShaderStage::RayGeneration)
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaCornellMissShader,
	    "/ArdaTests/CornellBox/CornellPathTracer.hlsl",
	    "CornellMiss",
	    "CornellMiss",
	    arda::EArdaRHIShaderStage::Miss)
	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaCornellClosestHitShader,
	    "/ArdaTests/CornellBox/CornellPathTracer.hlsl",
	    "CornellClosestHit",
	    "CornellClosestHit",
	    arda::EArdaRHIShaderStage::ClosestHit)

	FArdaDependencyNodeMetadata FArdaCornellTraceNode::GetMetadata()
	{
		return {"cornell.trace", 1};
	}

	eastl::string FArdaCornellTraceNode::GetCanonicalKey(const FParameters& P)
	{
		return MakeCornellNodeKey(P);
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaCornellTraceNode::FState>> FArdaCornellTraceNode::Prepare(
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

	TArdaRHIResult<eastl::shared_ptr<FArdaCornellTraceNode::FInstanceState>> FArdaCornellTraceNode::CreateInstance(
	    FArdaRHIDeviceRef,
	    const FParameters&,
	    const FState&)
	{
		return {eastl::make_shared<FInstanceState>(), {}};
	}

	FArdaDependencyNodeDesc FArdaCornellTraceNode::Describe(const FParameters& P, const FState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.mWorkspaceBytes = P.mWorkspaceBytes;
		const auto Read = [&](uint32_t I, EArdaRHIResourceState State)
		{
			D.mAccesses.push_back({P.mResources[I], EArdaDependencyAccess::Read, State});
		};
		const auto Write = [&](uint32_t I, EArdaRHIResourceState State)
		{
			D.mAccesses.push_back({P.mResources[I], EArdaDependencyAccess::Write, State});
		};
		Read(0, EArdaRHIResourceState::AccelStructRead);
		Read(6, EArdaRHIResourceState::AccelStructRead);
		for (uint32_t I = 1; I < 4; ++I)
		{
			Read(I, EArdaRHIResourceState::ShaderResource);
		}
		Write(4, EArdaRHIResourceState::UnorderedAccess);
		Read(5, EArdaRHIResourceState::ConstantBuffer);

		D.mPipelineStages = Prepared.mStages;
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::RayTracing, {}, Prepared.mConfiguration}};

		return D;
	}

	FArdaRHIStatus FArdaCornellTraceNode::Record(FArdaDependencyExecutionContext& C,
	    const FParameters& P,
	    const FState& Prepared,
	    FInstanceState& InstanceState)
	{
		auto& Commands = C.GetCommands();
		const auto* Pipeline = C.GetPipeline();
		if (!Pipeline)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
			    "Cornell pipeline inference produced no pipeline.");
		}
		FArdaRHIBindingSetDesc Binding;
		for (const auto& Stage : Prepared.mStages)
		{
			if (!Binding.mLayout && !Stage.mBindingLayouts.empty())
			{
				Binding.mLayout = Stage.mBindingLayouts[0];
			}
		}
		const auto Bind = [&](uint32_t Slot, EArdaRHIBindingType Type, FArdaRHIResourceRef Resource)
		{
			FArdaRHIBindingItem I;
			I.mSlot = Slot;
			I.mType = Type;
			I.mResource = eastl::move(Resource);
			Binding.mItems.push_back(eastl::move(I));
		};
		const auto Buffer = [&](uint32_t I)
		{
			return FArdaRHIResourceRef(C.GetBuffer(P.mResources[I]).Get());
		};
		const auto Texture = [&](uint32_t I)
		{
			return FArdaRHIResourceRef(C.GetTexture(P.mResources[I]).Get());
		};

		Bind(0,
		    EArdaRHIBindingType::RayTracingAccelStruct,
		    FArdaRHIResourceRef(C.GetAccelerationStructure(P.mResources[0]).Get()));
		for (uint32_t I = 1; I < 4; ++I)
		{
			Bind(I, EArdaRHIBindingType::StructuredBufferSRV, Buffer(I));
		}
		Bind(0, EArdaRHIBindingType::StructuredBufferUAV, Buffer(4));
		Bind(0, EArdaRHIBindingType::ConstantBuffer, Buffer(5));
		auto Bound = C.GetDevice()->CreateBindingSet(Binding);
		if (!Bound)
		{
			return Bound.mStatus;
		}
		if (InstanceState.mTablePipeline != Pipeline->mRayTracing)
		{
			FArdaRHIShaderTableDesc Desc;
			Desc.mMaxEntries = 3;
			Desc.mbPersistent = true;
			Desc.mDebugName = "Cornell shader table";
			auto Table = C.GetDevice()->CreateShaderTable(Pipeline->mRayTracing, Desc);
			if (!Table)
			{
				return Table.mStatus;
			}
			if (auto S = C.GetDevice()->SetShaderTableRayGeneration(Table.mValue, "CornellRayGen"); !S)
			{
				return S;
			}
			if (auto R = C.GetDevice()->AddShaderTableMiss(Table.mValue, "CornellMiss"); !R)
			{
				return R.mStatus;
			}
			if (auto R = C.GetDevice()->AddShaderTableHitGroup(Table.mValue, "CornellHitGroup"); !R)
			{
				return R.mStatus;
			}
			if (auto S = C.GetDevice()->CommitShaderTable(Table.mValue); !S)
			{
				return S;
			}
			InstanceState.mShaderTable = eastl::move(Table.mValue);
			InstanceState.mTablePipeline = Pipeline->mRayTracing;
		}
		FArdaRHIRayTracingState State;
		State.mShaderTable = InstanceState.mShaderTable;
		State.mBindings = {Bound.mValue};
		if (auto S = Commands.SetRayTracingState(State); !S)
		{
			return S;
		}
		return Commands.DispatchRays(P.mWidth, P.mHeight, P.mSamples);
	}
}
