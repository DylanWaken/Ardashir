#pragma once
#include "Nodes/ArdaTerrainNodeParameters.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"

namespace arda
{
	inline FArdaRHIStatus TerrainShaderError(const FArdaGlobalShaderMap& Map)
	{
		const auto Diagnostics = Map.GetDiagnostics();
		return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
		    Diagnostics.empty() ? "Terrain node shader initialization failed." : Diagnostics.back().mMessage.c_str());
	}

	constexpr uint32_t DivideRoundUp(uint32_t value, uint32_t divisor)
	{
		return (value + divisor - 1) / divisor;
	}

	template <class P>
	FArdaRHIStatus CreateBindings(FArdaDependencyExecutionContext& C,
	    const P& Parameters,
	    const FArdaRHIBindingLayoutRef& Layout,
	    FArdaRHIBindingSetRef& Result)
	{
		const auto Status = P::GetStaticMetadata().CreateBindingSet(*C.GetDevice(), &Parameters, Layout, Result);
		return Status ? FArdaRHIStatus{}
		              : FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Status.mMessage.c_str());
	}

	template <class Parameters>
	FArdaRHIStatus DispatchHeightmap(FArdaDependencyExecutionContext& C,
	    const Parameters& P,
	    const FArdaRHIBindingLayoutRef& Layout)
	{
		FArdaRHIBindingSetRef Bindings;
		auto Status = CreateBindings(C, P, Layout, Bindings);
		if (!Status)
		{
			return Status;
		}
		FArdaRHIComputeState State;
		State.mPipeline = C.GetPipeline()->mCompute;
		State.mBindings = {Bindings};
		Status = C.GetCommands().SetComputeState(State);
		if (!Status)
		{
			return Status;
		}
		C.GetCommands().Dispatch(DivideRoundUp(ArdaTerrainHeightmapWidth, 8),
		    DivideRoundUp(ArdaTerrainHeightmapHeight, 8),
		    1);
		return {};
	}

	inline void Append(eastl::string& Key, uint64_t Value)
	{
		Key.append(reinterpret_cast<const char*>(&Value), sizeof(Value));
	}

	inline void AppendResource(eastl::string& Key, FArdaDependencyResourceHandle R)
	{
		Append(Key, R.mGraph);
		Append(Key, R.mIndex);
		Append(Key, R.mGeneration);
	}

}
