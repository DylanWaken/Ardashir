#include "RHI/Config/ArdaRHIStatus.h"
#include "RHI/Pipelines/ArdaRHIFixedFunctionStates.h"
#include "RHI/Pipelines/ArdaRHIMeshletPipeline.h"
#include "RHI/Shaders/ArdaRHIShader.h"

#include "ArdaHash.h"

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

	size_t HashValue(const FArdaRHIMeshletPipelineDesc& V) noexcept
	{
		size_t H = 0;
		Combine(H, static_cast<uint8_t>(V.mTopology));
		CombineRef(H, V.mAmplificationShader);
		CombineRef(H, V.mMeshShader);
		CombineRef(H, V.mPixelShader);
		CombineRefs(H, V.mBindingLayouts);
		CombineRasterFixedFunctionState(H, V);
		return H;
	}

	FArdaRHIStatus Validate(const FArdaRHIMeshletPipelineDesc& V)
	{
		if (!V.mMeshShader)
		{
			return Invalid("Meshlet pipeline requires a mesh shader.");
		}
		if (V.mMeshShader->GetStage() != EArdaRHIShaderStage::Mesh)
		{
			return Invalid("Meshlet pipeline mesh shader has the wrong stage.");
		}
		if (V.mAmplificationShader && V.mAmplificationShader->GetStage() != EArdaRHIShaderStage::Amplification)
		{
			return Invalid("Meshlet pipeline amplification shader has the wrong stage.");
		}
		if (V.mPixelShader && V.mPixelShader->GetStage() != EArdaRHIShaderStage::Pixel)
		{
			return Invalid("Meshlet pipeline pixel shader has the wrong stage.");
		}
		if (V.mSampleCount == 0 || V.mColorFormats.size() > ArdaRHIMaxRenderTargets)
		{
			return Invalid("Meshlet pipeline sample count and attachment formats are invalid.");
		}
		return {};
	}
}
