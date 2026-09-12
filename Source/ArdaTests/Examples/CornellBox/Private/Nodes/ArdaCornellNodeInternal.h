#pragma once
#include "Nodes/ArdaCornellBoxNodes.h"
#include "ShaderStructs/ArdaGlobalShaderMap.h"

namespace arda
{
	inline eastl::string MakeCornellNodeKey(const FArdaCornellNodeParameters& P)
	{
		eastl::string Result;
		const auto Add = [&](uint64_t Value)
		{
			Result.append(reinterpret_cast<const char*>(&Value), sizeof(Value));
		};
		for (auto R : P.mResources)
		{
			Add(R.mGraph);
			Add(R.mIndex);
			Add(R.mGeneration);
		}
		Add(reinterpret_cast<uintptr_t>(P.mFrame.get()));
		Add(P.mWidth);
		Add(P.mHeight);
		Add(P.mSamples);
		Add(P.mVertexCount);
		Add(P.mIndexCount);
		Add(P.mVertexStride);
		Add(static_cast<uint64_t>(P.mBuildFlags));
		Add(P.mWorkspaceBytes);
		return Result;
	}

	inline FArdaRHIStatus CornellShaderError(const FArdaGlobalShaderMap& Map)
	{
		const auto Diagnostics = Map.GetDiagnostics();
		return FArdaRHIStatus::Error(EArdaRHIResult::InvalidState,
		    Diagnostics.empty() ? "Cornell node shader initialization failed." : Diagnostics.back().mMessage.c_str());
	}

	inline FArdaInductorPipelineContribution CornellShaderStage(const FArdaGlobalShaderInstance* Shader,
	    const char* Export = "",
	    const char* HitGroup = "")
	{
		FArdaInductorPipelineContribution S;
		S.mShader = Shader->GetShader();
		S.mBindingLayouts = Shader->GetBindingLayouts();
		S.mExportName = Export;
		S.mHitGroupName = HitGroup;
		return S;
	}
}
