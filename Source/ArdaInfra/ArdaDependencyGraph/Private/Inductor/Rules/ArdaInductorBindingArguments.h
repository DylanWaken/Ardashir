#pragma once

#include "Graph/Node/ArdaDependencyBindings.h"

namespace arda
{
	inline FArdaRHIStatus BindingError(const eastl::string& Message)
	{
		return FArdaRHIStatus::Error(EArdaRHIResult::InvalidArgument, Message.c_str());
	}

	inline const FArdaDependencyShaderResource* FindArgument(const FArdaDependencyShaderBindings& B,
	    const eastl::string& Path,
	    uint32_t E)
	{
		for (const auto& R : B.mResources)
		{
			if (R.mMember == Path && R.mArrayElement == E)
			{
				return &R;
			}
		}
		return nullptr;
	}
}
