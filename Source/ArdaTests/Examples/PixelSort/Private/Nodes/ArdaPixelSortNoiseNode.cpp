#include "Nodes/ArdaPixelSortNoiseNode.h"
#include "ArdaPixelSortNodeInternal.h"

namespace arda
{
	ARDA_BEGIN_SHADER_PARAMETER_STRUCT(FArdaPixelSortNoiseShaderParameters)
		ARDA_SHADER_CONSTANT_BUFFER(mConstants, 0, 0, EArdaRHIShaderStage::Compute)
		ARDA_SHADER_TEXTURE_UAV(mNoise, 0, 0, EArdaRHIShaderStage::Compute)
	ARDA_END_SHADER_PARAMETER_STRUCT()

	class FArdaPixelSortNoiseShader final : public FArdaGlobalShader
	{
	public:
		using FArdaParameters = FArdaPixelSortNoiseShaderParameters;
		ARDA_DECLARE_GLOBAL_SHADER(FArdaPixelSortNoiseShader);
	};

	ARDA_IMPLEMENT_GLOBAL_SHADER(FArdaPixelSortNoiseShader,
	    GetPixelSortShaderSource(),
	    "PixelSortNoise",
	    "NoiseCS",
	    EArdaRHIShaderStage::Compute)

	struct FArdaPixelSortNoiseNode::FArdaState
	{
		FArdaRHIDeviceRef mDevice;
		FArdaGlobalShaderMap mShaderMap;
		eastl::shared_ptr<const FArdaInductorPipelineConfiguration> mNoise;

		FArdaRHIStatus Initialize()
		{
			// Resolve the node's shader through the shared shader-map compilation path.
			if (!mShaderMap.Initialize(mDevice))
			{
				return PixelSortShaderError(mShaderMap);
			}

			const auto* Shader = mShaderMap.Find(FArdaPixelSortNoiseShader::GetStaticType());
			if (!Shader)
			{
				return PixelSortShaderError(mShaderMap);
			}

			// Keep only immutable pipeline inputs; the graph supplies bindings at execution.
			auto Configuration = eastl::make_shared<FArdaInductorPipelineConfiguration>();
			Configuration->mKind = EArdaPipelineStateKind::Compute;
			Configuration->mCompute =
			    FArdaComputePipelineStateInitializer::FromGlobalShader(*Shader, "PixelSort noise");
			mNoise = eastl::move(Configuration);
			return {};
		}
	};

	FArdaRHIStatus FArdaPixelSortNoiseNode::DeclareResources(FArdaDependencyResourceContext& C, FArdaParameters& P)
	{
		FArdaRHITextureDesc D;
		D.mWidth = P.mWidth;
		D.mHeight = P.mHeight;
		D.mFormat = EArdaRHIFormat::RGBA8UInt;
		D.mbCudaInterop = true;
		D.mUsage = EArdaRHITextureUsage::UnorderedAccess | EArdaRHITextureUsage::ShaderResource;
		return C.Texture(P.mNoise, "Output", D);
	}

	FArdaDependencyNodeMetadata FArdaPixelSortNoiseNode::GetMetadata()
	{
		return {"example.pixel-sort.noise", 1};
	}

	eastl::string FArdaPixelSortNoiseNode::GetCanonicalKey(const FArdaParameters& P)
	{
		return FArdaDependencyKeyBuilder()
		    .Resource(P.mConstants)
		    .Resource(P.mNoise)
		    .Value(P.mWidth)
		    .Value(P.mHeight)
		    .Build();
	}

	FArdaRHIStatus FArdaPixelSortNoiseNode::Validate(const FArdaParameters& P)
	{
		return ValidatePixelSortExtent(P.mWidth, P.mHeight);
	}

	TArdaRHIResult<eastl::shared_ptr<const FArdaPixelSortNoiseNode::FArdaState>> FArdaPixelSortNoiseNode::Prepare(
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

	FArdaDependencyNodeDesc FArdaPixelSortNoiseNode::Describe(const FArdaParameters& P, const FArdaState& Prepared)
	{
		FArdaDependencyNodeDesc D;
		D.BindShader<FArdaPixelSortNoiseShaderParameters>({{"mConstants", P.mConstants}, {"mNoise", P.mNoise}});
		D.mPipelines = {{"default", 0, EArdaPipelineStateKind::Compute, {}, Prepared.mNoise}};

		return D;
	}

	FArdaRHIStatus FArdaPixelSortNoiseNode::Record(FArdaDependencyExecutionContext& C,
	    const FArdaParameters& P,
	    const FArdaState& Prepared,
	    FArdaInstanceState& InstanceState)
	{
		// Bind the executor-prepared pipeline and shader arguments before dispatching this operation.
		if (auto S = C.SetComputeState(); !S)
		{
			return S;
		}

		C.GetCommands().Dispatch((P.mWidth + 7) / 8, (P.mHeight + 7) / 8);
		return {};
	}
}
