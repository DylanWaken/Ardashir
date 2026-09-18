#pragma once
#include "Nodes/ArdaCornellBoxNodes.h"
#include "RHI/Shaders/ArdaGlobalShaderMap.h"

namespace arda
{
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
