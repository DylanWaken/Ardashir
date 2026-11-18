#pragma once

#include "Graph/Resources/ArdaDependencyResource.h"

namespace arda
{
	template <class ResourceType>
	const void* GetArdaGraphPhysicalIdentity(const TArdaRHIRef<ResourceType>& Resource)
	{
		if (!Resource)
		{
			return nullptr;
		}
		const void* Identity = Resource->GetPhysicalIdentity();
		return Identity ? Identity : Resource.Get();
	}

	inline const void* GetArdaGraphPhysicalIdentity(const FArdaDependencyResourceDesc& Resource)
	{
		if (Resource.mExternalBuffer)
		{
			return GetArdaGraphPhysicalIdentity(Resource.mExternalBuffer);
		}
		if (Resource.mExternalTexture)
		{
			return GetArdaGraphPhysicalIdentity(Resource.mExternalTexture);
		}
		return GetArdaGraphPhysicalIdentity(Resource.mExternalAccelerationStructure);
	}
}
