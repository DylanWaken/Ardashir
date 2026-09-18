#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Pipelines/ArdaRHIFixedFunctionStates.h"
#include "RHI/Pipelines/ArdaRHIGraphicsPipeline.h"
#include "RHI/Shaders/ArdaRHIShader.h"

#include "RHI/Resources/ArdaHash.h"

#include <EASTL/algorithm.h>

#include <cmath>

namespace arda
{
	namespace
	{
		template <typename T>
		void Combine(size_t& Seed, const T& Value) noexcept
		{
			ArdaHashCombine(Seed, Value);
		}

		template <typename T>
		void CombineRef(size_t& Seed, const TArdaRHIRef<T>& Value) noexcept
		{
			Combine(Seed, reinterpret_cast<uintptr_t>(Value.Get()));
		}

		template <typename T>
		void CombineRefs(size_t& Seed, const eastl::vector<TArdaRHIRef<T>>& Values) noexcept
		{
			Combine(Seed, Values.size());
			for (const auto& Value : Values)
			{
				CombineRef(Seed, Value);
			}
		}

		FArdaRHIStatus Invalid(const char* Message)
		{
			return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message);
		}

		template <typename PipelineDesc>
		void CombineRasterFixedFunctionState(size_t& Hash, const PipelineDesc& Value)
		{
			Combine(Hash, HashValue(Value.mBlendState));
			Combine(Hash, HashValue(Value.mRasterState));
			Combine(Hash, HashValue(Value.mDepthStencilState));
			for (auto Format : Value.mColorFormats)
			{
				Combine(Hash, static_cast<uint8_t>(Format));
			}
			Combine(Hash, static_cast<uint8_t>(Value.mDepthFormat));
			Combine(Hash, Value.mSampleCount);
		}
	}

	size_t HashValue(const FArdaRHIGraphicsPipelineDesc& V) noexcept
	{
		size_t H = 0;
		Combine(H, static_cast<uint8_t>(V.mTopology));
		Combine(H, V.mPatchControlPoints);
		CombineRef(H, V.mInputLayout);
		CombineRef(H, V.mVertexShader);
		CombineRef(H, V.mHullShader);
		CombineRef(H, V.mDomainShader);
		CombineRef(H, V.mGeometryShader);
		CombineRef(H, V.mPixelShader);
		CombineRefs(H, V.mBindingLayouts);
		CombineRasterFixedFunctionState(H, V);
		return H;
	}

	FArdaRHIStatus Validate(const FArdaRHIGraphicsPipelineDesc& V)
	{
		if (!V.mVertexShader)
		{
			return Invalid("Graphics pipeline requires a vertex shader.");
		}
		if (V.mVertexShader->GetStage() != EArdaRHIShaderStage::Vertex)
		{
			return Invalid("Graphics pipeline vertex shader has the wrong stage.");
		}
		if (V.mHullShader && V.mHullShader->GetStage() != EArdaRHIShaderStage::Hull)
		{
			return Invalid("Graphics pipeline hull shader has the wrong stage.");
		}
		if (V.mDomainShader && V.mDomainShader->GetStage() != EArdaRHIShaderStage::Domain)
		{
			return Invalid("Graphics pipeline domain shader has the wrong stage.");
		}
		if (V.mGeometryShader && V.mGeometryShader->GetStage() != EArdaRHIShaderStage::Geometry)
		{
			return Invalid("Graphics pipeline geometry shader has the wrong stage.");
		}
		if (V.mPixelShader && V.mPixelShader->GetStage() != EArdaRHIShaderStage::Pixel)
		{
			return Invalid("Graphics pipeline pixel shader has the wrong stage.");
		}
		if (V.mSampleCount == 0 || V.mColorFormats.size() > ArdaRHIMaxRenderTargets)
		{
			return Invalid("Graphics pipeline sample count and attachment formats are invalid.");
		}
		if (V.mTopology == EArdaRHIPrimitiveTopology::PatchList && V.mPatchControlPoints == 0)
		{
			return Invalid("Patch-list pipelines require control points.");
		}
		return {};
	}
}
